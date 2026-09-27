#pragma once

#include <QString>
#include <functional>
#include <memory>
#include <vector>

// A decoded recording, stereo float at the engine's sample rate.
struct AudioClip
{
    int sampleRate = 48000;
    std::vector<float> left, right;
    QString path;

    size_t frames() const { return left.size(); }
    double seconds() const { return sampleRate > 0 ? double(frames()) / sampleRate : 0.0; }
};

// Decodes MP3/WAV/OGG/M4A... with Qt Multimedia and resamples to targetRate.
// Runs a local event loop; progress (0..1) is reported through the callback.
std::shared_ptr<AudioClip> decodeAudioFile(const QString &path, int targetRate, QString *error,
                                           const std::function<void(double)> &progress = {});

// Plays an AudioClip at a variable speed without changing its pitch (WSOLA time stretching).
// At rate 1.0 it copies samples directly.
class Stretcher
{
public:
    void setClip(std::shared_ptr<const AudioClip> clip);
    const AudioClip *clip() const { return m_clip.get(); }
    // Jump to a position in seconds (can be negative: silence until 0).
    void seek(double seconds);
    // Where the next output sample comes from, in seconds of the recording.
    double position() const;
    // Fills frames of stereo output at the given speed (0.25 .. 2).
    void render(float *left, float *right, int frames, double rate);

private:
    void produceBlock(double rate);
    float sampleL(long long i) const;
    float sampleR(long long i) const;

    std::shared_ptr<const AudioClip> m_clip;
    double m_readPos = 0.0;          // next analysis position (frames)
    long long m_prevActual = 0;      // start of the last used window
    bool m_havePrev = false;
    std::vector<float> m_overlapL, m_overlapR;
    std::vector<float> m_outL, m_outR; // produced but not yet played
    size_t m_outPos = 0;
    std::vector<float> m_window;
    int m_fadeIn = 0;
    double m_lastRate = 1.0;
    static constexpr int kN = 2048;   // window length
    static constexpr int kHop = kN / 2;
    static constexpr int kSearch = 384;
};
