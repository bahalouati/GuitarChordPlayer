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

ToneParams paramsFor(GuitarTone tone)
{
    ToneParams p;
    switch (tone) {
    case GuitarTone::Acoustic:
        // Steel strings, flat pick near the soundhole, dreadnought body.
        p.pickCutoff = 9000;
        p.pluckPosition = 0.13;
        p.brightness = 1.0;
        p.t60Low = 7.5;
        p.t60High = 2.8;
        p.polarization = 0.35f;
        p.pickNoise = 0.04f;
        p.level = 1.0f;
        break;
    case GuitarTone::Nylon:
        // Classical guitar: fingers, softer and darker, shorter sustain.
        p.pickCutoff = 3200;
        p.pluckPosition = 0.18;
        p.brightness = 0.6;
        p.t60Low = 5.0;
        p.t60High = 1.6;
        p.stiffWound = -0.08;
        p.stiffPlain = -0.02;
        p.polarization = 0.25f;
        p.pickNoise = 0.0f;
        p.level = 1.15f;
        break;
    case GuitarTone::Electric:
        // Clean electric: no acoustic body, long sustain, pickup near the bridge.
        p.pickCutoff = 5000;
        p.pluckPosition = 0.1;
        p.brightness = 1.1;
        p.t60Low = 9.0;
        p.t60High = 4.0;
        p.polarization = 0.3f;
        p.pickNoise = 0.03f;
        p.level = 1.5f;
        break;
    }
    return p;
}

} // namespace

// ---------------------------------------------------------------- StringLoop

void StringLoop::setup(double sampleRate, double freq, double brightness, double stiffness)
{
    m_sr = sampleRate;
    m_freq = freq;
    const double w = 2 * kPi * freq / sampleRate;

    // Brighter strings lose less high end each period.
    m_lpA = float(std::clamp(0.32 - 0.3 * brightness, 0.01, 0.6));
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

    if (int(m_buf.size()) != len) {
        // A new pitch: the old vibration can't continue at a different length.
        m_buf.assign(size_t(len), 0.f);
        m_lpState = 0.f;
        m_stX = m_stY = m_frX = m_frY = 0.f;
    }
    m_len = len;
    m_pos = std::min(m_pos, m_len - 1);
}

void StringLoop::scale(float f)
{
    for (float &v : m_buf)
        v *= f;
    m_lpState *= f;
    m_stX *= f;
    m_stY *= f;
    m_frX *= f;
    m_frY *= f;
}

void StringLoop::clear()
{
    std::fill(m_buf.begin(), m_buf.end(), 0.f);
    m_lpState = m_stX = m_stY = m_frX = m_frY = 0.f;
    m_quietCount = 0;
}

void StringLoop::setDecay(double t60)
{
    m_loss = float(std::pow(0.001, 1.0 / (m_freq * std::max(0.01, t60))));
}

float StringLoop::tick(float input)
{
    const float delayed = m_buf[size_t(m_pos)];
    // Loss: gain plus one-pole lowpass (high harmonics die faster).
    m_lpState = (1.f - m_lpA) * delayed + m_lpA * m_lpState;
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
    const float out = y + input;
    m_buf[size_t(m_pos)] = out;
    if (++m_pos >= m_len)
        m_pos = 0;

    if (std::abs(out) < 2e-5f && input == 0.f)
        ++m_quietCount;
    else
        m_quietCount = 0;
    return out;
}

// ---------------------------------------------------------------- GuitarString

void GuitarString::pluck(double freq, float velocity, float brightness, bool muted, bool wound,
                         const ToneParams &tone, const std::vector<float> &body, std::mt19937 &rng)
{
    // Harder picking = brighter tone.
    const double bright = std::clamp((0.45 + 0.4 * brightness * (0.6 + 0.4 * velocity)) * tone.brightness, 0.1, 0.97);
    const double stiff = wound ? tone.stiffWound : tone.stiffPlain;
    // The two polarizations are a hair apart, so they drift in and out of phase.
    m_a.setup(m_sr, freq * 1.00035, muted ? 0.1 : bright, stiff);
    m_b.setup(m_sr, freq * 0.99965, muted ? 0.08 : bright * 0.85, stiff);
    // The pick (or finger) stops most of the old vibration before starting the new one.
    m_a.scale(0.25f);
    m_b.scale(0.25f);

    const double t60 = muted ? 0.05
                             : std::clamp(tone.t60Low - (tone.t60Low - tone.t60High) * std::log2(freq / 82.0) / 3.0,
                                          tone.t60High, tone.t60Low);
    m_a.setDecay(t60);
    m_b.setDecay(t60 * 0.75);
    m_mixB = muted ? 0.0f : tone.polarization;

    // ---- Excitation: the body's impulse response, shaped by the pick.
    const size_t bodyLen = muted ? std::min<size_t>(body.size(), size_t(m_sr * 0.03)) : body.size();
    std::vector<float> ex(bodyLen + 8, 0.f);
    std::copy(body.begin(), body.begin() + long(bodyLen), ex.begin());
    // Pick/finger contact: a smooth low-pass. Softer notes (and fingers) are darker.
    const double cutoff = tone.pickCutoff * (0.35 + 0.65 * velocity) * (muted ? 0.4 : 1.0);
    const float a = float(std::exp(-2 * kPi * std::min(cutoff, m_sr * 0.45) / m_sr));
    float st = 0.f;
    for (float &v : ex) {
        st = (1.f - a) * v + a * st;
        v = st;
    }
    // Where the string is plucked cancels some harmonics (the comb filter of a plucked string).
    const int m = std::max(1, int(std::lround(tone.pluckPosition * m_sr / freq)));
    for (size_t i = ex.size(); i-- > size_t(m);)
        ex[i] -= 0.9f * ex[i - size_t(m)];
    // A little randomness so no two plucks are identical.
    std::uniform_real_distribution<float> jitter(-1.f, 1.f);
    for (size_t i = 0; i < std::min<size_t>(ex.size(), size_t(m_sr * 0.004)); ++i)
        ex[i] += tone.pickNoise * jitter(rng) * std::exp(-float(i) / float(m_sr * 0.0012));

    // ---- Level: simulate the string briefly and scale so every note has the same loudness
    // for the same velocity, whatever its pitch and the body resonances.
    StringLoop probe = m_a;
    probe.clear();
    double energy = 0;
    const size_t probeLen = std::min(ex.size() + size_t(m_sr * 0.05), size_t(m_sr * 0.2));
    for (size_t i = 0; i < probeLen; ++i) {
        const float o = probe.tick(i < ex.size() ? ex[i] : 0.f);
        energy += double(o) * o;
    }
    const double rms = std::sqrt(energy / double(probeLen)) + 1e-9;
    const float gain = float(0.1 * velocity * tone.level / rms) * (muted ? 0.7f : 1.0f);
    for (float &v : ex)
        v *= gain;
    m_exc.swap(ex);
    m_excPos = 0;
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
    const float in = m_excPos < m_exc.size() ? m_exc[m_excPos++] : 0.f;
    const float out = m_a.tick(in) * (1.f - m_mixB) + m_b.tick(in) * m_mixB;
    if (m_excPos >= m_exc.size() && m_a.quiet() && m_b.quiet())
        m_active = false;
    return out;
}

// ---------------------------------------------------------------- GuitarSynth

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

// The response of a guitar body to a tap on the bridge, built from its resonances:
// the air in the body (Helmholtz, ~100 Hz), the top plate modes (~200-900 Hz) and a
// dense cloud of smaller wood resonances up to a few kHz, each ringing and dying away.
void GuitarSynth::buildBody()
{
    if (m_tone == GuitarTone::Electric) {
        // Solid body: essentially no body sound, just a short bridge "tick".
        m_body.assign(size_t(m_sr * 0.004), 0.f);
        m_body[0] = 1.f;
        m_body[1] = 0.4f;
        return;
    }
    struct Mode { double f, amp, ms; };
    const bool nylon = m_tone == GuitarTone::Nylon;
    std::vector<Mode> modes = {
        {nylon ? 96.0 : 102.0, 1.0, 70},   // air resonance
        {nylon ? 185.0 : 198.0, 1.1, 55},  // top plate (1,1)
        {232, 0.55, 45},                   // back plate
        {nylon ? 355.0 : 372.0, 0.6, 38},  // top (2,1)
        {408, 0.45, 32},
        {nylon ? 465.0 : 486.0, 0.5, 30},  // top (1,2)
        {566, 0.38, 26},
        {648, 0.32, 24},
        {782, 0.3, 20},
        {880, 0.26, 18},
    };
    // The big low resonances give the warmth; keep them from drowning the brighter ones.
    for (Mode &mo : modes)
        mo.amp *= nylon ? 0.5 : 0.5;
    std::mt19937 rng(nylon ? 777u : 2024u);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    // Dense higher modes; a nylon guitar's (lighter bracing, fingers) fade out sooner.
    const double top = nylon ? 5000.0 : 6500.0;
    for (int i = 0; i < 120; ++i) {
        const double f = 950.0 * std::pow(top / 950.0, u(rng));
        const double amp = (nylon ? 0.2 : 0.26) * std::pow(f / 950.0, nylon ? -0.75 : -0.45) * (0.4 + 0.6 * u(rng));
        const double ms = 22.0 * std::pow(950.0 / f, 0.5) * (0.7 + 0.6 * u(rng));
        modes.push_back({f, amp, ms});
    }
    const size_t len = size_t(m_sr * 0.16);
    std::vector<double> ir(len, 0.0);
    for (const Mode &mo : modes) {
        if (mo.f >= m_sr * 0.45)
            continue;
        const double w = 2 * kPi * mo.f / m_sr;
        const double decay = std::exp(-1.0 / (mo.ms / 1000.0 * m_sr));
        const double phase = 2 * kPi * u(rng);
        // Recursive sine oscillator with exponential decay.
        double env = mo.amp;
        for (size_t n = 0; n < len; ++n) {
            ir[n] += env * std::sin(w * double(n) + phase);
            env *= decay;
            if (env < 1e-5)
                break;
        }
    }
    // The direct push of the bridge: keeps the attack crisp.
    ir[0] += nylon ? 1.2 : 2.0;
    ir[1] += nylon ? 0.6 : 0.8;
    // Gentle fade at the end.
    const size_t fade = size_t(m_sr * 0.03);
    for (size_t n = 0; n < fade; ++n)
        ir[len - 1 - n] *= double(n) / double(fade);
    double peak = 1e-9;
    for (double v : ir)
        peak = std::max(peak, std::abs(v));
    m_body.resize(len);
    for (size_t n = 0; n < len; ++n)
        m_body[n] = float(ir[n] / peak);
}

void GuitarSynth::setTone(GuitarTone tone)
{
    m_tone = tone;
    m_params = paramsFor(tone);
    buildBody();
    // Electric: a little brighter top end; acoustic/nylon: soften harshness.
    const double shelf = tone == GuitarTone::Electric ? -4.0 : tone == GuitarTone::Nylon ? -3.0 : -2.0;
    m_shelfL.highShelf(m_sr, 6000, shelf);
    m_shelfR.highShelf(m_sr, 6000, shelf);
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
    setTone(m_tone);

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
        m_strings[size_t(stringIndex)].pluck(freq, velocity, brightness, muted, stringIndex < 3, m_params, m_body, m_rng);
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
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 6; ++i) {
        const float s = m_strings[size_t(i)].tick();
        l += s * m_panL[size_t(i)];
        r += s * m_panR[size_t(i)];
    }
    l = m_shelfL.process(l);
    r = m_shelfR.process(r);

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
