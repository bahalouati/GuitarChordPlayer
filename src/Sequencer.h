#pragma once

#include "GuitarSynth.h"
#include "Song.h"

#include <array>
#include <deque>
#include <memory>
#include <random>
#include <vector>

// Turns a Timeline into guitar sound, sample by sample.
// Not thread-safe: AudioEngine guards it with a mutex.
class Sequencer
{
public:
    // Where playback is, as seen by the listener (latency already applied).
    struct Snapshot
    {
        bool playing = false;
        bool countIn = false;   // true while the count-in clicks play
        int countInBeat = 0;
        bool finished = false;  // reached the end of the song
        int bar = 0;
        int step = 0;
        double stepFraction = 0.0;
        std::array<float, 6> stringGlow{}; // 0..1 per string, index 0 = low E
    };

    void setSampleRate(double sr);
    double sampleRate() const { return m_sr; }
    void setTimeline(std::shared_ptr<const Timeline> tl);

    void play();
    void pause();
    void stop();
    void seekToBar(int bar);
    bool isPlaying() const { return m_playing; }

    void setTempoScale(double s) { m_tempoScale = s; }
    void setLoop(int firstBar, int lastBar) { m_loopFirst = firstBar; m_loopLast = lastBar; }
    void setMetronome(bool on) { m_metronome = on; }
    void setCountIn(bool on) { m_countInEnabled = on; }
    void setVolume(float v) { m_volume = v; }

    // Strums a chord right away (chord finder preview), independent of the song.
    void previewChord(const ChordShape &chord);

    void render(float *out, int frames);
    qint64 framesRendered() const { return m_frame; }
    Snapshot snapshot(qint64 playedFrame) const;

private:
    struct Pending
    {
        int delay;
        int string;
        double freq;
        float velocity;
        float brightness;
        bool muted;
    };
    struct PosEntry
    {
        qint64 frame;
        int bar;      // -1 = count-in, -2 = finished
        int step;
        double length;
        bool playing;
    };
    struct PluckEvent
    {
        qint64 frame;
        int string;
        float velocity;
    };

    void fireNextStep();
    void perform(const PatternStep &step, int chord);
    void changeChord(int chord);
    void schedule(int string, int fret, double delaySec, float velocity, float brightness, bool muted);
    double stepSamples(const Timeline::Bar &bar) const;
    double noteFreq(int string, int fret) const;
    void recordPos(int bar, int step, double length);

    double m_sr = 48000.0;
    GuitarSynth m_synth;
    std::shared_ptr<const Timeline> m_tl;
    std::mt19937 m_rng{4242};

    bool m_playing = false;
    int m_bar = 0;
    int m_step = 0;
    double m_samplesToNext = 0;
    int m_countInLeft = 0;
    int m_countInTotal = 0;
    int m_curChord = -2;
    qint64 m_frame = 0;

    double m_tempoScale = 1.0;
    int m_loopFirst = -1, m_loopLast = -1;
    bool m_metronome = false;
    bool m_countInEnabled = true;
    float m_volume = 0.8f;

    std::vector<Pending> m_pending;
    std::deque<PosEntry> m_history;
    std::deque<PluckEvent> m_plucks;
};
