#pragma once

#include <array>
#include <random>
#include <vector>

// One polarization of a vibrating string: a Karplus-Strong style delay loop with a
// frequency-dependent loss filter, an optional stiffness allpass (wound strings) and a
// fractional-delay allpass that keeps the pitch exact.
class StringLoop
{
public:
    void setup(double sampleRate, double freq, double brightness, double stiffness);
    void excite(const std::vector<float> &shape, float gain, bool addToExisting);
    void setDecay(double t60Seconds);
    float tick();
    bool quiet() const { return m_quietCount > m_len * 4; }

private:
    double m_sr = 48000.0;
    double m_freq = 110.0;
    std::vector<float> m_buf;
    int m_len = 2;
    int m_pos = 0;
    float m_loss = 0.999f;     // per-period gain
    float m_lpA = 0.2f;        // one-pole lowpass coefficient (higher = darker)
    float m_lpState = 0.f;
    float m_stiffA = 0.f;      // dispersion allpass coefficient
    float m_stX = 0.f, m_stY = 0.f;
    float m_fracA = 0.f;       // tuning allpass coefficient
    float m_frX = 0.f, m_frY = 0.f;
    int m_quietCount = 0;
};

// A guitar string made of two slightly detuned polarizations (gives the natural
// "breathing" decay), plus a short pick-noise transient.
class GuitarString
{
public:
    void setSampleRate(double sr) { m_sr = sr; }
    void pluck(double freq, float velocity, float brightness, bool muted, bool wound, std::mt19937 &rng);
    void damp(double seconds);
    float tick();
    bool active() const { return m_active; }

private:
    double m_sr = 48000.0;
    StringLoop m_a, m_b;
    float m_mixB = 0.3f;
    bool m_active = false;
    // Pick transient: band-limited noise burst.
    std::vector<float> m_click;
    size_t m_clickPos = 0;
};

// Six strings, guitar body, stereo room reverb and a metronome click.
class GuitarSynth
{
public:
    void setSampleRate(double sr);
    void pluck(int stringIndex, double freq, float velocity, float brightness, bool muted);
    void damp(int stringIndex, double seconds = 0.08);
    void dampAll(double seconds = 0.08);
    void click(bool accent);
    void setReverb(float wet) { m_reverbWet = wet; }
    void tick(float &left, float &right);

private:
    struct Biquad
    {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
        void bandpass(double sr, double f, double q);
        void highShelf(double sr, double f, double gainDb);
        float process(float x);
    };
    // Small Schroeder/Freeverb-style room.
    struct Comb
    {
        std::vector<float> buf;
        size_t pos = 0;
        float store = 0, feedback = 0.8f, damp = 0.3f;
        float process(float x);
    };
    struct Allpass
    {
        std::vector<float> buf;
        size_t pos = 0;
        float process(float x);
    };

    double m_sr = 48000.0;
    std::array<GuitarString, 6> m_strings;
    std::array<float, 6> m_panL{}, m_panR{};
    std::array<Biquad, 4> m_body;
    Biquad m_shelfL, m_shelfR;
    std::array<Comb, 4> m_combL, m_combR;
    std::array<Allpass, 2> m_apL, m_apR;
    float m_reverbWet = 0.14f;
    std::mt19937 m_rng{12345};
    float m_dcXL = 0, m_dcYL = 0, m_dcXR = 0, m_dcYR = 0;
    double m_clickPhase = 0, m_clickFreq = 1000;
    float m_clickEnv = 0, m_clickDecay = 0.999f;
};
