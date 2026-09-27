#include "Sequencer.h"

#include <algorithm>
#include <cmath>

void Sequencer::setSampleRate(double sr)
{
    m_sr = sr;
    m_synth.setSampleRate(sr);
}

void Sequencer::computeBarTimes()
{
    m_barTime.clear();
    if (!m_tl)
        return;
    double t = 0.0;
    for (const Timeline::Bar &b : m_tl->bars) {
        m_barTime.push_back(t);
        t += b.beatsPerBar * 60.0 / std::max(10.0, b.bpm);
    }
    m_barTime.push_back(t);
}

double Sequencer::expectedAudioTime() const
{
    if (!m_tl || m_bar < 0 || m_bar >= int(m_tl->bars.size()))
        return 0.0;
    const Timeline::Bar &b = m_tl->bars[m_bar];
    return m_audioOffset + m_barTime[size_t(m_bar)] + m_step * 60.0 / std::max(10.0, b.bpm) / b.subdivision;
}

void Sequencer::setAudio(std::shared_ptr<const AudioClip> clip, double offset)
{
    m_stretch.setClip(std::move(clip));
    m_audioOffset = offset;
    m_audioRunning = false;
}

void Sequencer::setTimeline(std::shared_ptr<const Timeline> tl)
{
    m_tl = std::move(tl);
    computeBarTimes();
    m_audioRunning = false;
    m_playing = false;
    m_pending.clear();
    m_synth.dampAll(0.1);
    m_bar = 0;
    m_step = 0;
    m_curChord = -2;
    m_loopFirst = m_loopLast = -1;
    m_history.clear();
    m_plucks.clear();
    recordPos(0, 0, 0);
}

void Sequencer::replaceTimeline(std::shared_ptr<const Timeline> tl)
{
    if (!m_tl || !tl || tl->bars.isEmpty()) {
        setTimeline(std::move(tl));
        return;
    }
    m_tl = std::move(tl);
    computeBarTimes();
    m_resync = true;
    const int count = int(m_tl->bars.size());
    if (m_bar > count || (m_bar == count && m_playing)) {
        m_bar = count - 1;
        m_step = 0;
    }
    if (m_bar < count && m_step >= m_tl->bars[m_bar].stepCount())
        m_step = 0;
    m_curChord = -2;
}

void Sequencer::play()
{
    if (!m_tl || m_playing)
        return;
    if (m_bar >= m_tl->bars.size()) {
        m_bar = 0;
        m_step = 0;
    }
    m_countInLeft = 0;
    if (m_countInEnabled && m_step == 0) {
        m_countInTotal = m_tl->bars[m_bar].beatsPerBar;
        m_countInLeft = m_countInTotal;
    }
    m_samplesToNext = 0;
    m_playing = true;
}

void Sequencer::pause()
{
    if (!m_playing)
        return;
    m_playing = false;
    m_audioRunning = false;
    m_pending.clear();
    m_synth.dampAll(0.4);
    recordPos(m_bar < (m_tl ? m_tl->bars.size() : 0) ? m_bar : 0, m_step, 0);
}

void Sequencer::stop()
{
    m_playing = false;
    m_audioRunning = false;
    m_pending.clear();
    m_synth.dampAll(0.1);
    m_bar = 0;
    m_step = 0;
    m_curChord = -2;
    recordPos(0, 0, 0);
}

void Sequencer::seekToBar(int bar)
{
    if (!m_tl)
        return;
    m_bar = std::clamp(bar, 0, int(m_tl->bars.size()) - 1);
    m_step = 0;
    m_audioRunning = false;
    m_pending.clear();
    m_synth.dampAll(0.1);
    m_curChord = -2;
    m_countInLeft = 0;
    m_samplesToNext = 0;
    recordPos(m_bar, 0, 0);
}

double Sequencer::stepSamples(const Timeline::Bar &bar) const
{
    const double bpm = std::max(10.0, bar.bpm * m_tempoScale);
    return m_sr * 60.0 / bpm / bar.subdivision;
}

double Sequencer::noteFreq(int string, int fret) const
{
    const Song &s = *m_tl->song;
    const int midi = s.tuning[size_t(string)] + s.capo + std::max(0, fret);
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

void Sequencer::recordPos(int bar, int step, double length)
{
    m_history.push_back({m_frame, bar, step, length, m_playing});
    while (m_history.size() > 512)
        m_history.pop_front();
}

void Sequencer::previewChord(const ChordShape &chord)
{
    static const int standard[6] = {40, 45, 50, 55, 59, 64};
    int n = 0;
    for (int i = 0; i < 6; ++i) {
        if (chord.frets[size_t(i)] < 0) {
            m_synth.damp(i, 0.05);
            continue;
        }
        const int midi = standard[i] + chord.frets[size_t(i)];
        Pending p;
        p.delay = 1 + int(n++ * 0.018 * m_sr);
        p.string = i;
        p.freq = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
        p.velocity = 0.75f;
        p.brightness = 0.7f;
        p.muted = false;
        m_pending.push_back(p);
    }
}

void Sequencer::render(float *left, float *right, int frames)
{
    // The recording is pulled in chunks of up to 64 frames, so a re-sync at a step boundary
    // takes effect within a millisecond or so.
    constexpr int kChunk = 64;
    if (m_audioL.size() < size_t(kChunk)) {
        m_audioL.resize(kChunk);
        m_audioR.resize(kChunk);
    }
    int audioEnd = 0;          // frames [m_audioChunkStart, audioEnd) are in m_audioL/R
    m_audioChunkStart = 0;

    for (int i = 0; i < frames; ++i) {
        if (m_playing && m_tl) {
            int guard = 0;
            while (m_playing && m_samplesToNext <= 0 && guard++ < 8)
                fireNextStep();
            if (m_audioSeeked) {
                audioEnd = i; // the recording jumped: fetch fresh audio from here
                m_audioSeeked = false;
            }
            m_samplesToNext -= 1.0;
        }

        for (size_t p = 0; p < m_pending.size();) {
            Pending &pe = m_pending[p];
            if (--pe.delay <= 0) {
                m_synth.pluck(pe.string, pe.freq, pe.velocity, pe.brightness, pe.muted);
                m_plucks.push_back({m_frame, pe.string, pe.muted ? pe.velocity * 0.4f : pe.velocity});
                if (m_plucks.size() > 128)
                    m_plucks.pop_front();
                m_pending[p] = m_pending.back();
                m_pending.pop_back();
            } else {
                ++p;
            }
        }

        float l, r;
        m_synth.tick(l, r);
        const float gv = m_guitarEnabled ? m_volume : 0.f;
        left[i] = l * gv;
        right[i] = r * gv;

        if (m_stretch.clip() && m_audioRunning && m_playing) {
            if (i >= audioEnd) {
                const int chunk = std::min(kChunk, frames - i);
                m_stretch.render(m_audioL.data(), m_audioR.data(), chunk, m_tempoScale);
                m_audioChunkStart = i;
                audioEnd = i + chunk;
            }
            if (m_audioEnabled) {
                left[i] += m_audioL[size_t(i - m_audioChunkStart)] * m_audioVolume;
                right[i] += m_audioR[size_t(i - m_audioChunkStart)] * m_audioVolume;
            }
        }
        ++m_frame;
    }
}

void Sequencer::fireNextStep()
{
    const auto &bars = m_tl->bars;

    if (m_countInLeft > 0) {
        m_audioRunning = false;
        const int beat = m_countInTotal - m_countInLeft;
        const Timeline::Bar &b = bars[m_bar];
        const double beatLen = stepSamples(b) * b.subdivision;
        m_synth.click(beat == 0);
        recordPos(-1, beat, beatLen);
        --m_countInLeft;
        m_samplesToNext += beatLen;
        return;
    }

    if (m_bar >= bars.size()) {
        m_playing = false;
        m_audioRunning = false;
        recordPos(-2, 0, 0);
        return;
    }

    const Timeline::Bar &bar = bars[m_bar];
    const double len = stepSamples(bar);
    recordPos(m_bar, m_step, len);

    // Keep the recording in step with the guitar: start it, or correct it if it drifted.
    if (m_stretch.clip()) {
        const double expected = expectedAudioTime();
        if (!m_audioRunning || m_resync || std::abs(m_stretch.position() - expected) > 0.06) {
            m_stretch.seek(expected);
            m_audioRunning = true;
            m_resync = false;
            m_audioSeeked = true;
        }
    }

    if (m_metronome && m_step % bar.subdivision == 0)
        m_synth.click(m_step == 0);

    const int chord = bar.chordAtStep.value(m_step, -1);
    if (chord != m_curChord)
        changeChord(chord);
    if (const PatternStep *st = m_tl->stepAt(m_bar, m_step))
        perform(*st, chord, len / m_sr, m_step % bar.subdivision == 0, m_step == 0);

    m_samplesToNext += len;

    if (++m_step >= bar.stepCount()) {
        m_step = 0;
        const bool looping = m_loopFirst >= 0 && m_loopLast >= m_loopFirst;
        int next = m_bar + 1;
        if (looping && (next > m_loopLast || next < m_loopFirst))
            next = m_loopFirst;
        m_bar = next;
    }
}

void Sequencer::changeChord(int chord)
{
    // Lift fingers: strings whose fret changes stop ringing, like a real chord change.
    if (chord < 0) {
        m_synth.dampAll(0.15);
    } else if (m_curChord >= 0) {
        const ChordShape &a = m_tl->chords[m_curChord];
        const ChordShape &b = m_tl->chords[chord];
        for (int i = 0; i < 6; ++i)
            if (a.frets[size_t(i)] != b.frets[size_t(i)])
                m_synth.damp(i, 0.12);
    }
    m_curChord = chord;
}

void Sequencer::schedule(int string, int fret, double delaySec, float velocity, float brightness, bool muted)
{
    std::normal_distribution<double> jitter(0.0, 0.0012);
    std::uniform_real_distribution<float> vel(0.94f, 1.06f);
    Pending p;
    p.delay = std::max(1, int((delaySec + std::abs(jitter(m_rng))) * m_sr));
    p.string = string;
    p.freq = noteFreq(string, fret);
    p.velocity = std::clamp(velocity * vel(m_rng), 0.05f, 1.0f);
    p.brightness = brightness;
    p.muted = muted;
    m_pending.push_back(p);
}

void Sequencer::perform(const PatternStep &st, int chordIdx, double stepSeconds, bool onBeat, bool downbeat)
{
    using K = PatternStep::Kind;
    // Dynamics like a real player: the "1" is strongest, off-beats a little softer.
    float accent = st.accent ? 1.3f : 1.0f;
    accent *= downbeat ? 1.08f : onBeat ? 1.0f : 0.9f;
    // The whole strum starts a hair early or late, as a human would.
    std::uniform_real_distribution<double> start(0.0, 0.006);
    const double t0 = start(m_rng);

    if (st.kind == K::Rest)
        return;

    if (st.kind == K::Mute) {
        // Percussive "chuck": the strumming hand slaps and mutes all strings at once.
        m_synth.dampAll(0.03);
        for (int i = 0; i < 6; ++i) {
            const int fret = chordIdx >= 0 ? std::max(0, m_tl->chords[chordIdx].frets[size_t(i)]) : 0;
            schedule(i, fret, t0 + i * 0.0025, 0.6f * accent, 0.9f, true);
        }
        return;
    }

    if (chordIdx < 0)
        return;
    const ChordShape &c = m_tl->chords[chordIdx];
    auto played = [&](int i) { return i >= 0 && i < 6 && c.frets[size_t(i)] >= 0; };

    if (st.kind == K::Down || st.kind == K::Up) {
        std::vector<int> strings;
        if (st.kind == K::Down) {
            const int from = st.light ? 2 : 0;
            for (int i = from; i < 6; ++i)
                if (played(i))
                    strings.push_back(i);
        } else {
            // Up-strums mostly catch the treble strings; a full one brushes the 5th string too.
            const int to = st.light ? 3 : 1;
            for (int i = 5; i >= to; --i)
                if (played(i))
                    strings.push_back(i);
        }
        if (strings.empty())
            return;
        // A strum takes longer at slow tempos, and speeds up as the pick crosses the strings.
        double total = std::clamp(stepSeconds * 0.35, 0.012, 0.045);
        if (st.light)
            total *= 0.75;
        if (st.kind == K::Up)
            total *= 0.8;
        const size_t n = strings.size();
        float base = st.kind == K::Down ? (st.light ? 0.5f : 0.82f) : (st.light ? 0.38f : 0.58f);
        base *= accent;
        const float bright = st.kind == K::Up ? 0.9f : 0.72f;
        for (size_t k = 0; k < n; ++k) {
            const double frac = n > 1 ? std::pow(double(k) / double(n - 1), 0.85) : 0.0;
            // The first strings hit are the loudest; the last ones only get brushed.
            float v = base * (1.0f - 0.06f * float(k));
            if (st.kind == K::Up && k + 1 == n && !st.light)
                v *= 0.55f; // the lowest string of an up-strum is just grazed
            schedule(strings[k], c.frets[size_t(strings[k])], t0 + total * frac, v, bright, false);
        }
        return;
    }

    // Picked notes (fingerstyle / arpeggio)
    const int bass = c.bassIndex();
    for (int s : st.strings) {
        int idx;
        float v = 0.7f;
        if (s == 0) {
            idx = bass;
            v = 0.8f;
        } else if (s == -1) {
            // Alternate bass: prefer the fifth above the root on the next two strings.
            idx = played(bass + 1) ? bass + 1 : bass;
            const int root = m_tl->song->tuning[size_t(bass)] + c.frets[size_t(bass)];
            for (int cand = bass + 1; cand <= bass + 2; ++cand) {
                if (played(cand)) {
                    const int m = m_tl->song->tuning[size_t(cand)] + c.frets[size_t(cand)];
                    if (((m - root) % 12 + 12) % 12 == 7) {
                        idx = cand;
                        break;
                    }
                }
            }
            v = 0.75f;
        } else {
            idx = 6 - s;
        }
        if (!played(idx))
            continue;
        schedule(idx, c.frets[size_t(idx)], t0 * 0.5, v * accent, 0.6f, false);
    }
}

Sequencer::Snapshot Sequencer::snapshot(qint64 playedFrame) const
{
    Snapshot s;
    const PosEntry *e = nullptr;
    for (auto it = m_history.rbegin(); it != m_history.rend(); ++it) {
        if (it->frame <= playedFrame) {
            e = &*it;
            break;
        }
    }
    if (!e && !m_history.empty())
        e = &m_history.front();
    if (e) {
        s.playing = e->playing;
        if (e->bar == -1) {
            s.countIn = true;
            s.countInBeat = e->step;
            s.bar = m_bar;
        } else if (e->bar == -2) {
            s.finished = true;
            s.playing = false;
            s.bar = m_tl ? int(m_tl->bars.size()) - 1 : 0;
        } else {
            s.bar = e->bar;
            s.step = e->step;
            if (e->length > 0)
                s.stepFraction = std::clamp((playedFrame - e->frame) / e->length, 0.0, 1.0);
        }
    }

    const double decay = 0.25 * m_sr;
    for (const PluckEvent &p : m_plucks) {
        if (p.frame > playedFrame)
            continue;
        const float g = p.velocity * float(std::exp(-(playedFrame - p.frame) / decay));
        s.stringGlow[size_t(p.string)] = std::max(s.stringGlow[size_t(p.string)], g);
    }
    return s;
}
