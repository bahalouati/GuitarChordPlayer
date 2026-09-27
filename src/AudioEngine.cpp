#include "AudioEngine.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QMediaDevices>
#include <algorithm>
#include <cstring>

SynthDevice::SynthDevice(Sequencer *seq, QMutex *mutex, const QAudioFormat &fmt, QObject *parent)
    : QIODevice(parent), m_seq(seq), m_mutex(mutex), m_fmt(fmt)
{
}

qint64 SynthDevice::bytesAvailable() const
{
    // An endless stream: always have data ready.
    return qint64(m_fmt.bytesPerFrame()) * 4096 + QIODevice::bytesAvailable();
}

qint64 SynthDevice::readData(char *data, qint64 maxlen)
{
    const int bpf = m_fmt.bytesPerFrame();
    const int channels = m_fmt.channelCount();
    const int frames = int(maxlen / bpf);
    if (frames <= 0)
        return 0;
    if (m_left.size() < size_t(frames)) {
        m_left.resize(size_t(frames));
        m_right.resize(size_t(frames));
    }

    {
        QMutexLocker lock(m_mutex);
        m_seq->render(m_left.data(), m_right.data(), frames);
    }

    const int bps = m_fmt.bytesPerSample();
    std::memset(data, 0, size_t(frames) * size_t(bpf));
    for (int f = 0; f < frames; ++f) {
        const float lr[2] = {std::clamp(m_left[size_t(f)], -1.f, 1.f), std::clamp(m_right[size_t(f)], -1.f, 1.f)};
        for (int c = 0; c < std::min(channels, 2); ++c) {
            // Mono devices get the average of both sides.
            const float v = channels == 1 ? 0.5f * (lr[0] + lr[1]) : lr[c];
            char *dst = data + f * bpf + c * bps;
            switch (m_fmt.sampleFormat()) {
            case QAudioFormat::Float:
                std::memcpy(dst, &v, sizeof(float));
                break;
            case QAudioFormat::Int32: {
                const qint32 smp = qint32(v * 2147483000.f);
                std::memcpy(dst, &smp, sizeof(smp));
                break;
            }
            case QAudioFormat::Int16: {
                const qint16 smp = qint16(v * 32767.f);
                std::memcpy(dst, &smp, sizeof(smp));
                break;
            }
            case QAudioFormat::UInt8:
                *reinterpret_cast<quint8 *>(dst) = quint8(128 + int(v * 127.f));
                break;
            default:
                break;
            }
        }
    }
    return qint64(frames) * bpf;
}

AudioEngine::AudioEngine(QObject *parent) : QObject(parent) {}

AudioEngine::~AudioEngine()
{
    if (m_sink)
        m_sink->stop();
}

bool AudioEngine::start(QString *error)
{
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) {
        if (error)
            *error = tr("No audio output device found.");
        return false;
    }

    QAudioFormat fmt = dev.preferredFormat();
    if (fmt.sampleRate() <= 0)
        fmt.setSampleRate(48000);
    if (fmt.channelCount() <= 0)
        fmt.setChannelCount(2);
    QAudioFormat f = fmt;
    f.setSampleFormat(QAudioFormat::Float);
    if (dev.isFormatSupported(f)) {
        fmt = f;
    } else {
        f.setSampleFormat(QAudioFormat::Int16);
        if (dev.isFormatSupported(f))
            fmt = f;
    }
    m_format = fmt;
    m_seq.setSampleRate(fmt.sampleRate());

    m_device = new SynthDevice(&m_seq, &m_mutex, fmt, this);
    m_device->open(QIODevice::ReadOnly);

    m_sink = new QAudioSink(dev, fmt, this);
    // ~40 ms of buffering keeps the strums tight but avoids dropouts.
    m_sink->setBufferSize(fmt.bytesForDuration(40000));
    m_sink->start(m_device);
    if (m_sink->error() != QAudio::NoError) {
        if (error)
            *error = tr("Could not start audio output (error %1).").arg(int(m_sink->error()));
        return false;
    }
    return true;
}

void AudioEngine::setTimeline(std::shared_ptr<const Timeline> tl)
{
    QMutexLocker l(&m_mutex);
    m_seq.setTimeline(std::move(tl));
}

void AudioEngine::replaceTimeline(std::shared_ptr<const Timeline> tl)
{
    QMutexLocker l(&m_mutex);
    m_seq.replaceTimeline(std::move(tl));
}

void AudioEngine::play()
{
    QMutexLocker l(&m_mutex);
    m_seq.play();
}

void AudioEngine::pause()
{
    QMutexLocker l(&m_mutex);
    m_seq.pause();
}

void AudioEngine::stop()
{
    QMutexLocker l(&m_mutex);
    m_seq.stop();
}

void AudioEngine::seekToBar(int bar)
{
    QMutexLocker l(&m_mutex);
    m_seq.seekToBar(bar);
}

bool AudioEngine::isPlaying()
{
    QMutexLocker l(&m_mutex);
    return m_seq.isPlaying();
}

void AudioEngine::setTempoScale(double s)
{
    QMutexLocker l(&m_mutex);
    m_seq.setTempoScale(s);
}

void AudioEngine::setLoop(int firstBar, int lastBar)
{
    QMutexLocker l(&m_mutex);
    m_seq.setLoop(firstBar, lastBar);
}

void AudioEngine::setMetronome(bool on)
{
    QMutexLocker l(&m_mutex);
    m_seq.setMetronome(on);
}

void AudioEngine::setCountIn(bool on)
{
    QMutexLocker l(&m_mutex);
    m_seq.setCountIn(on);
}

void AudioEngine::setVolume(float v)
{
    QMutexLocker l(&m_mutex);
    m_seq.setVolume(v);
}

int AudioEngine::sampleRate() const
{
    return m_format.sampleRate() > 0 ? m_format.sampleRate() : 48000;
}

void AudioEngine::setAudio(std::shared_ptr<const AudioClip> clip, double offset)
{
    QMutexLocker l(&m_mutex);
    m_seq.setAudio(std::move(clip), offset);
}

void AudioEngine::setAudioOffset(double offset)
{
    QMutexLocker l(&m_mutex);
    m_seq.setAudioOffset(offset);
}

void AudioEngine::setAudioVolume(float v)
{
    QMutexLocker l(&m_mutex);
    m_seq.setAudioVolume(v);
}

void AudioEngine::setAudioEnabled(bool on)
{
    QMutexLocker l(&m_mutex);
    m_seq.setAudioEnabled(on);
}

void AudioEngine::setGuitarEnabled(bool on)
{
    QMutexLocker l(&m_mutex);
    m_seq.setGuitarEnabled(on);
}

void AudioEngine::previewChord(const ChordShape &chord)
{
    QMutexLocker l(&m_mutex);
    m_seq.previewChord(chord);
}

Sequencer::Snapshot AudioEngine::snapshot()
{
    QMutexLocker l(&m_mutex);
    const qint64 generated = m_seq.framesRendered();
    qint64 played;
    const qint64 us = m_sink ? m_sink->processedUSecs() : 0;
    if (us > 0) {
        played = qint64(double(us) * m_format.sampleRate() / 1e6);
    } else {
        const int bpf = std::max(1, m_format.bytesPerFrame());
        played = generated - (m_sink ? m_sink->bufferSize() / bpf : 0);
    }
    played = std::min(played, generated);
    return m_seq.snapshot(played);
}
