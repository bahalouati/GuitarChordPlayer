#include "AudioTrack.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>

std::shared_ptr<AudioClip> decodeAudioFile(const QString &path, int targetRate, QString *error,
                                           const std::function<void(double)> &progress)
{
    if (!QFileInfo::exists(path)) {
        if (error)
            *error = QStringLiteral("File not found: %1").arg(path);
        return nullptr;
    }

    QAudioDecoder decoder;
    decoder.setSource(QUrl::fromLocalFile(path));
    std::vector<float> l, r;
    int srcRate = 0;
    QString err;
    QEventLoop loop;

    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&] {
        const QAudioBuffer buf = decoder.read();
        if (!buf.isValid())
            return;
        const QAudioFormat fmt = buf.format();
        srcRate = fmt.sampleRate();
        const int ch = fmt.channelCount();
        const int frames = int(buf.frameCount());
        const int bps = fmt.bytesPerSample();
        const char *data = buf.constData<char>();
        for (int f = 0; f < frames; ++f) {
            float v[2] = {0.f, 0.f};
            for (int c = 0; c < std::min(ch, 2); ++c) {
                const char *p = data + (size_t(f) * size_t(ch) + size_t(c)) * size_t(bps);
                switch (fmt.sampleFormat()) {
                case QAudioFormat::Float: v[c] = *reinterpret_cast<const float *>(p); break;
                case QAudioFormat::Int16: v[c] = *reinterpret_cast<const qint16 *>(p) / 32768.f; break;
                case QAudioFormat::Int32: v[c] = float(*reinterpret_cast<const qint32 *>(p) / 2147483648.0); break;
                case QAudioFormat::UInt8: v[c] = (*reinterpret_cast<const quint8 *>(p) - 128) / 128.f; break;
                default: break;
                }
            }
            l.push_back(v[0]);
            r.push_back(ch > 1 ? v[1] : v[0]);
        }
        if (progress && decoder.duration() > 0)
            progress(std::min(1.0, double(buf.startTime() / 1000) / double(decoder.duration())));
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop,
                     [&](QAudioDecoder::Error) {
                         err = decoder.errorString();
                         loop.quit();
                     });
    decoder.start();
    if (decoder.error() != QAudioDecoder::NoError) {
        if (error)
            *error = decoder.errorString();
        return nullptr;
    }
    // Watchdog: if the decoder stops delivering audio without saying it finished, give up
    // instead of waiting forever (keep what was decoded so far).
    size_t lastSize = 0;
    int idleTicks = 0;
    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, &loop, [&] {
        if (l.size() != lastSize) {
            lastSize = l.size();
            idleTicks = 0;
        } else if (++idleTicks >= 20) { // 10 seconds without progress
            if (l.empty())
                err = QStringLiteral("The decoder stopped responding.");
            loop.quit();
        }
    });
    watchdog.start(500);
    loop.exec();
    watchdog.stop();
    decoder.stop();

    if (l.empty() || srcRate <= 0) {
        if (error)
            *error = err.isEmpty() ? QStringLiteral("Could not decode %1").arg(QFileInfo(path).fileName()) : err;
        return nullptr;
    }

    auto clip = std::make_shared<AudioClip>();
    clip->path = path;
    clip->sampleRate = targetRate;
    if (srcRate == targetRate) {
        clip->left = std::move(l);
        clip->right = std::move(r);
    } else {
        // Resample with cubic (Catmull-Rom) interpolation.
        const double step = double(srcRate) / targetRate;
        const size_t outFrames = size_t(double(l.size()) / step);
        clip->left.resize(outFrames);
        clip->right.resize(outFrames);
        auto at = [](const std::vector<float> &v, long long i) {
            return v[size_t(std::clamp<long long>(i, 0, (long long)v.size() - 1))];
        };
        for (size_t o = 0; o < outFrames; ++o) {
            const double pos = o * step;
            const long long i = (long long)pos;
            const float t = float(pos - double(i));
            for (int c = 0; c < 2; ++c) {
                const std::vector<float> &src = c == 0 ? l : r;
                const float p0 = at(src, i - 1), p1 = at(src, i), p2 = at(src, i + 1), p3 = at(src, i + 2);
                const float v = p1 + 0.5f * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));
                (c == 0 ? clip->left : clip->right)[o] = v;
            }
        }
    }
    if (progress)
        progress(1.0);
    return clip;
}

// ---------------------------------------------------------------- Stretcher

void Stretcher::setClip(std::shared_ptr<const AudioClip> clip)
{
    m_clip = std::move(clip);
    if (m_window.empty()) {
        m_window.resize(kN);
        for (int i = 0; i < kN; ++i)
            m_window[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * 3.14159265358979 * i / kN));
    }
    seek(0);
}

float Stretcher::sampleL(long long i) const
{
    return (i >= 0 && i < (long long)m_clip->left.size()) ? m_clip->left[size_t(i)] : 0.f;
}

float Stretcher::sampleR(long long i) const
{
    return (i >= 0 && i < (long long)m_clip->right.size()) ? m_clip->right[size_t(i)] : 0.f;
}

void Stretcher::seek(double seconds)
{
    m_readPos = m_clip ? seconds * m_clip->sampleRate : 0.0;
    m_havePrev = false;
    m_overlapL.assign(kHop, 0.f);
    m_overlapR.assign(kHop, 0.f);
    m_outL.clear();
    m_outR.clear();
    m_outPos = 0;
    m_fadeIn = 256; // avoid a click when jumping
}

double Stretcher::position() const
{
    if (!m_clip)
        return 0.0;
    // Samples already produced but not yet played still belong to the past read position.
    const double pending = double(m_outL.size() - m_outPos) * m_lastRate;
    return (m_readPos - pending) / m_clip->sampleRate;
}

void Stretcher::produceBlock(double rate)
{
    const long long target = (long long)std::llround(m_readPos);
    long long actual = target;
    if (m_havePrev) {
        // Find the window near the target that best continues the previous one (WSOLA).
        const long long natural = m_prevActual + kHop;
        double best = -1e30;
        for (long long d = -kSearch; d <= kSearch; d += 4) {
            double corr = 0.0;
            for (int i = 0; i < kHop; i += 8) {
                const float a = sampleL(target + d + i) + sampleR(target + d + i);
                const float b = sampleL(natural + i) + sampleR(natural + i);
                corr += double(a) * b;
            }
            if (corr > best) {
                best = corr;
                actual = target + d;
            }
        }
    }
    std::vector<float> outL(kHop), outR(kHop);
    for (int i = 0; i < kHop; ++i) {
        outL[size_t(i)] = m_overlapL[size_t(i)] + sampleL(actual + i) * m_window[size_t(i)];
        outR[size_t(i)] = m_overlapR[size_t(i)] + sampleR(actual + i) * m_window[size_t(i)];
    }
    for (int i = 0; i < kHop; ++i) {
        m_overlapL[size_t(i)] = sampleL(actual + kHop + i) * m_window[size_t(kHop + i)];
        m_overlapR[size_t(i)] = sampleR(actual + kHop + i) * m_window[size_t(kHop + i)];
    }
    if (!m_havePrev) {
        // First block after a seek has no overlap partner: use the plain samples.
        for (int i = 0; i < kHop; ++i) {
            outL[size_t(i)] = sampleL(actual + i);
            outR[size_t(i)] = sampleR(actual + i);
        }
        for (int i = 0; i < kHop; ++i) {
            // Continue with a window that sums to one with the first half of the next.
            m_overlapL[size_t(i)] = sampleL(actual + kHop + i) * m_window[size_t(kHop + i)];
            m_overlapR[size_t(i)] = sampleR(actual + kHop + i) * m_window[size_t(kHop + i)];
        }
    }
    m_prevActual = actual;
    m_havePrev = true;
    m_readPos += kHop * rate;

    // Drop what has been played and append the new block.
    m_outL.erase(m_outL.begin(), m_outL.begin() + long(m_outPos));
    m_outR.erase(m_outR.begin(), m_outR.begin() + long(m_outPos));
    m_outPos = 0;
    m_outL.insert(m_outL.end(), outL.begin(), outL.end());
    m_outR.insert(m_outR.end(), outR.begin(), outR.end());
}

void Stretcher::render(float *left, float *right, int frames, double rate)
{
    if (!m_clip) {
        std::fill(left, left + frames, 0.f);
        std::fill(right, right + frames, 0.f);
        return;
    }
    rate = std::clamp(rate, 0.25, 2.0);
    m_lastRate = rate;
    if (std::abs(rate - 1.0) < 1e-6 && m_outL.size() == m_outPos) {
        // Normal speed: straight copy, sample exact.
        long long pos = (long long)std::llround(m_readPos);
        for (int i = 0; i < frames; ++i, ++pos) {
            float g = 1.f;
            if (m_fadeIn > 0)
                g = 1.f - float(m_fadeIn--) / 256.f;
            left[i] = sampleL(pos) * g;
            right[i] = sampleR(pos) * g;
        }
        m_readPos = double(pos);
        m_havePrev = false;
        m_overlapL.assign(kHop, 0.f);
        m_overlapR.assign(kHop, 0.f);
        return;
    }
    for (int i = 0; i < frames; ++i) {
        if (m_outPos >= m_outL.size())
            produceBlock(rate);
        float g = 1.f;
        if (m_fadeIn > 0)
            g = 1.f - float(m_fadeIn--) / 256.f;
        left[i] = m_outL[m_outPos] * g;
        right[i] = m_outR[m_outPos] * g;
        ++m_outPos;
    }
}
