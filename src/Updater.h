#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QWidget;

// Checks the GitHub releases of the repository for a newer build and installs it (Windows).
// CI publishes every build of the main branch as a release tagged "build-<number>".
class Updater : public QObject
{
    Q_OBJECT
public:
    explicit Updater(QWidget *window);

    static int currentBuild();
    static QString repository();

    // interactive: also report "up to date" and errors, and offer the update right away.
    void check(bool interactive);
    // Downloads the latest release and replaces this installation, then restarts.
    void install();

signals:
    void updateAvailable(int build, const QString &notes);

private:
    void offer(int build, const QString &notes);
    bool installDirWritable() const;

    QWidget *m_window;
    QNetworkAccessManager *m_net;
    int m_latestBuild = 0;
    QString m_assetUrl;
    QString m_pageUrl;
    bool m_busy = false;
};
