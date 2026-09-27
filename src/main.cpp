#include "MainWindow.h"
#include "Song.h"
#include "WavWriter.h"

#include <QApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
    // Headless export: GuitarChordPlayer --render song.xml out.wav
    if (argc == 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--render")) {
        QCoreApplication app(argc, argv);
        QTextStream err(stderr);
        QString error;
        auto song = loadSong(QString::fromLocal8Bit(argv[2]), &error);
        auto tl = song ? buildTimeline(song, &error) : nullptr;
        if (!tl || !exportSongToWav(tl, QString::fromLocal8Bit(argv[3]), 1.0, &error)) {
            err << "Error: " << error << Qt::endl;
            return 1;
        }
        return 0;
    }

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("GuitarChordPlayer"));
    QApplication::setApplicationName(QStringLiteral("GuitarChordPlayer"));
    MainWindow w;
    w.show();
    if (argc > 1)
        w.loadSongFile(QString::fromLocal8Bit(argv[1]));
    return app.exec();
}
