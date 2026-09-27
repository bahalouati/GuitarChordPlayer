#include "WavWriter.h"

#include <QDataStream>
#include <QFile>
#include <algorithm>

bool writeWav(const QString &path, const std::vector<float> &samples, int sampleRate, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QDataStream out(&f);
    out.setByteOrder(QDataStream::LittleEndian);
    const quint32 dataBytes = quint32(samples.size() * 2);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataBytes);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << quint32(sampleRate)
        << quint32(sampleRate * 2) << quint16(2) << quint16(16);
    out.writeRawData("data", 4);
    out << dataBytes;
    for (float s : samples)
        out << qint16(std::clamp(s, -1.f, 1.f) * 32767.f);
    return out.status() == QDataStream::Ok;
}

#include "Sequencer.h"

bool exportSongToWav(std::shared_ptr<const Timeline> tl, const QString &path, double tempoScale,
                     QString *error)
{
    constexpr int sr = 44100;
    Sequencer seq;
    seq.setSampleRate(sr);
    seq.setTimeline(std::move(tl));
    seq.setCountIn(false);
    seq.setTempoScale(tempoScale);
    seq.setVolume(0.9f);
    seq.play();

    std::vector<float> out;
    std::vector<float> block(1024);
    const size_t maxFrames = size_t(sr) * 60 * 20; // safety limit: 20 minutes
    while (seq.isPlaying() && out.size() < maxFrames) {
        seq.render(block.data(), int(block.size()));
        out.insert(out.end(), block.begin(), block.end());
    }
    // Let the last chord ring out.
    for (int i = 0; i < sr * 3 / int(block.size()); ++i) {
        seq.render(block.data(), int(block.size()));
        out.insert(out.end(), block.begin(), block.end());
    }
    return writeWav(path, out, sr, error);
}
