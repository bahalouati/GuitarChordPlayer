#pragma once

#include <array>
#include <random>
#include <vector>

// A single plucked string using an extended Karplus-Strong model:
// noise burst -> delay line with loss filter + fractional-delay allpass for tuning.
class PluckedString
{
public:
    void setSampleRate(double sr) { m_sr = sr; }
    void pluck(double freq, float velocity, float brightness, bool muted, std::mt19937 &rng);
    // Quickly damp the string (finger lifted / palm touch).
    void damp(double seconds = 0.08);
    float tick();
    bool active() const { return m_active; }

private:
    void setDecay(double t60);

    double m_sr = 48000.0;
    double m_freq = 110.0;
    std::vector<float> m_buf;
    int m_len = 0;
    int m_pos = 0;
    float m_apCoef = 0.f;
    float m_apX1 = 0.f, m_apY1 = 0.f;
    float m_prev = 0.f;
    float m_loss = 0.996f;
    float m_stretch = 0.5f;
    bool m_active = false;
    int m_silentCount = 0;
};

// Six strings plus a simple guitar-body resonance and a metronome click.
class GuitarSynth
{
public:
    void setSampleRate(double sr);
    void pluck(int stringIndex, double freq, float velocity, float brightness, bool muted);
    void damp(int stringIndex, double seconds = 0.08);
    void dampAll(double seconds = 0.08);
    void click(bool accent);
    float tick();

private:
    struct Biquad
    {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
        void bandpass(double sr, double f, double q);
        float process(float x);
    };

    double m_sr = 48000.0;
    std::array<PluckedString, 6> m_strings;
    std::array<Biquad, 3> m_body;
    std::mt19937 m_rng{12345};
    float m_dcX = 0, m_dcY = 0;
    // metronome
    double m_clickPhase = 0, m_clickFreq = 1000;
    float m_clickEnv = 0, m_clickDecay = 0.999f;
};
