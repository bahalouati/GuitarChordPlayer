#include "Sequencer.h"

#include <algorithm>
#include <cmath>

void Sequencer::setSampleRate(double sr)
{
    m_sr = sr;
    m_synth.setSampleRate(sr);
}

void Sequencer::setTimeline(std::shared_ptr<const Timeline> tl)
{
    m_tl = std::move(tl);
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
    m_pending.clear();
    m_synth.dampAll(0.4);
    recordPos(m_bar < (m_tl ? m_tl->bars.size() : 0) ? m_bar : 0, m_step, 0);
}

void Sequencer::stop()
{
    m_playing = false;
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

void Sequencer::render(float *out, int frames)
{
    for (int i = 0; i < frames; ++i) {
        if (m_playing && m_tl) {
            int guard = 0;
            while (m_playing && m_samplesToNext <= 0 && guard++ < 8)
                fireNextStep();
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

        out[i] = m_synth.tick() * m_volume;
        ++m_frame;
    }
}

void Sequencer::fireNextStep()
{
    const auto &bars = m_tl->bars;

    if (m_countInLeft > 0) {
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
        recordPos(-2, 0, 0);
        return;
    }

    const Timeline::Bar &bar = bars[m_bar];
    const double len = stepSamples(bar);
    recordPos(m_bar, m_step, len);

    if (m_metronome && m_step % bar.subdivision == 0)
        m_synth.click(m_step == 0);

    const int chord = bar.chordAtStep.value(m_step, -1);
    if (chord != m_curChord)
        changeChord(chord);
    if (const PatternStep *st = m_tl->stepAt(m_bar, m_step))
        perform(*st, chord);

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
    std::uniform_real_distribution<double> jitter(0.0, 0.004);
    std::uniform_real_distribution<float> vel(0.93f, 1.07f);
    Pending p;
    p.delay = std::max(1, int((delaySec + jitter(m_rng)) * m_sr));
    p.string = string;
    p.freq = noteFreq(string, fret);
    p.velocity = std::clamp(velocity * vel(m_rng), 0.05f, 1.0f);
    p.brightness = brightness;
    p.muted = muted;
    m_pending.push_back(p);
}

void Sequencer::perform(const PatternStep &st, int chordIdx)
{
    using K = PatternStep::Kind;
    const float accent = st.accent ? 1.3f : 1.0f;

    if (st.kind == K::Rest)
        return;

    if (st.kind == K::Mute) {
        // Percussive "chuck": all strings muted by the strumming hand.
        for (int i = 0; i < 6; ++i) {
            const int fret = chordIdx >= 0 ? std::max(0, m_tl->chords[chordIdx].frets[size_t(i)]) : 0;
            schedule(i, fret, i * 0.004, 0.55f * accent, 0.9f, true);
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
            const int to = st.light ? 3 : 2;
            for (int i = 5; i >= to; --i)
                if (played(i))
                    strings.push_back(i);
        }
        const double spread = st.light ? 0.008 : 0.011;
        float base = st.kind == K::Down ? (st.light ? 0.5f : 0.8f) : (st.light ? 0.4f : 0.6f);
        base *= accent;
        const float bright = st.kind == K::Up ? 0.85f : 0.7f;
        for (size_t n = 0; n < strings.size(); ++n) {
            const float v = base * (1.0f - 0.04f * float(n));
            schedule(strings[n], c.frets[size_t(strings[n])], n * spread, v, bright, false);
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
        schedule(idx, c.frets[size_t(idx)], 0.0, v * accent, 0.6f, false);
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
