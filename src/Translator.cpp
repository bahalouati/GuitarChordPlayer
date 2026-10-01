#include "Translator.h"

#include <QFontDatabase>

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>

bool JsonTranslator::loadJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        m_map.insert(it.key(), it.value().toString());
    return !m_map.isEmpty();
}

QString JsonTranslator::translate(const char *, const char *sourceText, const char *, int) const
{
    const auto it = m_map.constFind(QString::fromUtf8(sourceText));
    return it == m_map.constEnd() ? QString() : *it;
}

QString installLanguage(QApplication &app)
{
    QString lang = QSettings().value(QStringLiteral("language"), QStringLiteral("auto")).toString();
    if (lang == QLatin1String("auto"))
        lang = QLocale::system().language() == QLocale::Arabic ? QStringLiteral("ar") : QStringLiteral("en");
    if (lang == QLatin1String("ar")) {
        auto *tr = new JsonTranslator;
        if (tr->loadJson(QStringLiteral(":/resources/i18n/ar.json"))) {
            app.installTranslator(tr);
            QApplication::setLayoutDirection(Qt::RightToLeft);
            loadAppFonts();
            QApplication::setFont(appTextFont(QApplication::font()));
        } else {
            delete tr;
            lang = QStringLiteral("en");
        }
    }
    return lang;
}

static QString g_textFamily;

void loadAppFonts()
{
    if (!g_textFamily.isEmpty())
        return;
    for (const char *file : {":/resources/fonts/IBMPlexSansArabic-Regular.ttf", ":/resources/fonts/IBMPlexSansArabic-Bold.ttf"}) {
        const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(file));
        if (id >= 0 && g_textFamily.isEmpty())
            g_textFamily = QFontDatabase::applicationFontFamilies(id).value(0);
    }
}

QFont appTextFont(const QFont &base)
{
    loadAppFonts();
    QFont f = base;
    if (!g_textFamily.isEmpty())
        f.setFamilies({g_textFamily, base.family()});
    return f;
}
