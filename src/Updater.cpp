#include "Updater.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>
#include <QWidget>

#ifndef APP_BUILD
#define APP_BUILD 0
#endif
#ifndef UPDATE_REPO
#define UPDATE_REPO "bahalouati/GuitarChordPlayer"
#endif

namespace {
const char *kAssetName = "GuitarChordPlayer-windows.zip";

// Waits for the app to exit, unpacks the new version over the old one and starts it again.
[[maybe_unused]] const char *kUpdateScript = R"PS(
param([int]$ProcId, [string]$Zip, [string]$Dest, [string]$Exe)
$log = Join-Path $env:TEMP 'GuitarChordPlayer-update.log'
"Updating $Dest from $Zip" | Out-File $log
try {
    Wait-Process -Id $ProcId -Timeout 60 -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
    $tmp = Join-Path $env:TEMP ('GuitarChordPlayer-update-' + [guid]::NewGuid())
    Expand-Archive -Path $Zip -DestinationPath $tmp -Force
    $src = $tmp
    $items = @(Get-ChildItem $tmp)
    if ($items.Count -eq 1 -and $items[0].PSIsContainer) { $src = $items[0].FullName }
    Copy-Item -Path (Join-Path $src '*') -Destination $Dest -Recurse -Force
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item $Zip -Force -ErrorAction SilentlyContinue
    "Done" | Out-File $log -Append
} catch {
    "Failed: $_" | Out-File $log -Append
}
Start-Process -FilePath $Exe
)PS";
} // namespace

Updater::Updater(QWidget *window) : QObject(window), m_window(window), m_net(new QNetworkAccessManager(this))
{
    // Use the system's proxy settings, like a browser would.
    QNetworkProxyFactory::setUseSystemConfiguration(true);
}

int Updater::currentBuild()
{
    return APP_BUILD;
}

QString Updater::repository()
{
    return QStringLiteral(UPDATE_REPO);
}

void Updater::check(bool interactive)
{
    if (m_busy)
        return;
    m_busy = true;
    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repository())));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GuitarChordPlayer"));
    req.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, interactive] {
        reply->deleteLater();
        m_busy = false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            qWarning("Update check failed: %d %s", status, qPrintable(reply->errorString()));
            if (interactive) {
                QMessageBox::warning(m_window, tr("Check for updates"),
                                     status == 404 ? tr("No releases have been published yet.")
                                                   : tr("Could not reach GitHub:\n%1").arg(reply->errorString()));
            }
            return;
        }
        const QJsonObject rel = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = rel.value(QStringLiteral("tag_name")).toString();
        const auto m = QRegularExpression(QStringLiteral("(\\d+)$")).match(tag);
        m_latestBuild = m.hasMatch() ? m.captured(1).toInt() : 0;
        m_pageUrl = rel.value(QStringLiteral("html_url")).toString();
        m_assetUrl.clear();
        for (const QJsonValue &a : rel.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject asset = a.toObject();
            if (asset.value(QStringLiteral("name")).toString() == QLatin1String(kAssetName))
                m_assetUrl = asset.value(QStringLiteral("browser_download_url")).toString();
        }
        const QString notes = rel.value(QStringLiteral("body")).toString().trimmed();

        const bool newer = m_latestBuild > currentBuild();
        if (newer && currentBuild() > 0)
            emit updateAvailable(m_latestBuild, notes);
        if (!interactive)
            return;
        if (newer || currentBuild() == 0)
            offer(m_latestBuild, notes);
        else
            QMessageBox::information(m_window, tr("Check for updates"),
                                     tr("You have the latest version (build %1).").arg(currentBuild()));
    });
}

void Updater::offer(int build, const QString &notes)
{
    QMessageBox box(m_window);
    box.setWindowTitle(tr("Update available"));
    box.setIcon(QMessageBox::Information);
    const QString current = currentBuild() > 0 ? tr("build %1").arg(currentBuild()) : tr("a development build");
    box.setText(tr("<b>Build %1 is available.</b> You have %2.").arg(build).arg(current));
    box.setInformativeText(tr("What's new:\n%1\n\nYour own songs (Documents\\Guitar Chord Player) are kept.")
                           .arg(notes.left(800)));
#ifdef Q_OS_WIN
    QPushButton *installBtn = box.addButton(tr("Install and restart"), QMessageBox::AcceptRole);
#else
    QPushButton *installBtn = nullptr;
#endif
    QPushButton *pageBtn = box.addButton(tr("Open download page"), QMessageBox::ActionRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.exec();
    if (installBtn && box.clickedButton() == installBtn)
        install();
    else if (box.clickedButton() == pageBtn)
        QDesktopServices::openUrl(QUrl(m_pageUrl));
}

bool Updater::installDirWritable() const
{
    QTemporaryFile probe(QCoreApplication::applicationDirPath() + QStringLiteral("/.write-test-XXXXXX"));
    return probe.open();
}

void Updater::install()
{
#ifndef Q_OS_WIN
    QMessageBox::information(m_window, tr("Update"),
                             tr("Automatic install is only available on Windows.\n"
                                "On Linux, run \"git pull\" in the source folder and rebuild."));
    return;
#else
    if (m_assetUrl.isEmpty()) {
        QMessageBox::warning(m_window, tr("Update"), tr("The latest release has no Windows download."));
        return;
    }
    if (!installDirWritable()) {
        QMessageBox::warning(m_window, tr("Update"),
                             tr("Can't write to %1.\nMove the app to a folder you own (e.g. Documents or Desktop), "
                                "or download the update yourself.").arg(QCoreApplication::applicationDirPath()));
        QDesktopServices::openUrl(QUrl(m_pageUrl));
        return;
    }

    const QString zipPath = QDir::temp().filePath(QStringLiteral("GuitarChordPlayer-build-%1.zip").arg(m_latestBuild));
    auto *progress = new QProgressDialog(tr("Downloading build %1...").arg(m_latestBuild), tr("Cancel"), 0, 100, m_window);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);

    QNetworkRequest req{QUrl(m_assetUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GuitarChordPlayer"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::downloadProgress, progress, [progress](qint64 got, qint64 total) {
        if (total > 0)
            progress->setValue(int(got * 100 / total));
    });
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, progress, zipPath] {
        reply->deleteLater();
        progress->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() != QNetworkReply::OperationCanceledError)
                QMessageBox::warning(m_window, tr("Update"), tr("Download failed:\n%1").arg(reply->errorString()));
            return;
        }
        QSaveFile zip(zipPath);
        if (!zip.open(QIODevice::WriteOnly) || zip.write(reply->readAll()) < 0 || !zip.commit()) {
            QMessageBox::warning(m_window, tr("Update"), tr("Could not save the download to %1").arg(zipPath));
            return;
        }
        const QString scriptPath = QDir::temp().filePath(QStringLiteral("GuitarChordPlayer-update.ps1"));
        QFile script(scriptPath);
        if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate) || script.write(kUpdateScript) < 0) {
            QMessageBox::warning(m_window, tr("Update"), tr("Could not write the update script."));
            return;
        }
        script.close();

        // Close the window first so unsaved edits can be saved (or the update cancelled).
        if (!m_window->close())
            return;
        const QStringList args = {
            QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-File"), scriptPath,
            QStringLiteral("-ProcId"), QString::number(QCoreApplication::applicationPid()),
            QStringLiteral("-Zip"), QDir::toNativeSeparators(zipPath),
            QStringLiteral("-Dest"), QDir::toNativeSeparators(QCoreApplication::applicationDirPath()),
            QStringLiteral("-Exe"), QDir::toNativeSeparators(QCoreApplication::applicationFilePath())};
        if (!QProcess::startDetached(QStringLiteral("powershell.exe"), args)) {
            QMessageBox::warning(nullptr, tr("Update"), tr("Could not start the updater."));
            return;
        }
        QCoreApplication::quit();
    });
#endif
}
