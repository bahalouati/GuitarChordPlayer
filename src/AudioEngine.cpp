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
    if (m_scratch.size() < size_t(frames))
        m_scratch.resize(size_t(frames));

    {
        QMutexLocker lock(m_mutex);
        m_seq->render(m_scratch.data(), frames);
    }

    const int bps = m_fmt.bytesPerSample();
    std::memset(data, 0, size_t(frames) * size_t(bpf));
    for (int f = 0; f < frames; ++f) {
        const float v = std::clamp(m_scratch[size_t(f)], -1.f, 1.f);
        // Same signal on the first two channels (left/right), silence elsewhere.
        for (int c = 0; c < std::min(channels, 2); ++c) {
            char *dst = data + f * bpf + c * bps;
            switch (m_fmt.sampleFormat()) {
            case QAudioFormat::Float:
                std::memcpy(dst, &v, sizeof(float));
                break;
            case QAudioFormat::Int32: {
                const qint32 s = qint32(v * 2147483000.f);
                std::memcpy(dst, &s, sizeof(s));
                break;
            }
            case QAudioFormat::Int16: {
                const qint16 s = qint16(v * 32767.f);
                std::memcpy(dst, &s, sizeof(s));
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
