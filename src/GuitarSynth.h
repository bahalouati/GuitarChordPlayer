#pragma once

#include <array>
#include <random>
#include <vector>

// Which guitar is simulated.
enum class GuitarTone { Acoustic = 0, Nylon = 1, Electric = 2 };

// Per-tone settings of the string model.
struct ToneParams
{
    double pickCutoff = 8000;     // brightness of the pick/finger attack (Hz, at full strength)
    double pluckPosition = 0.12;  // where along the string it is plucked (fraction from the bridge)
    double brightness = 1.0;      // scales how slowly high harmonics die
    double t60Low = 7.0, t60High = 2.6; // sustain of the lowest / highest notes (seconds)
    double stiffWound = -0.12, stiffPlain = -0.04;
    float polarization = 0.35f;   // level of the second, slightly detuned polarization
    float pickNoise = 0.05f;      // extra pick "tick"
    float level = 1.0f;
};

// One polarization of a vibrating string: a Karplus-Strong style delay loop with a
// frequency-dependent loss filter, an optional stiffness allpass (wound strings) and a
// fractional-delay allpass that keeps the pitch exact. Driven by an input signal.
class StringLoop
{
public:
    void setup(double sampleRate, double freq, double brightness, double stiffness);
    void scale(float f);           // quickly quiet what is ringing (the pick touches the string)
    void clear();
    void setDecay(double t60Seconds);
    float tick(float input);
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
// "breathing" decay). It is excited with the guitar body's impulse response shaped by the
// pick ("commuted synthesis"), so each note rings through a wooden body.
class GuitarString
{
public:
    void setSampleRate(double sr) { m_sr = sr; }
    void pluck(double freq, float velocity, float brightness, bool muted, bool wound,
               const ToneParams &tone, const std::vector<float> &body, std::mt19937 &rng);
    void damp(double seconds);
    float tick();
    bool active() const { return m_active; }

private:
    double m_sr = 48000.0;
    StringLoop m_a, m_b;
    float m_mixB = 0.3f;
    bool m_active = false;
    std::vector<float> m_exc;     // excitation still to be fed into the string
    size_t m_excPos = 0;
};

// Six strings, stereo room reverb and a metronome click.
class GuitarSynth
{
public:
    void setSampleRate(double sr);
    void setTone(GuitarTone tone);
    GuitarTone tone() const { return m_tone; }
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
    void buildBody();

    double m_sr = 48000.0;
    GuitarTone m_tone = GuitarTone::Acoustic;
    ToneParams m_params;
    std::vector<float> m_body;    // body impulse response for the current tone
    std::array<GuitarString, 6> m_strings;
    std::array<float, 6> m_panL{}, m_panR{};
    Biquad m_shelfL, m_shelfR;
    std::array<Comb, 4> m_combL, m_combR;
    std::array<Allpass, 2> m_apL, m_apR;
    float m_reverbWet = 0.14f;
    std::mt19937 m_rng{12345};
    float m_dcXL = 0, m_dcYL = 0, m_dcXR = 0, m_dcYR = 0;
    double m_clickPhase = 0, m_clickFreq = 1000;
    float m_clickEnv = 0, m_clickDecay = 0.999f;
};
