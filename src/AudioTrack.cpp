#include "AudioTrack.h"

#include "minimp3_ex.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

// MP3 with minimp3: handles ID3 tags and cover art, no system codecs needed.
bool decodeMp3(const QByteArray &data, std::vector<float> *l, std::vector<float> *r, int *rate, QString *err,
               const std::function<void(double)> &progress)
{
    mp3dec_t dec;
    mp3dec_file_info_t info{};
    struct Ctx { const std::function<void(double)> *progress; } ctx{&progress};
    auto cb = [](void *user, size_t fileSize, size_t offset, mp3dec_frame_info_t *) -> int {
        auto *c = static_cast<Ctx *>(user);
        if (*c->progress && fileSize > 0)
            (*c->progress)(double(offset) / double(fileSize));
        return 0;
    };
    const int res = mp3dec_load_buf(&dec, reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()),
                                    &info, cb, &ctx);
    if (res != 0 || !info.buffer || info.samples == 0 || info.channels <= 0 || info.hz <= 0) {
        free(info.buffer);
        if (err)
            *err = QStringLiteral("This MP3 file could not be read (error %1).").arg(res);
        return false;
    }
    const size_t frames = info.samples / size_t(info.channels);
    l->resize(frames);
    r->resize(frames);
    for (size_t f = 0; f < frames; ++f) {
        const float a = info.buffer[f * size_t(info.channels)];
        (*l)[f] = a;
        (*r)[f] = info.channels > 1 ? info.buffer[f * size_t(info.channels) + 1] : a;
    }
    *rate = info.hz;
    free(info.buffer);
    return true;
}

// Uncompressed WAV (PCM 8/16/24/32-bit or 32-bit float).
bool decodeWav(const QByteArray &d, std::vector<float> *l, std::vector<float> *r, int *rate, QString *err)
{
    auto u16 = [&](qsizetype o) { return quint16(quint8(d[o]) | quint8(d[o + 1]) << 8); };
    auto u32 = [&](qsizetype o) { return quint32(u16(o)) | quint32(u16(o + 2)) << 16; };
    if (d.size() < 44 || !d.startsWith("RIFF") || d.mid(8, 4) != "WAVE")
        return false;
    int format = 0, channels = 0, bits = 0;
    qsizetype pos = 12, dataPos = -1;
    quint32 dataLen = 0;
    while (pos + 8 <= d.size()) {
        const QByteArray id = d.mid(pos, 4);
        const quint32 len = u32(pos + 4);
        if (id == "fmt " && pos + 24 <= d.size()) {
            format = u16(pos + 8);
            channels = u16(pos + 10);
            *rate = int(u32(pos + 12));
            bits = u16(pos + 22);
            if (format == 0xFFFE && len >= 40)
                format = u16(pos + 32); // WAVE_FORMAT_EXTENSIBLE: sub-format
        } else if (id == "data") {
            dataPos = pos + 8;
            dataLen = quint32(std::min<qsizetype>(len, d.size() - dataPos));
            break;
        }
        pos += 8 + len + (len & 1);
    }
    const int bps = bits / 8;
    if (dataPos < 0 || channels <= 0 || bps <= 0 || *rate <= 0 || (format != 1 && format != 3)) {
        if (err)
            *err = QStringLiteral("Unsupported WAV format.");
        return false;
    }
    const size_t frames = dataLen / size_t(channels * bps);
    l->resize(frames);
    r->resize(frames);
    const char *p = d.constData() + dataPos;
    auto sample = [&](const char *s) -> float {
        if (format == 3 && bps == 4) {
            float f;
            std::memcpy(&f, s, 4);
            return f;
        }
        switch (bps) {
        case 1: return (quint8(s[0]) - 128) / 128.f;
        case 2: return qint16(quint16(quint8(s[0]) | quint8(s[1]) << 8)) / 32768.f;
        case 3: return float(qint32((quint32(quint8(s[0])) << 8) | (quint32(quint8(s[1])) << 16) | (quint32(quint8(s[2])) << 24)) / 2147483648.0);
        case 4: {
            qint32 v;
            std::memcpy(&v, s, 4);
            return float(v / 2147483648.0);
        }
        default: return 0.f;
        }
    };
    for (size_t f = 0; f < frames; ++f) {
        const char *fr = p + f * size_t(channels * bps);
        (*l)[f] = sample(fr);
        (*r)[f] = channels > 1 ? sample(fr + bps) : (*l)[f];
    }
    return true;
}

// Anything else (M4A, OGG, FLAC, WMA...) through Qt Multimedia.
bool decodeWithQt(const QString &path, std::vector<float> &l, std::vector<float> &r, int *rate, QString *error,
                  const std::function<void(double)> &progress)
{
    int srcRate = 0;
    QString err;
    QEventLoop loop;
    QAudioDecoder decoder;
    decoder.setSource(QUrl::fromLocalFile(path));

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
        return false;
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

    *rate = srcRate;
    if (l.empty() || srcRate <= 0) {
        if (error)
            *error = err.isEmpty() ? QStringLiteral("The system audio decoder returned no sound.") : err;
        return false;
    }
    return true;
}

} // namespace

std::shared_ptr<AudioClip> decodeAudioFile(const QString &path, int targetRate, QString *error,
                                           const std::function<void(double)> &progress)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open %1: %2").arg(QFileInfo(path).fileName(), file.errorString());
        return nullptr;
    }
    std::vector<float> l, r;
    int srcRate = 0;
    QString err;
    bool ok = false;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("mp3") || suffix == QLatin1String("wav")) {
        const QByteArray data = file.readAll();
        ok = suffix == QLatin1String("mp3") ? decodeMp3(data, &l, &r, &srcRate, &err, progress)
                                            : decodeWav(data, &l, &r, &srcRate, &err);
    }
    file.close();
    if (!ok) {
        // Other formats, or an MP3/WAV our decoders could not read: try the system's decoder.
        QString qtErr;
        l.clear();
        r.clear();
        ok = decodeWithQt(path, l, r, &srcRate, &qtErr, progress);
        if (!ok && err.isEmpty())
            err = qtErr;
    }
    if (!ok) {
        if (error)
            *error = QStringLiteral("Could not decode %1: %2").arg(QFileInfo(path).fileName(),
                                                                  err.isEmpty() ? QStringLiteral("unknown error") : err);
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
