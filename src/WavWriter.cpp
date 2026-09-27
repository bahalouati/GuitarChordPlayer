#include "WavWriter.h"

#include <QDataStream>
#include <QFile>
#include <algorithm>

bool writeWav(const QString &path, const std::vector<float> &left, const std::vector<float> &right,
              int sampleRate, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QDataStream out(&f);
    out.setByteOrder(QDataStream::LittleEndian);
    const quint32 dataBytes = quint32(left.size() * 4);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataBytes);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(2) << quint32(sampleRate)
        << quint32(sampleRate * 4) << quint16(4) << quint16(16);
    out.writeRawData("data", 4);
    out << dataBytes;
    for (size_t i = 0; i < left.size(); ++i)
        out << qint16(std::clamp(left[i], -1.f, 1.f) * 32767.f) << qint16(std::clamp(right[i], -1.f, 1.f) * 32767.f);
    return out.status() == QDataStream::Ok;
}

#include "Sequencer.h"

bool exportSongToWav(std::shared_ptr<const Timeline> tl, const QString &path, double tempoScale,
                     QString *error, std::shared_ptr<const AudioClip> audio, double audioOffset, GuitarTone tone)
{
    const int sr = audio ? audio->sampleRate : 44100;
    Sequencer seq;
    seq.setSampleRate(sr);
    seq.setTone(tone);
    seq.setTimeline(std::move(tl));
    if (audio)
        seq.setAudio(audio, audioOffset);
    seq.setCountIn(false);
    seq.setTempoScale(tempoScale);
    seq.setVolume(0.9f);
    seq.play();

    std::vector<float> outL, outR;
    std::vector<float> bl(1024), br(1024);
    const size_t maxFrames = size_t(sr) * 60 * 20; // safety limit: 20 minutes
    while (seq.isPlaying() && outL.size() < maxFrames) {
        seq.render(bl.data(), br.data(), int(bl.size()));
        outL.insert(outL.end(), bl.begin(), bl.end());
        outR.insert(outR.end(), br.begin(), br.end());
    }
    // Let the last chord ring out.
    for (int i = 0; i < sr * 3 / int(bl.size()); ++i) {
        seq.render(bl.data(), br.data(), int(bl.size()));
        outL.insert(outL.end(), bl.begin(), bl.end());
        outR.insert(outR.end(), br.begin(), br.end());
    }
    return writeWav(path, outL, outR, sr, error);
}
