#pragma once

#include "Sequencer.h"

#include <QAudioFormat>
#include <QIODevice>
#include <QMutex>
#include <QObject>
#include <memory>

class QAudioSink;

// Pull-mode audio source: the sound card asks for samples, we render them.
class SynthDevice : public QIODevice
{
    Q_OBJECT
public:
    SynthDevice(Sequencer *seq, QMutex *mutex, const QAudioFormat &fmt, QObject *parent = nullptr);

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    Sequencer *m_seq;
    QMutex *m_mutex;
    QAudioFormat m_fmt;
    std::vector<float> m_left, m_right;
};

class AudioEngine : public QObject
{
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    bool start(QString *error);

    void setTimeline(std::shared_ptr<const Timeline> tl);
    void replaceTimeline(std::shared_ptr<const Timeline> tl);
    void play();
    void pause();
    void stop();
    void seekToBar(int bar);
    bool isPlaying();

    void setTempoScale(double s);
    void setLoop(int firstBar, int lastBar);
    void setMetronome(bool on);
    void setCountIn(bool on);
    void setVolume(float v);
    void previewChord(const ChordShape &chord);
    void setTone(GuitarTone tone);
    GuitarTone tone() const { return m_tone; }

    // Recording played along (see Sequencer::setAudio).
    int sampleRate() const;
    void setAudio(std::shared_ptr<const AudioClip> clip, double offset);
    void setAudioOffset(double offset);
    void setAudioVolume(float v);
    void setAudioEnabled(bool on);
    void setGuitarEnabled(bool on);

    // Playback position as currently heard through the speakers.
    Sequencer::Snapshot snapshot();

private:
    QMutex m_mutex;
    Sequencer m_seq;
    QAudioFormat m_format;
    QAudioSink *m_sink = nullptr;
    SynthDevice *m_device = nullptr;
    GuitarTone m_tone = GuitarTone::Acoustic;
};
