#include "GuitarSynth.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace {
constexpr double kPi = 3.14159265358979323846;

// Phase delay (in samples) of a first-order filter (b0 + b1 z^-1) / (1 + a1 z^-1) at angular frequency w.
double phaseDelay(double b0, double b1, double a1, double w)
{
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> h = (b0 + b1 * z1) / (1.0 + a1 * z1);
    return -std::arg(h) / w;
}

// One-pole lowpass (1-a)/(1-a z^-1)
double lowpassDelay(double a, double w)
{
    return phaseDelay(1.0 - a, 0.0, -a, w);
}

// First-order allpass (c + z^-1)/(1 + c z^-1)
double allpassDelay(double c, double w)
{
    double d = phaseDelay(c, 1.0, c, w);
    if (d < 0)
        d += 2 * kPi / w;
    return d;
}
} // namespace

// ---------------------------------------------------------------- StringLoop

void StringLoop::setup(double sampleRate, double freq, double brightness, double stiffness)
{
    m_sr = sampleRate;
    m_freq = freq;
    const double w = 2 * kPi * freq / sampleRate;

    // Brighter strings lose less high end each period.
    m_lpA = float(std::clamp(0.55 - 0.45 * brightness, 0.04, 0.7));
    m_stiffA = float(stiffness);

    // Total loop delay must be one period; subtract what the filters add.
    double period = sampleRate / freq;
    period -= lowpassDelay(m_lpA, w);
    if (m_stiffA != 0.f)
        period -= allpassDelay(m_stiffA, w);
    int len = int(std::floor(period));
    double frac = period - len;
    if (frac < 0.1) {
        --len;
        frac += 1.0;
    }
    len = std::max(len, 2);
    m_fracA = float((1.0 - frac) / (1.0 + frac));

    if (int(m_buf.size()) != len)
        m_buf.assign(size_t(len), 0.f);
    m_len = len;
    m_pos = 0;
}

void StringLoop::excite(const std::vector<float> &shape, float gain, bool addToExisting)
{
    const size_t n = shape.size();
    for (int i = 0; i < m_len; ++i) {
        const float v = shape[size_t(i) * n / size_t(m_len)] * gain;
        float &cell = m_buf[size_t((m_pos + i) % m_len)];
        cell = addToExisting ? 0.25f * cell + v : v;
    }
    m_lpState = 0.f;
    m_stX = m_stY = m_frX = m_frY = 0.f;
    m_quietCount = 0;
}

void StringLoop::setDecay(double t60)
{
    m_loss = float(std::pow(0.001, 1.0 / (m_freq * std::max(0.01, t60))));
}

float StringLoop::tick()
{
    const float out = m_buf[size_t(m_pos)];
    // Loss: gain plus one-pole lowpass (high harmonics die faster).
    m_lpState = (1.f - m_lpA) * out + m_lpA * m_lpState;
    float x = m_loss * m_lpState;
    // Stiffness: disperses high partials (slightly sharp overtones on wound strings).
    if (m_stiffA != 0.f) {
        const float y = m_stiffA * x + m_stX - m_stiffA * m_stY;
        m_stX = x;
        m_stY = y;
        x = y;
    }
    // Fractional delay for exact tuning.
    const float y = m_fracA * x + m_frX - m_fracA * m_frY;
    m_frX = x;
    m_frY = y;
    m_buf[size_t(m_pos)] = y;
    if (++m_pos >= m_len)
        m_pos = 0;

    if (std::abs(out) < 2e-5f)
        ++m_quietCount;
    else
        m_quietCount = 0;
    return out;
}

// ---------------------------------------------------------------- GuitarString

void GuitarString::pluck(double freq, float velocity, float brightness, bool muted, bool wound,
                         std::mt19937 &rng)
{
    // Harder picking = brighter tone.
    const double bright = std::clamp(0.35 + 0.45 * brightness * (0.6 + 0.4 * velocity), 0.1, 0.95);
    const double stiff = wound ? -0.12 : -0.04;
    // The second polarization is a hair flat, so the two drift in and out of phase.
    m_a.setup(m_sr, freq * 1.00035, muted ? 0.1 : bright, stiff);
    m_b.setup(m_sr, freq * 0.99965, muted ? 0.08 : bright * 0.85, stiff);

    // Excitation: filtered noise shaped by where the pick hits the string (~1/8 from the bridge).
    const int n = std::max(8, int(m_sr / freq));
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    std::vector<float> ex(static_cast<size_t>(n));
    const float lp = float(std::clamp(0.1 + 0.85 * bright, 0.05, 0.98));
    float s = 0.f;
    for (int i = 0; i < n; ++i) {
        s += lp * (dist(rng) - s);
        ex[size_t(i)] = s;
    }
    // Blend in a smooth "finger/pick displacement" shape for body at low velocities.
    const int apex = std::max(1, n / 8);
    for (int i = 0; i < n; ++i) {
        const float tri = i < apex ? float(i) / apex : float(n - i) / float(n - apex);
        ex[size_t(i)] = 0.65f * ex[size_t(i)] + 0.35f * (tri - 0.5f);
    }
    const int comb = std::max(1, n / 8);
    std::vector<float> shaped(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        shaped[size_t(i)] = ex[size_t(i)] - 0.85f * ex[size_t((i + n - comb) % n)];
    float mean = 0.f, peak = 1e-6f;
    for (float v : shaped)
        mean += v;
    mean /= float(n);
    for (float &v : shaped) {
        v -= mean;
        peak = std::max(peak, std::abs(v));
    }
    const float gain = velocity / peak;
    m_a.excite(shaped, gain, m_active);
    m_b.excite(shaped, gain, m_active);

    const double t60 = muted ? 0.05 : std::clamp(8.0 - freq / 70.0, 2.5, 7.0);
    m_a.setDecay(t60);
    m_b.setDecay(t60 * 0.8);
    m_mixB = muted ? 0.0f : 0.35f;

    // Pick transient: 3-6 ms of bright noise, louder for hard strums and muted chucks.
    const int clickLen = int(m_sr * (muted ? 0.012 : 0.004));
    m_click.assign(size_t(clickLen), 0.f);
    float hp = 0.f, prev = 0.f;
    const float amp = (muted ? 0.5f : 0.12f) * velocity;
    for (int i = 0; i < clickLen; ++i) {
        const float x = dist(rng);
        hp = 0.7f * (hp + x - prev); // high-pass
        prev = x;
        const float env = std::exp(-6.f * float(i) / float(clickLen));
        m_click[size_t(i)] = hp * env * amp;
    }
    m_clickPos = 0;
    m_active = true;
}

void GuitarString::damp(double seconds)
{
    if (!m_active)
        return;
    m_a.setDecay(seconds);
    m_b.setDecay(seconds);
}

float GuitarString::tick()
{
    if (!m_active)
        return 0.f;
    float out = m_a.tick() * (1.f - m_mixB) + m_b.tick() * m_mixB;
    if (m_clickPos < m_click.size())
        out += m_click[m_clickPos++];
    if (m_a.quiet() && m_b.quiet() && m_clickPos >= m_click.size())
        m_active = false;
    return out;
}

// ---------------------------------------------------------------- GuitarSynth

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

void GuitarSynth::Biquad::highShelf(double sr, double f, double gainDb)
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2 * kPi * f / sr;
    const double cw = std::cos(w), sw = std::sin(w);
    const double alpha = sw / 2 * std::sqrt(2.0);
    const double sa = 2 * std::sqrt(A) * alpha;
    const double a0 = (A + 1) - (A - 1) * cw + sa;
    b0 = float(A * ((A + 1) + (A - 1) * cw + sa) / a0);
    b1 = float(-2 * A * ((A - 1) + (A + 1) * cw) / a0);
    b2 = float(A * ((A + 1) + (A - 1) * cw - sa) / a0);
    a1 = float(2 * ((A - 1) - (A + 1) * cw) / a0);
    a2 = float(((A + 1) - (A - 1) * cw - sa) / a0);
}

float GuitarSynth::Biquad::process(float x)
{
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

float GuitarSynth::Comb::process(float x)
{
    const float out = buf[pos];
    store = out * (1.f - damp) + store * damp;
    buf[pos] = x + store * feedback;
    if (++pos >= buf.size())
        pos = 0;
    return out;
}

float GuitarSynth::Allpass::process(float x)
{
    const float b = buf[pos];
    const float out = -x + b;
    buf[pos] = x + b * 0.5f;
    if (++pos >= buf.size())
        pos = 0;
    return out;
}

void GuitarSynth::setSampleRate(double sr)
{
    m_sr = sr;
    for (auto &s : m_strings)
        s.setSampleRate(sr);
    // Low strings a little to the left, high strings a little to the right (player's view).
    for (int i = 0; i < 6; ++i) {
        const double pan = -0.35 + 0.7 * i / 5.0; // -1..1
        const double a = (pan + 1) * kPi / 4;
        m_panL[size_t(i)] = float(std::cos(a));
        m_panR[size_t(i)] = float(std::sin(a));
    }
    // Acoustic guitar body: air cavity, top plate modes and some presence.
    m_body[0].bandpass(sr, 98, 3.0);
    m_body[1].bandpass(sr, 196, 2.5);
    m_body[2].bandpass(sr, 410, 2.0);
    m_body[3].bandpass(sr, 2600, 0.9);
    m_shelfL.highShelf(sr, 6000, -4.0);
    m_shelfR.highShelf(sr, 6000, -4.0);

    // Room: Freeverb-like tunings scaled to the sample rate, slightly different per side.
    const int combs[4] = {1116, 1188, 1277, 1356};
    const int aps[2] = {556, 441};
    const double scale = sr / 44100.0 * 0.6; // small room
    for (int i = 0; i < 4; ++i) {
        m_combL[size_t(i)].buf.assign(size_t(combs[i] * scale), 0.f);
        m_combR[size_t(i)].buf.assign(size_t((combs[i] + 23) * scale), 0.f);
        m_combL[size_t(i)].feedback = m_combR[size_t(i)].feedback = 0.78f;
        m_combL[size_t(i)].damp = m_combR[size_t(i)].damp = 0.35f;
    }
    for (int i = 0; i < 2; ++i) {
        m_apL[size_t(i)].buf.assign(size_t(aps[i] * scale), 0.f);
        m_apR[size_t(i)].buf.assign(size_t((aps[i] + 23) * scale), 0.f);
    }
}

void GuitarSynth::pluck(int stringIndex, double freq, float velocity, float brightness, bool muted)
{
    if (stringIndex >= 0 && stringIndex < 6)
        m_strings[size_t(stringIndex)].pluck(freq, velocity, brightness, muted, stringIndex < 3, m_rng);
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

void GuitarSynth::tick(float &left, float &right)
{
    float l = 0.f, r = 0.f, mono = 0.f;
    for (int i = 0; i < 6; ++i) {
        const float s = m_strings[size_t(i)].tick();
        l += s * m_panL[size_t(i)];
        r += s * m_panR[size_t(i)];
        mono += s;
    }
    const float g = 0.42f;
    l *= g;
    r *= g;
    mono *= g;

    // Body resonance is shared by both channels.
    const float body = 0.8f * m_body[0].process(mono) + 0.6f * m_body[1].process(mono)
            + 0.35f * m_body[2].process(mono) + 0.2f * m_body[3].process(mono);
    l = m_shelfL.process(l + body * 0.7f);
    r = m_shelfR.process(r + body * 0.7f);

    // DC blockers
    float y = l - m_dcXL + 0.995f * m_dcYL;
    m_dcXL = l;
    m_dcYL = y;
    l = y;
    y = r - m_dcXR + 0.995f * m_dcYR;
    m_dcXR = r;
    m_dcYR = y;
    r = y;

    // Room reverb
    if (m_reverbWet > 0.f) {
        const float in = (l + r) * 0.5f * 0.25f;
        float wl = 0.f, wr = 0.f;
        for (size_t i = 0; i < 4; ++i) {
            wl += m_combL[i].process(in);
            wr += m_combR[i].process(in);
        }
        for (size_t i = 0; i < 2; ++i) {
            wl = m_apL[i].process(wl);
            wr = m_apR[i].process(wr);
        }
        l += wl * m_reverbWet;
        r += wr * m_reverbWet;
    }

    if (m_clickEnv > 1e-4f) {
        const float c = m_clickEnv * float(std::sin(m_clickPhase));
        l += c;
        r += c;
        m_clickPhase += 2 * kPi * m_clickFreq / m_sr;
        m_clickEnv *= m_clickDecay;
    }
    // Soft limiter
    left = std::tanh(l);
    right = std::tanh(r);
}
