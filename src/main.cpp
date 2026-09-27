#include "MainWindow.h"
#include "Song.h"
#include "WavWriter.h"
#include "ChordName.h"
#include "Translator.h"
#include "AudioTrack.h"
#include "ChordDetector.h"

#include <QFileInfo>

#include <QApplication>
#include <cmath>
#include <QIcon>
#include <QSettings>
#include <QTextStream>

int main(int argc, char *argv[])
{
#ifdef Q_OS_LINUX
    // Prefer Qt's FFmpeg backend for decoding MP3s (the GStreamer one needs extra plugins).
    if (qEnvironmentVariableIsEmpty("QT_MEDIA_BACKEND"))
        qputenv("QT_MEDIA_BACKEND", "ffmpeg");
#endif
    // Headless export: GuitarChordPlayer --render song.xml out.wav
    if (argc == 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--render")) {
        QCoreApplication app(argc, argv);
        QTextStream err(stderr);
        QString error;
        auto song = loadSong(QString::fromLocal8Bit(argv[2]), &error);
        auto tl = song ? buildTimeline(song, &error) : nullptr;
        std::shared_ptr<AudioClip> clip;
        if (tl && !song->audioFile.isEmpty()) {
            clip = decodeAudioFile(song->audioFile, 44100, &error);
            if (!clip)
                err << "Warning: recording not loaded: " << error << Qt::endl;
        }
        // GCP_TONE=acoustic|nylon|electric picks the guitar for command-line renders.
        const QByteArray toneName = qgetenv("GCP_TONE");
        const GuitarTone tone = toneName == "nylon" ? GuitarTone::Nylon
                              : toneName == "electric" ? GuitarTone::Electric : GuitarTone::Acoustic;
        if (!tl || !exportSongToWav(tl, QString::fromLocal8Bit(argv[3]), 1.0, &error, clip, song->audioOffset, tone)) {
            err << "Error: " << error << Qt::endl;
            return 1;
        }
        return 0;
    }

    // Decoding check: GuitarChordPlayer --decode song.mp3  (prints length and level)
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--decode")) {
        QCoreApplication app(argc, argv);
        QTextStream out(stdout);
        QString error;
        auto clip = decodeAudioFile(QString::fromLocal8Bit(argv[2]), 44100, &error);
        if (!clip) {
            out << "Error: " << error << Qt::endl;
            return 1;
        }
        double sum = 0;
        for (float v : clip->left)
            sum += double(v) * v;
        out << "ok " << clip->seconds() << " s, rms " << std::sqrt(sum / std::max<size_t>(1, clip->frames())) << Qt::endl;
        return 0;
    }

    // Chord detection: GuitarChordPlayer --detect song.mp3 out.xml
    if (argc == 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--detect")) {
        QCoreApplication app(argc, argv);
        QTextStream err(stderr);
        QString error;
        const QString in = QString::fromLocal8Bit(argv[2]);
        auto clip = decodeAudioFile(in, 44100, &error);
        DetectedSong song;
        if (!clip || !detectChords(*clip, &song, &error)) {
            err << "Error: " << error << Qt::endl;
            return 1;
        }
        QFile f(QString::fromLocal8Bit(argv[3]));
        if (!f.open(QIODevice::WriteOnly)) {
            err << "Error: cannot write " << f.fileName() << Qt::endl;
            return 1;
        }
        f.write(detectedSongToXml(song, QFileInfo(in).completeBaseName(), QString(), QFileInfo(in).fileName()).toUtf8());
        err << "bpm " << song.bpm << ", " << song.beatsPerBar << " beats/bar, offset " << song.offset
            << "s, capo " << song.capo << ", key " << song.key << ", " << song.bars.size() << " bars, fit "
            << song.confidence << Qt::endl;
        return 0;
    }

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("GuitarChordPlayer"));
    QApplication::setApplicationName(QStringLiteral("GuitarChordPlayer"));
#ifdef APP_VERSION
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
#endif
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/app.png")));
    installLanguage(app);
    MainWindow w;
    w.show();
    if (argc > 1)
        w.loadSongFile(QString::fromLocal8Bit(argv[1]));
    return app.exec();
}
