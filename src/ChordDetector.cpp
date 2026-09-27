#include "ChordDetector.h"

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

struct ChordLabel
{
    int root = -1;      // 0..11, -1 = no chord
    bool minor = false;
    QString name() const
    {
        if (root < 0)
            return QStringLiteral("N.C.");
        return QString::fromLatin1(kNames[root]) + (minor ? QStringLiteral("m") : QString());
    }
};

QString transposeName(const QString &chord, int semis)
{
    if (chord == QLatin1String("N.C."))
        return chord;
    for (int r = 0; r < 12; ++r) {
        const QString n = QString::fromLatin1(kNames[r]);
        if (chord.startsWith(n) && (chord.size() == n.size() || chord.mid(n.size()) == QLatin1String("m"))) {
            if (n.size() == 1 && chord.size() > 1 && (chord[1] == QLatin1Char('#') || chord[1] == QLatin1Char('b')))
                continue;
            return QString::fromLatin1(kNames[((r + semis) % 12 + 12) % 12]) + chord.mid(n.size());
        }
    }
    return chord;
}

int shapeCost(const QString &chord)
{
    static const QStringList easy = {QStringLiteral("C"), QStringLiteral("D"), QStringLiteral("E"),
                                     QStringLiteral("G"), QStringLiteral("A"), QStringLiteral("Am"),
                                     QStringLiteral("Em"), QStringLiteral("Dm"), QStringLiteral("N.C.")};
    static const QStringList medium = {QStringLiteral("F"), QStringLiteral("Bm")};
    return easy.contains(chord) ? 0 : medium.contains(chord) ? 1 : 2;
}

} // namespace

bool detectChords(const AudioClip &clip, DetectedSong *out, QString *error,
                  const std::function<void(double)> &progress, const std::atomic<bool> *cancel)
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

    // ---- 1. Onset strength (spectral flux) at ~10 ms resolution
    const size_t onN = 1024, onHop = size_t(std::max(1, sr / 100));
    const auto onSpec = stft(x, onN, onHop, report(0.05, 0.15));
    if (cancelled() || onSpec.empty())
        return false;
    const double onRate = double(sr) / double(onHop); // frames per second
    std::vector<float> onset(onSpec.size(), 0.f);
    std::vector<float> prev(onN / 2, 0.f);
    for (size_t f = 0; f < onSpec.size(); ++f) {
        float flux = 0.f;
        for (size_t b = 1; b < onN / 2; ++b) {
            const float v = std::log1p(1000.f * onSpec[f][b]);
            flux += std::max(0.f, v - prev[b]);
            prev[b] = v;
        }
        onset[f] = flux;
    }
    {
        // Remove the slowly varying part and normalise.
        const int w = int(onRate * 0.5);
        std::vector<float> smooth(onset.size());
        double acc = 0;
        for (size_t i = 0; i < onset.size(); ++i) {
            acc += onset[i];
            if (i >= size_t(w))
                acc -= onset[i - size_t(w)];
            smooth[i] = float(acc / std::min<size_t>(i + 1, size_t(w)));
        }
        double mean = 0, var = 0;
        for (size_t i = 0; i < onset.size(); ++i) {
            onset[i] = std::max(0.f, onset[i] - smooth[i]);
            mean += onset[i];
        }
        mean /= double(onset.size());
        for (float v : onset)
            var += (v - mean) * (v - mean);
        const double sd = std::sqrt(var / double(onset.size())) + 1e-9;
        for (float &v : onset)
            v = float(v / sd);
    }

    // ---- 2. Tempo from the autocorrelation of the onset curve (weighted towards ~110 BPM)
    const int minLag = int(onRate * 60.0 / 200.0), maxLag = int(onRate * 60.0 / 50.0);
    std::vector<double> ac(size_t(maxLag + 2), 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag) {
        double s = 0;
        for (size_t i = size_t(lag); i < onset.size(); ++i)
            s += double(onset[i]) * onset[i - size_t(lag)];
        ac[size_t(lag)] = s / double(onset.size() - size_t(lag));
    }
    int bestLag = minLag;
    double bestScore = -1;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const double bpm = 60.0 * onRate / lag;
        const double wgt = std::exp(-0.5 * std::pow(std::log2(bpm / 110.0) / 0.9, 2));
        const double s = ac[size_t(lag)] * wgt;
        if (s > bestScore) {
            bestScore = s;
            bestLag = lag;
        }
    }
    double period = bestLag;
    if (bestLag > minLag && bestLag < maxLag) {
        const double a = ac[size_t(bestLag - 1)], b = ac[size_t(bestLag)], c = ac[size_t(bestLag + 1)];
        const double d = a - 2 * b + c;
        if (d < 0)
            period += 0.5 * (a - c) / d;
    }
    if (progress)
        progress(0.25);

    // ---- 3. Beat tracking by dynamic programming (Ellis 2007)
    const size_t F = onset.size();
    std::vector<double> score(F, 0.0);
    std::vector<int> back(F, -1);
    const double tight = 100.0;
    for (size_t t = 0; t < F; ++t) {
        double best = 0;
        int arg = -1;
        const int lo = int(t) - int(std::round(2 * period)), hi = int(t) - int(std::round(period / 2));
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
    size_t end = F - 1;
    for (size_t t = F > size_t(period) ? F - size_t(period) : 0; t < F; ++t)
        if (score[t] > score[end])
            end = t;
    std::vector<double> beats;
    for (int t = int(end); t >= 0; t = back[size_t(t)])
        beats.push_back(double(t) / onRate);
    std::reverse(beats.begin(), beats.end());
    if (beats.size() < 8) {
        if (error)
            *error = QStringLiteral("Could not find a steady beat in the recording.");
        return false;
    }

    // ---- 4. Fit one steady grid to the beats (robust linear fit, two passes)
    double t0 = 0, T = period / onRate;
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<std::pair<double, double>> pts; // (index, time)
        for (size_t k = 0; k < beats.size(); ++k) {
            if (pass == 1) {
                const double idx = std::round((beats[k] - t0) / T);
                const double pred = t0 + idx * T;
                if (std::abs(beats[k] - pred) < 0.07)
                    pts.emplace_back(idx, beats[k]);
            } else {
                pts.emplace_back(double(k), beats[k]);
            }
        }
        if (pts.size() < 8)
            break;
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (auto &p : pts) {
            sx += p.first;
            sy += p.second;
            sxx += p.first * p.first;
            sxy += p.first * p.second;
        }
        const double n = double(pts.size());
        const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        t0 = (sy - slope * sx) / n;
        T = slope;
    }
    // Put t0 at the earliest beat at or after time zero.
    while (t0 - T >= 0)
        t0 -= T;
    while (t0 < 0)
        t0 += T;
    const double duration = clip.seconds();
    const int nBeats = int((duration - t0) / T);
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
    std::vector<float> semi(static_cast<size_t>(NP)), white(static_cast<size_t>(NP)), sal(static_cast<size_t>(NP));
    for (size_t f = 0; f < CF; ++f) {
        std::fill(semi.begin(), semi.end(), 0.f);
        float e = 0.f;
        for (size_t b = 1; b < chN / 2; ++b) {
            const int p = binPitch[b];
            if (p < 0)
                continue;
            float &cell = semi[size_t(p - pLo)];
            cell = std::max(cell, spec[f][b]);
            e += spec[f][b];
        }
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
        energy[f] = e;
    }
    const double chRate = double(sr) / double(chHop);
    if (progress)
        progress(0.78);

    // ---- 6. Beat-synchronous chroma and chord scores
    std::vector<std::array<float, 12>> beatChroma(static_cast<size_t>(std::max(0, nBeats))), beatBass(static_cast<size_t>(std::max(0, nBeats)));
    std::vector<float> beatEnergy(size_t(std::max(0, nBeats)), 0.f);
    for (int k = 0; k < nBeats; ++k) {
        // The analysis window is centred on the frame, so shift by half a window.
        const double ta = t0 + k * T, tb = ta + T;
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
        beatChroma[size_t(k)] = c;
        beatBass[size_t(k)] = bs;
        beatEnergy[size_t(k)] = count ? e / float(count) : 0.f;
    }
    std::vector<float> sortedE = beatEnergy;
    std::sort(sortedE.begin(), sortedE.end());
    const float medianE = sortedE.empty() ? 0.f : sortedE[sortedE.size() / 2];

    // 25 states: 12 major, 12 minor, no-chord.
    const int S = 25;
    std::vector<std::array<double, 25>> emission(static_cast<size_t>(nBeats));
    std::vector<double> bestFit(size_t(nBeats), 0.0);
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
        for (int s = 0; s < 24; ++s) {
            const int root = s % 12;
            const bool minor = s >= 12;
            const int third = (root + (minor ? 3 : 4)) % 12, fifth = (root + 7) % 12;
            // Zero-mean template: +1 on chord tones, -3/9 elsewhere (unit length).
            double dot = 0;
            for (int i = 0; i < 12; ++i) {
                const bool tone = i == root || i == third || i == fifth;
                dot += c[size_t(i)] * (tone ? 1.0 : -1.0 / 3.0);
            }
            dot /= std::sqrt(3.0 + 9.0 / 9.0);
            const double bassBonus = 0.25 * bs[size_t(root)] + 0.08 * bs[size_t(fifth)];
            const double v = dot + bassBonus;
            emission[size_t(k)][size_t(s)] = 12.0 * v;
            bestFit[size_t(k)] = std::max(bestFit[size_t(k)], dot);
        }
        emission[size_t(k)][24] = silent ? 8.0 : -6.0;
    }

    // ---- 7. Viterbi smoothing: chords usually last several beats
    const double stay = std::log(0.82), change = std::log(0.18 / (S - 1));
    std::vector<std::array<double, 25>> dp(static_cast<size_t>(nBeats));
    std::vector<std::array<int, 25>> from(static_cast<size_t>(nBeats));
    for (int s = 0; s < S; ++s)
        dp[0][size_t(s)] = emission[0][size_t(s)];
    for (int k = 1; k < nBeats; ++k) {
        int bestPrev = 0;
        for (int s = 1; s < S; ++s)
            if (dp[size_t(k - 1)][size_t(s)] > dp[size_t(k - 1)][size_t(bestPrev)])
                bestPrev = s;
        for (int s = 0; s < S; ++s) {
            const double viaStay = dp[size_t(k - 1)][size_t(s)] + stay;
            const double viaChange = dp[size_t(k - 1)][size_t(bestPrev)] + change;
            if (viaStay >= viaChange || bestPrev == s) {
                dp[size_t(k)][size_t(s)] = viaStay + emission[size_t(k)][size_t(s)];
                from[size_t(k)][size_t(s)] = s;
            } else {
                dp[size_t(k)][size_t(s)] = viaChange + emission[size_t(k)][size_t(s)];
                from[size_t(k)][size_t(s)] = bestPrev;
            }
        }
    }
    const bool debug = qEnvironmentVariableIsSet("GCP_DEBUG");
    if (debug) {
        fprintf(stderr, "tuning %.0f cents, T %.4f t0 %.3f beats %d\n", bestCents, T, t0, nBeats);
        for (int k = 0; k < std::min(nBeats, 24); ++k) {
            const auto &c = beatChroma[size_t(k)];
            fprintf(stderr, "beat %2d:", k);
            for (int i = 0; i < 12; ++i)
                fprintf(stderr, " %s:%.0f", kNames[i], c[size_t(i)]);
            int bs = 0;
            for (int s2 = 1; s2 < 24; ++s2)
                if (emission[size_t(k)][size_t(s2)] > emission[size_t(k)][size_t(bs)])
                    bs = s2;
            fprintf(stderr, "  -> %s%s\n", kNames[bs % 12], bs >= 12 ? "m" : "");
        }
    }
    std::vector<ChordLabel> labels(static_cast<size_t>(nBeats));
    if (nBeats > 0) {
        int s = 0;
        for (int i = 1; i < S; ++i)
            if (dp[size_t(nBeats - 1)][size_t(i)] > dp[size_t(nBeats - 1)][size_t(s)])
                s = i;
        for (int k = nBeats - 1; k >= 0; --k) {
            labels[size_t(k)] = s == 24 ? ChordLabel{} : ChordLabel{s % 12, s >= 12};
            s = from[size_t(k)][size_t(s)];
        }
    }
    if (progress)
        progress(0.9);

    // ---- 8. Meter and bar lines: chords change on bar lines far more often than elsewhere
    std::vector<int> changes;
    for (int k = 1; k < nBeats; ++k)
        if (labels[size_t(k)].name() != labels[size_t(k - 1)].name())
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
                const size_t f = size_t(std::lround((t0 + k * T) * onRate));
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

    // ---- 9. Bars: one chord per half bar (4/4) or per bar (3/4)
    const int firstBeat = bestPhase; // first downbeat index at or after t0
    // Include a partial bar at the start if the music begins before the first downbeat.
    const int startBeat = firstBeat >= 2 ? firstBeat - bestBpb : firstBeat;
    QVector<QStringList> bars;
    for (int k = startBeat; k < nBeats; k += bestBpb) {
        auto majority = [&](int a, int b) {
            std::map<QString, int> votes;
            for (int i = a; i < b; ++i) {
                const QString n = (i >= 0 && i < nBeats) ? labels[size_t(i)].name() : QStringLiteral("N.C.");
                votes[n] += (i == a) ? 2 : 1; // the chord on the beat counts double
            }
            return std::max_element(votes.begin(), votes.end(), [](auto &x, auto &y) { return x.second < y.second; })->first;
        };
        if (bestBpb == 4) {
            const QString a = majority(k, k + 2), b = majority(k + 2, k + 4);
            bars.push_back(a == b ? QStringList{a} : QStringList{a, b});
        } else {
            bars.push_back(QStringList{majority(k, k + bestBpb)});
        }
    }
    // Drop silent bars at the very end.
    while (!bars.isEmpty() && bars.last() == QStringList{QStringLiteral("N.C.")})
        bars.removeLast();
    if (bars.isEmpty()) {
        if (error)
            *error = QStringLiteral("No chords found in the recording.");
        return false;
    }

    // ---- 10. Key (most common chord, by time) and an easy capo position
    std::map<QString, int> count;
    for (const auto &b : bars)
        for (const QString &c : b)
            if (c != QLatin1String("N.C."))
                count[c] += 2 / int(b.size());
    out->key = count.empty() ? QString()
                             : std::max_element(count.begin(), count.end(), [](auto &x, auto &y) { return x.second < y.second; })->first;
    int bestCapo = 0, bestCost = INT32_MAX;
    for (int capo = 0; capo <= 5; ++capo) {
        int cost = 0;
        for (const auto &b : bars)
            for (const QString &c : b)
                cost += shapeCost(transposeName(c, -capo));
        if (cost < bestCost) {
            bestCost = cost;
            bestCapo = capo;
        }
    }
    for (auto &b : bars)
        for (QString &c : b)
            c = transposeName(c, -bestCapo);

    double fitSum = 0;
    for (double v : bestFit)
        fitSum += v;
    out->confidence = nBeats ? std::clamp(fitSum / nBeats, 0.0, 1.0) : 0.0;
    out->bpm = 60.0 / T;
    out->beatsPerBar = bestBpb;
    out->offset = t0 + startBeat * T;
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

    w.writeEmptyElement(QStringLiteral("audio"));
    w.writeAttribute(QStringLiteral("file"), audioFileName);
    w.writeAttribute(QStringLiteral("offset"), QString::number(song.offset, 'f', 3));

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
