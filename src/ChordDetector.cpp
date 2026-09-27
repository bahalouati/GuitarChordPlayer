#include "ChordDetector.h"

#include "Arranger.h"
#include "ChordLibrary.h"
#include "ChordName.h"

#include <QXmlStreamWriter>
#include <QtGlobal>
#include <cstdio>
#include <array>
#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <numeric>

namespace {

constexpr double kPi = 3.14159265358979323846;
const char *kNames[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"};

// In-place iterative radix-2 FFT (size must be a power of two).
void fft(std::vector<std::complex<float>> &a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * kPi / double(len);
        const std::complex<float> wl(float(std::cos(ang)), float(std::sin(ang)));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Magnitude spectra of a signal (Hann window), one vector of n/2 bins per hop.
std::vector<std::vector<float>> stft(const std::vector<float> &x, size_t n, size_t hop,
                                     const std::function<bool(double)> &tick)
{
    std::vector<float> win(n);
    for (size_t i = 0; i < n; ++i)
        win[i] = float(0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(n)));
    std::vector<std::vector<float>> out;
    std::vector<std::complex<float>> buf(n);
    const size_t frames = x.size() > n ? (x.size() - n) / hop + 1 : 1;
    out.reserve(frames);
    for (size_t f = 0; f < frames; ++f) {
        const size_t start = f * hop;
        for (size_t i = 0; i < n; ++i) {
            const size_t k = start + i;
            buf[i] = std::complex<float>(k < x.size() ? x[k] * win[i] : 0.f, 0.f);
        }
        fft(buf);
        std::vector<float> mag(n / 2);
        for (size_t i = 0; i < n / 2; ++i)
            mag[i] = std::abs(buf[i]);
        out.push_back(std::move(mag));
        if ((f & 63) == 0 && tick && !tick(double(f) / double(frames)))
            return {};
    }
    return out;
}

// Mono mix, low-passed and decimated to about 11-12 kHz for analysis.
std::vector<float> analysisSignal(const AudioClip &clip, int *rate)
{
    const int factor = std::max(1, int(std::lround(clip.sampleRate / 11025.0)));
    *rate = clip.sampleRate / factor;
    // Windowed-sinc low-pass at 0.45 of the new Nyquist.
    const int taps = 8 * factor + 1;
    std::vector<float> h(static_cast<size_t>(taps));
    const double fc = 0.45 / factor;
    double sum = 0;
    for (int i = 0; i < taps; ++i) {
        const double m = i - (taps - 1) / 2.0;
        const double sinc = m == 0 ? 2 * fc : std::sin(2 * kPi * fc * m) / (kPi * m);
        const double w = 0.54 - 0.46 * std::cos(2 * kPi * i / (taps - 1));
        h[size_t(i)] = float(sinc * w);
        sum += h[size_t(i)];
    }
    for (float &v : h)
        v = float(v / sum);
    const size_t n = clip.frames();
    std::vector<float> out(n / size_t(factor));
    for (size_t o = 0; o < out.size(); ++o) {
        const long long c = (long long)(o * size_t(factor));
        double acc = 0;
        for (int t = 0; t < taps; ++t) {
            const long long k = c + t - (taps - 1) / 2;
            if (k >= 0 && k < (long long)n)
                acc += h[size_t(t)] * 0.5 * (clip.left[size_t(k)] + clip.right[size_t(k)]);
        }
        out[o] = float(acc);
    }
    return out;
}

// Beats are heard slightly after the onset curve rises (seconds, measured on test songs).
constexpr double kBeatLatency = 0.0;
static double kMetreBase = 0.3;
static int kHpssTime = 2, kHpssPitch = 3;
static bool kBeatMedian = true;
static double kBassRoot = 0.25, kBassFifth = 0.08;

// Onset strength per frame: rise in log energy summed over quarter-octave bands (up to maxHz), compared with
// the maximum of the neighbouring bands two frames earlier. Detrended and scaled to unit spread.
std::vector<float> onsetCurve(const std::vector<std::vector<float>> &spec, int sr, size_t n, double maxHz = 1e9)
{
    const size_t bins = n / 2;
    const double binHz = double(sr) / double(n);
    std::vector<int> band(bins, -1);
    int nb = 0;
    for (size_t b = 1; b < bins; ++b) {
        const double f = std::max(double(b) * binHz, 30.0);
        if (double(b) * binHz > maxHz)
            break;
        band[b] = int(std::floor(4.0 * std::log2(f / 30.0)));
        nb = std::max(nb, band[b] + 1);
    }
    std::vector<std::vector<float>> L(spec.size(), std::vector<float>(size_t(nb), 0.f));
    for (size_t f = 0; f < spec.size(); ++f) {
        for (size_t b = 1; b < bins; ++b)
            if (band[b] >= 0)
                L[f][size_t(band[b])] += spec[f][b];
        for (float &v : L[f])
            v = std::log1p(100.f * v);
    }
    std::vector<float> onset(spec.size(), 0.f);
    for (size_t f = 2; f < spec.size(); ++f) {
        float flux = 0.f;
        for (int b = 0; b < nb; ++b) {
            float ref = L[f - 2][size_t(b)];
            if (b > 0)
                ref = std::max(ref, L[f - 2][size_t(b - 1)]);
            if (b + 1 < nb)
                ref = std::max(ref, L[f - 2][size_t(b + 1)]);
            flux += std::max(0.f, L[f][size_t(b)] - ref);
        }
        onset[f] = flux;
    }
    // Remove the slowly varying part and normalise.
    const int w = 50;
    std::vector<float> smooth(onset.size());
    double acc = 0;
    for (size_t i = 0; i < onset.size(); ++i) {
        acc += onset[i];
        if (i >= size_t(w))
            acc -= onset[i - size_t(w)];
        smooth[i] = float(acc / double(std::min<size_t>(i + 1, size_t(w))));
    }
    double mean = 0, var = 0;
    for (size_t i = 0; i < onset.size(); ++i) {
        onset[i] = std::max(0.f, onset[i] - smooth[i]);
        mean += onset[i];
    }
    mean /= double(std::max<size_t>(1, onset.size()));
    for (float v : onset)
        var += (v - mean) * (v - mean);
    const double sd = std::sqrt(var / double(std::max<size_t>(1, onset.size()))) + 1e-9;
    for (float &v : onset)
        v = float(v / sd);
    return onset;
}

// Onsets spread over +-2 frames, to tolerate a little timing jitter.
std::vector<float> spreadOnsets(const std::vector<float> &onset)
{
    const size_t F = onset.size();
    std::vector<float> s(F, 0.f);
    for (size_t f = 0; f < F; ++f) {
        float v = onset[f];
        for (int d = 1; d <= 2; ++d) {
            const float w = d == 1 ? 0.7f : 0.35f;
            if (f >= size_t(d))
                v = std::max(v, w * onset[f - size_t(d)]);
            if (f + size_t(d) < F)
                v = std::max(v, w * onset[f + size_t(d)]);
        }
        s[f] = v;
    }
    return s;
}

// A steady beat grid: first beat t0 and period T (frames); score = average onset strength on its beats.
struct Grid
{
    double t0 = 0, T = 0, score = -1;
};

// The steady grid that best fits the onsets between frames from..to, with a period within
// lo..hi times P.
Grid combGrid(const std::vector<float> &s, double P, double lo = 0.96, double hi = 1.04, double rStep = 0.0005,
              size_t from = 0, size_t to = 0)
{
    const size_t F = to ? std::min(to, s.size()) : s.size();
    auto at = [&](double pos) {
        const size_t i = size_t(pos);
        const double fr = pos - double(i);
        return i + 1 < F ? s[i] * (1 - fr) + s[i + 1] * fr : 0.0;
    };
    Grid best;
    for (double r = lo; r <= hi + 1e-9; r += rStep) {
        const double T = P * r;
        for (double ph = double(from); ph < double(from) + T; ph += 0.5) {
            double sum = 0;
            int n = 0;
            for (double pos = ph; pos + 1 < double(F); pos += T, ++n)
                sum += at(pos);
            const double sc = n ? sum / n : 0;
            if (sc > best.score)
                best = {ph, T, sc};
        }
    }
    return best;
}

// How well beats about P apart fit the onsets when the tempo may wander a little: the average,
// over 12 s windows, of how much the best local grid (period within +-3%) stands out.
double localFit(const std::vector<float> &s, double P, double onRate)
{
    const size_t win = size_t(12 * onRate), hop = size_t(6 * onRate);
    double sum = 0;
    int n = 0;
    for (size_t a = 0; a == 0 || a + win <= s.size(); a += hop) {
        const size_t b = std::min(s.size(), a + win);
        const Grid g = combGrid(s, P, 0.97, 1.03, 0.005, a, b);
        double mean = 0;
        for (size_t i = a; i < b; ++i)
            mean += s[i];
        mean /= double(std::max<size_t>(1, b - a));
        const double beats = double(b - a) / P;
        // How far the grid stands out from the average onset level. A slow grid has fewer
        // beats, so it lands on strong onsets by chance more easily: scale by sqrt(beats).
        sum += (g.score - mean) * std::sqrt(beats / 16.0);
        ++n;
        if (a + win >= s.size())
            break;
    }
    return n ? sum / n : 0.0;
}

// Normalised autocorrelation of the onset curve at a (fractional) lag in frames.
double onsetCorrelation(const std::vector<float> &onset, double lag)
{
    const size_t l0 = size_t(lag);
    const double fr = lag - double(l0);
    double s0 = 0, s1 = 0, z = 1e-9;
    for (size_t i = 0; i < onset.size(); ++i) {
        z += double(onset[i]) * onset[i];
        if (i >= l0 + 1) {
            s0 += double(onset[i]) * onset[i - l0];
            s1 += double(onset[i]) * onset[i - l0 - 1];
        }
    }
    return (s0 * (1 - fr) + s1 * fr) / z;
}

// Beat tracking by dynamic programming (Ellis 2007): beats on strong onsets, spaced about one
// period apart but free to follow a drifting tempo. Returns frame indices.
std::vector<int> trackBeats(const std::vector<float> &onset, double period, double tight)
{
    const size_t F = onset.size();
    std::vector<double> score(F, 0.0);
    std::vector<int> back(F, -1);
    for (size_t t = 0; t < F; ++t) {
        double best = 0;
        int arg = -1;
        const int lo = int(t) - int(std::round(1.3 * period)), hi = int(t) - int(std::round(0.7 * period));
        for (int tau = std::max(0, lo); tau <= hi; ++tau) {
            const double l = std::log((double(t) - tau) / period);
            const double v = score[size_t(tau)] - tight * l * l;
            if (arg < 0 || v > best) {
                best = v;
                arg = tau;
            }
        }
        score[t] = onset[t] + (arg >= 0 ? best : 0.0);
        back[t] = arg;
    }
    if (F == 0)
        return {};
    size_t end = F - 1;
    for (size_t t = F > size_t(period) ? F - size_t(period) : 0; t < F; ++t)
        if (score[t] > score[end])
            end = t;
    std::vector<int> beats;
    for (int t = int(end); t >= 0; t = back[size_t(t)])
        beats.push_back(t);
    std::reverse(beats.begin(), beats.end());
    return beats;
}

} // namespace

bool detectChords(const AudioClip &clip, DetectedSong *out, QString *error,
                  const std::function<void(double)> &progress, const std::atomic<bool> *cancel, bool detailed)
{
    auto cancelled = [&] { return cancel && cancel->load(); };
    auto report = [&](double base, double span) {
        return [&, base, span](double f) {
            if (progress)
                progress(base + span * f);
            return !cancelled();
        };
    };
    if (clip.frames() < size_t(clip.sampleRate) * 5) {
        if (error)
            *error = QStringLiteral("The recording is too short (less than 5 seconds).");
        return false;
    }

    int sr = 0;
    const std::vector<float> x = analysisSignal(clip, &sr);
    if (progress)
        progress(0.05);

    // ---- 1. Onset strength: log-band spectral flux with a max filter ("SuperFlux", which
    // ignores vibrato), 10 ms frames. Frame f is centred at f / onRate + onShift seconds.
    const size_t onN = 512, onHop = size_t(std::max(1, sr / 100));
    const auto onSpec = stft(x, onN, onHop, report(0.05, 0.15));
    if (cancelled() || onSpec.empty())
        return false;
    const double onRate = double(sr) / double(onHop); // frames per second
    const double onShift = double(onN) / 2.0 / sr;
    std::vector<float> onset = onsetCurve(onSpec, sr, onN);
    const size_t F = onset.size();
    const bool debugOn = qEnvironmentVariableIsSet("GCP_DEBUG");
    if (qEnvironmentVariableIsSet("GCP_METRE"))
        kMetreBase = qEnvironmentVariable("GCP_METRE").toDouble();
    if (qEnvironmentVariableIsSet("GCP_BASS")) {
        const QStringList v = qEnvironmentVariable("GCP_BASS").split(QLatin1Char(','));
        kBassRoot = v.value(0).toDouble();
        kBassFifth = v.value(1).toDouble();
    }
    if (qEnvironmentVariableIsSet("GCP_MEDIAN"))
        kBeatMedian = qEnvironmentVariable("GCP_MEDIAN").toInt() != 0;
    if (qEnvironmentVariableIsSet("GCP_HPSS")) {
        const QStringList v = qEnvironmentVariable("GCP_HPSS").split(QLatin1Char(','));
        kHpssTime = v.value(0).toInt();
        kHpssPitch = v.value(1).toInt();
    }

    // ---- 2. Tempo and beat grid: candidate periods from the autocorrelation of the onset curve;
    // for each, the steady grid that fits the onsets best. The grid whose beats land on the
    // strongest onsets wins (with a mild preference for 70-160 BPM).
    const int minLag = int(onRate * 60.0 / 220.0), maxLag = int(onRate * 60.0 / 45.0);
    std::vector<double> ac(size_t(maxLag + 2), 0.0);
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag) {
        double sacc = 0;
        for (size_t i = size_t(lag); i < F; ++i)
            sacc += double(onset[i]) * onset[i - size_t(lag)];
        ac[size_t(lag)] = sacc / double(F - size_t(lag));
    }
    std::vector<std::pair<double, double>> peaks; // (strength, refined lag)
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const double a = ac[size_t(lag - 1)], b = ac[size_t(lag)], c = ac[size_t(lag + 1)];
        if (b > a && b >= c && b > 0) {
            const double d = a - 2 * b + c;
            peaks.emplace_back(b, d < 0 ? lag + 0.5 * (a - c) / d : double(lag));
        }
    }
    std::sort(peaks.begin(), peaks.end(), [](auto &p1, auto &p2) { return p1.first > p2.first; });
    if (peaks.size() > 8)
        peaks.resize(8);
    if (peaks.empty()) {
        if (error)
            *error = QStringLiteral("Could not find a steady beat in the recording.");
        return false;
    }
    const std::vector<float> spread = spreadOnsets(onset);
    Grid grid;
    double bestTempoScore = -1e30;
    // Also try double and half of the strongest period.
    {
        const double top = peaks.front().second;
        for (double m : {0.5, 2.0})
            if (60.0 * onRate / (top * m) >= 45 && 60.0 * onRate / (top * m) <= 220)
                peaks.emplace_back(0.0, top * m);
    }
    for (const auto &pk : peaks) {
        // Refine the period on the whole song first: the bar test below multiplies any error.
        const double P = combGrid(spread, pk.second, 0.97, 1.03, 0.001).T;
        const double bpm = 60.0 * onRate / P;
        if (bpm < 45 || bpm > 220)
            continue;
        const double fit = localFit(spread, P, onRate);
        const double wgt = std::exp(-0.5 * std::pow(std::log2(bpm / 110.0) / 0.8, 2));
        // Rhythm patterns repeat every bar: a real beat period shows strong repetition 4 and 8
        // beats later (or 3 and 6 in 3/4). Three eighth notes, say, would fit a steady strum
        // just as well as the beat does, but its multiples don't line up with the bars.
        const double metre = std::max(0.0, std::max(onsetCorrelation(onset, 4 * P) + onsetCorrelation(onset, 8 * P),
                                                    onsetCorrelation(onset, 3 * P) + onsetCorrelation(onset, 6 * P)));
        const double sc = fit * wgt * (kMetreBase + metre);
        if (debugOn)
            fprintf(stderr, "tempo candidate %.2f bpm: local fit %.3f, bar repetition %.3f, score %.3f\n", bpm, fit, metre, sc);
        if (sc > bestTempoScore) {
            bestTempoScore = sc;
            grid.T = P;
        }
    }
    if (grid.T <= 0) {
        if (error)
            *error = QStringLiteral("Could not find a steady beat in the recording.");
        return false;
    }
    grid = combGrid(spread, grid.T);
    if (progress)
        progress(0.25);
    // Let the grid follow a band that drifts; keep it steady when the drift is negligible.
    double T = grid.T / onRate;
    double t0 = grid.t0 / onRate + onShift + kBeatLatency;
    while (t0 - T >= 0)
        t0 -= T;
    while (t0 < 0)
        t0 += T;
    const double duration = clip.seconds();
    std::vector<double> beatT; // beat k spans beatT[k] .. beatT[k+1]
    for (double t = t0; t < duration; t += T)
        beatT.push_back(t);
    // Does the steady grid hold everywhere? In windows where a clearly better local grid sits
    // somewhere else, the band has drifted: then follow the tracked beats instead.
    bool steady = true;
    {
        const size_t win = size_t(12 * onRate), hop = size_t(6 * onRate);
        int windows = 0, drifted = 0;
        for (size_t a = 0; a + win <= spread.size(); a += hop) {
            const Grid local = combGrid(spread, grid.T, 0.97, 1.03, 0.005, a, a + win);
            // The steady grid's own fit in this window.
            double sum = 0;
            int n = 0;
            const double first = grid.t0 + std::ceil((double(a) - grid.t0) / grid.T) * grid.T;
            for (double pos = first; pos + 1 < double(a + win); pos += grid.T, ++n) {
                const size_t i = size_t(pos);
                const double fr = pos - double(i);
                sum += spread[i] * (1 - fr) + spread[i + 1] * fr;
            }
            const double own = n ? sum / n : 0;
            // Phase difference between the two grids near the window centre (frames).
            const double c = double(a + win / 2);
            const double pl = local.t0 + std::round((c - local.t0) / local.T) * local.T;
            const double pg = grid.t0 + std::round((pl - grid.t0) / grid.T) * grid.T;
            ++windows;
            // (A grid half a beat away is the off-beat, not drift.)
            const double off = std::abs(pl - pg) / grid.T;
            if (std::abs(pl - pg) > 0.04 * onRate && (off < 0.35 || off > 0.65) && local.score > 1.2 * own + 0.1)
                ++drifted;
        }
        steady = drifted <= std::max(1, windows / 8);
        if (debugOn)
            fprintf(stderr, "grid %.2f bpm; windows off the grid: %d of %d -> %s\n", 60.0 / T, drifted, windows,
                    steady ? "steady" : "tempo map");
    }
    if (!steady) {
        beatT.clear();
        for (int f : trackBeats(onset, grid.T, 400.0))
            beatT.push_back(double(f) / onRate + onShift + kBeatLatency);
        while (beatT.size() >= 2 && beatT.back() + (beatT.back() - beatT[beatT.size() - 2]) < duration)
            beatT.push_back(2 * beatT.back() - beatT[beatT.size() - 2]);
    }
    if (beatT.size() < 9) {
        if (error)
            *error = QStringLiteral("Could not find a steady beat in the recording.");
        return false;
    }
    const int nBeats = int(beatT.size()) - 1;
    // Time of beat index k, extrapolated beyond the ends.
    auto beatTime = [&](double k) {
        const int n = int(beatT.size());
        if (k <= 0)
            return beatT[0] + k * (beatT[1] - beatT[0]);
        if (k >= n - 1)
            return beatT[size_t(n - 1)] + (k - (n - 1)) * (beatT[size_t(n - 1)] - beatT[size_t(n - 2)]);
        const int i = int(k);
        return beatT[size_t(i)] + (k - i) * (beatT[size_t(i + 1)] - beatT[size_t(i)]);
    };
    if (progress)
        progress(0.3);

    // ---- 5. Semitone spectrum with tuning correction
    const size_t chN = 8192, chHop = 512;
    const auto spec = stft(x, chN, chHop, report(0.3, 0.45));
    if (cancelled() || spec.empty())
        return false;
    const double binHz = double(sr) / double(chN);
    // Tuning: which offset (in cents) puts the most energy on semitone centres?
    std::vector<double> avg(chN / 2, 0.0);
    for (const auto &fr : spec)
        for (size_t b = 0; b < chN / 2; ++b)
            avg[b] += fr[b];
    double bestCents = 0, bestE = -1;
    for (int cents = -50; cents < 50; cents += 5) {
        double e = 0;
        for (int p = 40; p < 90; ++p) {
            const double f = 440.0 * std::pow(2.0, (p - 69 + cents / 100.0) / 12.0);
            const double b = f / binHz;
            const size_t i = size_t(b);
            if (i + 1 >= avg.size())
                break;
            e += avg[i] + (avg[i + 1] - avg[i]) * (b - double(i));
        }
        if (e > bestE) {
            bestE = e;
            bestCents = cents;
        }
    }
    // Map each FFT bin to a semitone (MIDI 28..95) with the tuning applied.
    const int pLo = 28, pHi = 96;
    std::vector<int> binPitch(chN / 2, -1);
    for (size_t b = 1; b < chN / 2; ++b) {
        const double f = double(b) * binHz;
        const double p = 69 + 12 * std::log2(f / 440.0) - bestCents / 100.0;
        const int pi = int(std::lround(p));
        if (pi >= pLo && pi < pHi && std::abs(p - pi) < 0.4)
            binPitch[b] = pi;
    }
    // Per frame: a semitone spectrum (one value per note), whitened across pitch so only
    // peaks count, then harmonic summation so each note's overtones support its fundamental.
    // Treble chroma comes from the middle register, bass chroma from the low one.
    const size_t CF = spec.size();
    const int NP = pHi - pLo;
    std::vector<std::array<float, 12>> chroma(CF), bass(CF);
    std::vector<float> energy(CF, 0.f);
    // Semitone spectrogram (the loudest bin of each note).
    std::vector<std::vector<float>> semiSpec(CF, std::vector<float>(static_cast<size_t>(NP), 0.f));
    for (size_t f = 0; f < CF; ++f) {
        float e = 0.f;
        for (size_t b = 1; b < chN / 2; ++b) {
            const int p = binPitch[b];
            if (p < 0)
                continue;
            float &cell = semiSpec[f][size_t(p - pLo)];
            cell = std::max(cell, spec[f][b]);
            e += spec[f][b];
        }
        energy[f] = e;
    }
    // Harmonic/percussive separation (median filtering): notes are steady in time, drum hits
    // are short and spread over all pitches. Keep the part that is steady.
    if (kHpssTime > 0) {
        std::vector<std::vector<float>> harm(CF, std::vector<float>(static_cast<size_t>(NP), 0.f));
        std::vector<float> buf;
        for (int p = 0; p < NP; ++p)
            for (size_t f = 0; f < CF; ++f) {
                buf.clear();
                for (long long g = (long long)f - kHpssTime; g <= (long long)f + kHpssTime; ++g)
                    if (g >= 0 && g < (long long)CF)
                        buf.push_back(semiSpec[size_t(g)][size_t(p)]);
                std::nth_element(buf.begin(), buf.begin() + long(buf.size() / 2), buf.end());
                harm[f][size_t(p)] = buf[buf.size() / 2];
            }
        for (size_t f = 0; f < CF; ++f) {
            std::vector<float> &row = semiSpec[f];
            std::vector<float> perc(static_cast<size_t>(NP));
            for (int p = 0; p < NP; ++p) {
                buf.clear();
                for (int q = std::max(0, p - kHpssPitch); q <= std::min(NP - 1, p + kHpssPitch); ++q)
                    buf.push_back(row[size_t(q)]);
                std::nth_element(buf.begin(), buf.begin() + long(buf.size() / 2), buf.end());
                perc[size_t(p)] = buf[buf.size() / 2];
            }
            for (int p = 0; p < NP; ++p) {
                const float h = harm[f][size_t(p)], pc = perc[size_t(p)];
                row[size_t(p)] *= h * h / (h * h + pc * pc + 1e-12f);
            }
        }
    }
    std::vector<float> semi(static_cast<size_t>(NP)), white(static_cast<size_t>(NP)), sal(static_cast<size_t>(NP));
    for (size_t f = 0; f < CF; ++f) {
        semi = semiSpec[f];
        for (float &v : semi)
            v = std::log1p(100.f * v);
        for (int p = 0; p < NP; ++p) {
            float acc = 0.f;
            int n = 0;
            for (int q = std::max(0, p - 6); q <= std::min(NP - 1, p + 6); ++q, ++n)
                acc += semi[size_t(q)];
            white[size_t(p)] = std::max(0.f, semi[size_t(p)] - acc / float(n));
        }
        static const int harm[5] = {0, 12, 19, 24, 28};
        static const float hw[5] = {1.f, 0.6f, 0.45f, 0.35f, 0.3f};
        for (int p = 0; p < NP; ++p) {
            float v = 0.f;
            for (int h = 0; h < 5; ++h)
                if (p + harm[h] < NP)
                    v += hw[h] * white[size_t(p + harm[h])];
            // Only keep notes whose fundamental itself is present.
            sal[size_t(p)] = white[size_t(p)] > 0.f ? v : 0.f;
        }
        std::array<float, 12> c{}, bs{};
        for (int p = 0; p < NP; ++p) {
            const int midi = p + pLo;
            if (midi >= 48 && midi <= 84)
                c[size_t(midi % 12)] += sal[size_t(p)];
            else if (midi < 55)
                bs[size_t(midi % 12)] += sal[size_t(p)];
        }
        chroma[f] = c;
        bass[f] = bs;
    }
    const double chRate = double(sr) / double(chHop);
    if (progress)
        progress(0.78);

    // ---- 6. Beat-synchronous chroma and chord scores
    std::vector<std::array<float, 12>> beatChroma(static_cast<size_t>(std::max(0, nBeats))), beatBass(static_cast<size_t>(std::max(0, nBeats)));
    std::vector<float> beatEnergy(size_t(std::max(0, nBeats)), 0.f);
    for (int k = 0; k < nBeats; ++k) {
        // The analysis window is centred on the frame, so shift by half a window.
        const double ta = beatT[size_t(k)], tb = beatT[size_t(k + 1)];
        const long long fa = (long long)std::floor(ta * chRate - chN / 2.0 / chHop);
        const long long fb = (long long)std::ceil(tb * chRate - chN / 2.0 / chHop);
        std::array<float, 12> c{}, bs{};
        float e = 0;
        int count = 0;
        for (long long f = std::max(0LL, fa); f <= fb && f < (long long)CF; ++f, ++count) {
            for (size_t i = 0; i < 12; ++i) {
                c[i] += chroma[size_t(f)][i];
                bs[i] += bass[size_t(f)][i];
            }
            e += energy[size_t(f)];
        }
        if (kBeatMedian && count >= 3) {
            // Median of the frames: a passing melody note doesn't count as much.
            std::vector<float> v;
            for (size_t i = 0; i < 12; ++i) {
                for (int pass = 0; pass < 2; ++pass) {
                    v.clear();
                    for (long long f = std::max(0LL, fa); f <= fb && f < (long long)CF; ++f)
                        v.push_back(pass ? bass[size_t(f)][i] : chroma[size_t(f)][i]);
                    std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
                    (pass ? bs : c)[i] = v[v.size() / 2] * float(count);
                }
            }
        }
        beatChroma[size_t(k)] = c;
        beatBass[size_t(k)] = bs;
        beatEnergy[size_t(k)] = count ? e / float(count) : 0.f;
    }
    std::vector<float> sortedE = beatEnergy;
    std::sort(sortedE.begin(), sortedE.end());
    const float medianE = sortedE.empty() ? 0.f : sortedE[sortedE.size() / 2];

    // States: 12 roots x the chord types below, plus no-chord (last).
    // Extended types start with a small handicap so a plain triad wins unless the extra
    // note is clearly there.
    struct Quality { const char *name; int tones[4]; int count; double prior; };
    // Tuned on test mixes (see the README): 7ths are often a single quiet string in a guitar voicing.
    constexpr double kSeventhWeight = 0.7;
    static const Quality allQualities[] = {
        {"", {0, 4, 7, 0}, 3, 0.0},       {"m", {0, 3, 7, 0}, 3, 0.0},
        {"7", {0, 4, 7, 10}, 4, 0.03},    {"m7", {0, 3, 7, 10}, 4, 0.05},
        {"maj7", {0, 4, 7, 11}, 4, 0.08}, {"sus4", {0, 5, 7, 0}, 3, 0.13},
        {"sus2", {0, 2, 7, 0}, 3, 0.14},  {"dim", {0, 3, 6, 0}, 3, 0.10},
        {"aug", {0, 4, 8, 0}, 3, 0.15},
    };
    const int NQ = detailed ? int(std::size(allQualities)) : 2;
    // Priors and the weight of a 7th in the templates (tunable for testing via GCP_PRIORS).
    double prior[9], seventhWeight = kSeventhWeight;
    for (int q = 0; q < 9; ++q)
        prior[q] = allQualities[q].prior;
    if (qEnvironmentVariableIsSet("GCP_PRIORS")) {
        const QStringList v = qEnvironmentVariable("GCP_PRIORS").split(QLatin1Char(','));
        for (int q = 2; q < 9 && q - 2 < v.size(); ++q)
            prior[q] = v[q - 2].toDouble();
        if (v.size() > 7)
            seventhWeight = v[7].toDouble();
    }
    const int S = 12 * NQ + 1;
    const int NC = S - 1;
    std::vector<std::vector<double>> emission(static_cast<size_t>(nBeats), std::vector<double>(static_cast<size_t>(S)));
    std::vector<double> bestFit(static_cast<size_t>(nBeats), 0.0);
    // Zero-mean, unit-length templates.
    std::vector<std::array<double, 12>> templ(static_cast<size_t>(12 * NQ));
    for (int q = 0; q < NQ; ++q) {
        for (int root = 0; root < 12; ++root) {
            std::array<double, 12> t{};
            const int k = allQualities[q].count;
            for (int i = 0; i < 12; ++i)
                t[size_t(i)] = -double(k) / (12 - k);
            for (int j = 0; j < k; ++j)
                t[size_t((root + allQualities[q].tones[j]) % 12)] = j == 3 ? seventhWeight : 1.0;
            double n = 0;
            for (double v : t)
                n += v * v;
            n = std::sqrt(n);
            for (double &v : t)
                v /= n;
            templ[size_t(q * 12 + root)] = t;
        }
    }
    for (int k = 0; k < nBeats; ++k) {
        std::array<float, 12> c = beatChroma[size_t(k)], bs = beatBass[size_t(k)];
        // Normalise and remove the average level so only the pattern counts.
        auto normalise = [](std::array<float, 12> &v) {
            float mean = 0;
            for (float x : v)
                mean += x;
            mean /= 12.f;
            float n = 0;
            for (float &x : v) {
                x -= mean;
                n += x * x;
            }
            n = std::sqrt(n) + 1e-9f;
            for (float &x : v)
                x /= n;
        };
        normalise(c);
        normalise(bs);
        const bool silent = beatEnergy[size_t(k)] < 0.08f * medianE;
        for (int st = 0; st < NC; ++st) {
            const int q = st / 12, root = st % 12;
            const auto &t = templ[size_t(st)];
            double dot = 0;
            for (int i = 0; i < 12; ++i)
                dot += c[size_t(i)] * t[size_t(i)];
            const int fifth = (root + allQualities[q].tones[2]) % 12;
            const double bassBonus = kBassRoot * bs[size_t(root)] + kBassFifth * bs[size_t(fifth)];
            emission[size_t(k)][size_t(st)] = 12.0 * (dot + bassBonus - prior[q]);
            bestFit[size_t(k)] = std::max(bestFit[size_t(k)], dot);
        }
        emission[size_t(k)][size_t(NC)] = silent ? 8.0 : -6.0;
    }

    // ---- 7. Viterbi smoothing: chords usually last several beats. changeCost[k] = cost of a new
    // chord starting on beat k.
    auto stateName = [&](int st) {
        return st == NC ? QStringLiteral("N.C.")
                        : QString::fromLatin1(kNames[st % 12]) + QString::fromLatin1(allQualities[st / 12].name);
    };
    auto viterbi = [&](const std::vector<double> &changeCost) {
        std::vector<int> path(static_cast<size_t>(nBeats), NC);
        if (nBeats <= 0)
            return path;
        std::vector<std::vector<int>> from(static_cast<size_t>(nBeats), std::vector<int>(static_cast<size_t>(S)));
        std::vector<double> dp(static_cast<size_t>(S)), nd(static_cast<size_t>(S));
        for (int st = 0; st < S; ++st)
            dp[size_t(st)] = emission[0][size_t(st)];
        for (int k = 1; k < nBeats; ++k) {
            const int bestPrev = int(std::max_element(dp.begin(), dp.end()) - dp.begin());
            for (int st = 0; st < S; ++st) {
                const double viaStay = dp[size_t(st)];
                const double viaChange = dp[size_t(bestPrev)] - changeCost[size_t(k)];
                const bool stay = viaStay >= viaChange || bestPrev == st;
                nd[size_t(st)] = (stay ? viaStay : viaChange) + emission[size_t(k)][size_t(st)];
                from[size_t(k)][size_t(st)] = stay ? st : bestPrev;
            }
            dp.swap(nd);
        }
        int st = int(std::max_element(dp.begin(), dp.end()) - dp.begin());
        for (int k = nBeats - 1; k >= 0; --k) {
            path[size_t(k)] = st;
            st = from[size_t(k)][size_t(st)];
        }
        return path;
    };
    const double kChange = 6.2; // about log(0.18 / (S - 1) / 0.82) with the old fixed transition matrix
    std::vector<int> path = viterbi(std::vector<double>(static_cast<size_t>(nBeats), kChange));
    const bool debug = debugOn;
    if (progress)
        progress(0.86);

    // ---- 8. Meter and bar lines: chords change on bar lines far more often than elsewhere
    std::vector<int> changes;
    for (int k = 1; k < nBeats; ++k)
        if (path[size_t(k)] != path[size_t(k - 1)])
            changes.push_back(k);
    int bestBpb = 4, bestPhase = 0;
    double bestFitScore = -1;
    for (int bpb : {4, 3}) {
        for (int ph = 0; ph < bpb; ++ph) {
            double sc = 0;
            for (int c : changes) {
                const int m = ((c - ph) % bpb + bpb) % bpb;
                sc += m == 0 ? 1.0 : (bpb == 4 && m == 2) ? 0.4 : 0.0;
            }
            // Accented beats (onset strength) also mark the downbeat a little.
            double acc = 0;
            int cnt = 0;
            for (int k = ph; k < nBeats; k += bpb) {
                const size_t f = size_t(std::max(0L, std::lround((beatT[size_t(k)] - onShift - kBeatLatency) * onRate)));
                if (f < onset.size()) {
                    acc += onset[f];
                    ++cnt;
                }
            }
            sc = sc / std::max<size_t>(1, changes.size()) + 0.05 * (cnt ? acc / cnt : 0);
            if (bpb == 3)
                sc -= 0.1; // prefer 4/4 unless 3/4 is clearly better
            if (sc > bestFitScore) {
                bestFitScore = sc;
                bestBpb = bpb;
                bestPhase = ph;
            }
        }
    }

    // ---- 9. Decode again, now that the bar lines are known: a new chord almost always starts
    // on a bar line or half way through a bar, very rarely on another beat.
    double costBar = 2.0, costHalf = 4.0, costBeat = 30.0;
    if (qEnvironmentVariableIsSet("GCP_DECODE")) {
        const QStringList v = qEnvironmentVariable("GCP_DECODE").split(QLatin1Char(','));
        if (v.size() >= 3) {
            costBar = v[0].toDouble();
            costHalf = v[1].toDouble();
            costBeat = v[2].toDouble();
        }
    }
    {
        std::vector<double> cost(static_cast<size_t>(nBeats));
        for (int k = 0; k < nBeats; ++k) {
            const int m = ((k - bestPhase) % bestBpb + bestBpb) % bestBpb;
            cost[size_t(k)] = m == 0 ? costBar : (bestBpb == 4 && m == 2) ? costHalf : costBeat;
        }
        path = viterbi(cost);
    }
    std::vector<QString> labels(static_cast<size_t>(nBeats));
    for (int k = 0; k < nBeats; ++k)
        labels[size_t(k)] = stateName(path[size_t(k)]);
    if (debug) {
        fprintf(stderr, "tuning %.0f cents, beats %d, %d/4 from beat %d\n", bestCents, nBeats, bestBpb, bestPhase);
        for (int k = 0; k < std::min(nBeats, 24); ++k)
            fprintf(stderr, "beat %2d -> %s\n", k, qPrintable(labels[size_t(k)]));
    }
    if (progress)
        progress(0.9);

    // ---- 10. Bars: the chords of each bar with their lengths in beats.
    const int firstBeat = bestPhase; // first downbeat index at or after t0
    // Include a partial bar at the start if the music begins before the first downbeat.
    const int startBeat = firstBeat >= 2 ? firstBeat - bestBpb : firstBeat;
    struct Slot { int state; int beats; };
    std::vector<std::vector<Slot>> barSlots;
    for (int k = startBeat; k < nBeats; k += bestBpb) {
        std::vector<Slot> cells;
        for (int i = k; i < k + bestBpb; ++i) {
            const int st = (i >= 0 && i < nBeats) ? path[size_t(i)] : NC;
            if (!cells.empty() && cells.back().state == st)
                ++cells.back().beats;
            else
                cells.push_back({st, 1});
        }
        barSlots.push_back(cells);
    }
    // Drop silent bars at the very end.
    while (!barSlots.empty() && barSlots.back().size() == 1 && barSlots.back()[0].state == NC)
        barSlots.pop_back();
    if (barSlots.empty()) {
        if (error)
            *error = QStringLiteral("No chords found in the recording.");
        return false;
    }

    // ---- 11. Key (most common chord, by time) and an easy capo position
    std::map<int, int> count;
    for (const auto &b : barSlots)
        for (const Slot &sl : b)
            if (sl.state != NC)
                count[sl.state] += sl.beats;
    out->key = count.empty() ? QString()
                             : stateName(std::max_element(count.begin(), count.end(),
                                                          [](auto &x, auto &y) { return x.second < y.second; })->first);
    // Capo: where the shapes are easiest to play.
    auto shapeName = [&](int st, int capo) {
        return QString::fromLatin1(kNames[((st % 12) - capo + 12) % 12]) + QString::fromLatin1(allQualities[st / 12].name);
    };
    int bestCapo = 0;
    double bestCost = 1e30;
    for (int capo = 0; capo <= 5; ++capo) {
        double cost = capo * 0.15;
        for (const auto &b : barSlots)
            for (const Slot &sl : b) {
                if (sl.state == NC)
                    continue;
                const auto shape = ChordLibrary::lookup(shapeName(sl.state, capo));
                cost += (shape ? Arranger::difficulty(*shape) : 20.0) * sl.beats / bestBpb;
            }
        if (cost < bestCost) {
            bestCost = cost;
            bestCapo = capo;
        }
    }
    // Written out: "C" for a whole bar, "C G" for two halves, "C:3 G:1" otherwise.
    QVector<QStringList> bars;
    for (const auto &cells : barSlots) {
        QStringList bar;
        const bool even = cells.size() == 1 || (cells.size() == 2 && cells[0].beats == cells[1].beats)
                          || int(cells.size()) == bestBpb;
        for (const Slot &sl : cells) {
            const QString name = sl.state == NC ? QStringLiteral("N.C.") : shapeName(sl.state, bestCapo);
            bar << (even ? name : name + QLatin1Char(':') + QString::number(sl.beats));
        }
        bars.push_back(bar);
    }

    double fitSum = 0;
    for (double v : bestFit)
        fitSum += v;
    out->confidence = nBeats ? std::clamp(fitSum / nBeats, 0.0, 1.0) : 0.0;
    {
        // Tempo = median beat length; the beat map is kept when the tempo moves.
        std::vector<double> ioi;
        for (size_t k = 1; k < beatT.size(); ++k)
            ioi.push_back(beatT[k] - beatT[k - 1]);
        std::nth_element(ioi.begin(), ioi.begin() + long(ioi.size() / 2), ioi.end());
        out->bpm = steady ? 60.0 / T : 60.0 / ioi[ioi.size() / 2];
    }
    out->beats.clear();
    if (!steady)
        for (int k = startBeat; k <= nBeats; ++k)
            out->beats.push_back(beatTime(k));
    out->beatsPerBar = bestBpb;
    out->offset = beatTime(startBeat);
    out->capo = bestCapo;
    out->bars = bars;
    if (progress)
        progress(1.0);
    return true;
}

QString detectedSongToXml(const DetectedSong &song, const QString &title, const QString &artist,
                          const QString &audioFileName)
{
    const double bpm = std::round(song.bpm * 100.0) / 100.0;
    QString pattern = song.beatsPerBar == 3 ? QStringLiteral("waltz")
                    : bpm < 85 ? QStringLiteral("ballad")
                    : bpm < 125 ? QStringLiteral("folk") : QStringLiteral("drive");

    QString xml;
    QXmlStreamWriter w(&xml);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(2);
    w.writeStartDocument();
    w.writeComment(QStringLiteral(" Chords detected automatically from the recording by Guitar Chord Player. "
                                  "Some may be wrong: fix them by ear in the editor (Ctrl+E, Live). "));
    w.writeStartElement(QStringLiteral("song"));
    w.writeAttribute(QStringLiteral("title"), title);
    if (!artist.isEmpty())
        w.writeAttribute(QStringLiteral("artist"), artist);
    w.writeAttribute(QStringLiteral("bpm"), QString::number(bpm, 'f', 2));
    w.writeAttribute(QStringLiteral("beatsPerBar"), QString::number(song.beatsPerBar));
    if (song.capo > 0)
        w.writeAttribute(QStringLiteral("capo"), QString::number(song.capo));
    w.writeAttribute(QStringLiteral("pattern"), pattern);

    if (song.beats.isEmpty()) {
        w.writeEmptyElement(QStringLiteral("audio"));
        w.writeAttribute(QStringLiteral("file"), audioFileName);
        w.writeAttribute(QStringLiteral("offset"), QString::number(song.offset, 'f', 3));
    } else {
        // The tempo moves: the time of every beat, one bar per line, so the guitar follows it.
        w.writeStartElement(QStringLiteral("audio"));
        w.writeAttribute(QStringLiteral("file"), audioFileName);
        w.writeAttribute(QStringLiteral("offset"), QString::number(song.offset, 'f', 3));
        QString text;
        for (int i = 0; i < song.beats.size(); ++i) {
            if (i % song.beatsPerBar == 0)
                text += QStringLiteral("\n      ");
            else
                text += QLatin1Char(' ');
            text += QString::number(song.beats[i], 'f', 3);
        }
        w.writeTextElement(QStringLiteral("beats"), text + QStringLiteral("\n    "));
        w.writeEndElement();
    }

    w.writeStartElement(QStringLiteral("sections"));
    const int partBars = 8;
    for (int p = 0; p * partBars < song.bars.size(); ++p) {
        w.writeStartElement(QStringLiteral("section"));
        w.writeAttribute(QStringLiteral("name"), QStringLiteral("Part %1").arg(p + 1));
        for (int line = 0; line < partBars; line += 4) {
            QStringList bars;
            for (int b = p * partBars + line; b < std::min<int>(p * partBars + line + 4, song.bars.size()); ++b)
                bars << song.bars[b].join(QLatin1Char(' '));
            if (!bars.isEmpty())
                w.writeTextElement(QStringLiteral("bars"), bars.join(QStringLiteral(" | ")));
        }
        w.writeEndElement();
    }
    w.writeEndElement(); // sections
    w.writeEndElement(); // song
    w.writeEndDocument();
    return xml;
}
