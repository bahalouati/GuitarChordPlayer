#include "GuitarSynth.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void PluckedString::setDecay(double t60)
{
    // Per-period gain so that the fundamental falls 60 dB in t60 seconds.
    m_loss = float(std::pow(0.001, 1.0 / (m_freq * t60)));
}

void PluckedString::pluck(double freq, float velocity, float brightness, bool muted, std::mt19937 &rng)
{
    m_freq = freq;
    // The two-point loss filter adds half a sample of delay; the allpass takes the fraction.
    double period = m_sr / freq - 0.5;
    int len = int(std::floor(period));
    double frac = period - len;
    if (frac < 0.1) {
        --len;
        frac += 1.0;
    }
    len = std::max(len, 2);
    m_apCoef = float((1.0 - frac) / (1.0 + frac));

    std::vector<float> old = m_buf;
    const int oldLen = m_len;
    m_buf.assign(size_t(len), 0.f);
    m_len = len;
    m_pos = 0;

    // Excitation: filtered noise. Softer / darker picks -> stronger lowpass.
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    const float lp = std::clamp(0.15f + 0.8f * brightness * (0.5f + 0.5f * velocity), 0.05f, 0.98f);
    float state = 0.f;
    for (int i = 0; i < len; ++i) {
        state += lp * (dist(rng) - state);
        m_buf[size_t(i)] = state;
    }
    // Pick-position comb (plucking ~1/7 of the way from the bridge) removes some harmonics.
    const int comb = std::max(1, int(len * 0.14));
    std::vector<float> ex = m_buf;
    for (int i = 0; i < len; ++i)
        m_buf[size_t(i)] = ex[size_t(i)] - 0.9f * ex[size_t((i + len - comb) % len)];
    // Remove DC and normalise.
    float mean = 0.f, peak = 1e-6f;
    for (float v : m_buf)
        mean += v;
    mean /= float(len);
    for (float &v : m_buf) {
        v -= mean;
        peak = std::max(peak, std::abs(v));
    }
    const float gain = velocity / peak;
    for (int i = 0; i < len; ++i) {
        float v = m_buf[size_t(i)] * gain;
        // A string that is still ringing keeps a little of its old energy.
        if (m_active && oldLen > 0)
            v += 0.15f * old[size_t(i % oldLen)];
        m_buf[size_t(i)] = v;
    }

    m_apX1 = m_apY1 = 0.f;
    m_prev = 0.f;
    // Low strings sustain longer than high ones.
    const double t60 = muted ? 0.045 : std::clamp(7.0 - freq / 90.0, 2.0, 6.0);
    m_stretch = muted ? 0.8f : 0.5f; // muted notes: darker thud
    setDecay(t60);
    m_active = true;
    m_silentCount = 0;
}

void PluckedString::damp(double seconds)
{
    if (m_active)
        setDecay(seconds);
}

float PluckedString::tick()
{
    if (!m_active)
        return 0.f;
    const float cur = m_buf[size_t(m_pos)];
    // Two-point averaging loss filter.
    const float filtered = m_loss * ((1.f - m_stretch) * cur + m_stretch * m_prev);
    m_prev = cur;
    // First-order allpass for the fractional part of the delay.
    const float ap = m_apCoef * filtered + m_apX1 - m_apCoef * m_apY1;
    m_apX1 = filtered;
    m_apY1 = ap;
    m_buf[size_t(m_pos)] = ap;
    if (++m_pos >= m_len)
        m_pos = 0;

    if (std::abs(cur) < 1e-5f) {
        if (++m_silentCount > m_len * 4)
            m_active = false;
    } else {
        m_silentCount = 0;
    }
    return cur;
}

void GuitarSynth::Biquad::bandpass(double sr, double f, double q)
{
    const double w = 2 * kPi * f / sr;
    const double alpha = std::sin(w) / (2 * q);
    const double a0 = 1 + alpha;
    b0 = float(alpha / a0);
    b1 = 0;
    b2 = float(-alpha / a0);
    a1 = float(-2 * std::cos(w) / a0);
    a2 = float((1 - alpha) / a0);
}

float GuitarSynth::Biquad::process(float x)
{
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

void GuitarSynth::setSampleRate(double sr)
{
    m_sr = sr;
    for (auto &s : m_strings)
        s.setSampleRate(sr);
    // Rough acoustic guitar body: air resonance, top plate, and some upper-mid presence.
    m_body[0].bandpass(sr, 105, 2.5);
    m_body[1].bandpass(sr, 220, 2.0);
    m_body[2].bandpass(sr, 2800, 0.8);
}

void GuitarSynth::pluck(int stringIndex, double freq, float velocity, float brightness, bool muted)
{
    if (stringIndex >= 0 && stringIndex < 6)
        m_strings[size_t(stringIndex)].pluck(freq, velocity, brightness, muted, m_rng);
}

void GuitarSynth::damp(int stringIndex, double seconds)
{
    if (stringIndex >= 0 && stringIndex < 6)
        m_strings[size_t(stringIndex)].damp(seconds);
}

void GuitarSynth::dampAll(double seconds)
{
    for (auto &s : m_strings)
        s.damp(seconds);
}

void GuitarSynth::click(bool accent)
{
    m_clickFreq = accent ? 1760 : 1175;
    m_clickPhase = 0;
    m_clickEnv = accent ? 0.35f : 0.22f;
    m_clickDecay = float(std::exp(-1.0 / (0.012 * m_sr)));
}

float GuitarSynth::tick()
{
    float dry = 0.f;
    for (auto &s : m_strings)
        dry += s.tick();
    dry *= 0.4f;

    float body = 0.9f * m_body[0].process(dry) + 0.6f * m_body[1].process(dry) + 0.25f * m_body[2].process(dry);
    float out = dry + body;

    // DC blocker
    const float y = out - m_dcX + 0.995f * m_dcY;
    m_dcX = out;
    m_dcY = y;
    out = y;

    if (m_clickEnv > 1e-4f) {
        out += m_clickEnv * float(std::sin(m_clickPhase));
        m_clickPhase += 2 * kPi * m_clickFreq / m_sr;
        m_clickEnv *= m_clickDecay;
    }
    // Gentle soft clip so big strums never crackle.
    return std::tanh(out);
}
