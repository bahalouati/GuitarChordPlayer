#!/usr/bin/env python3
"""Makes pop songs with known chords and beats, for testing chord detection
(scripts/check_detection.py): drums, bass, piano / guitar / pad accompaniment, and a
formant-synthesised singing voice whose tune uses notes outside the chord, vibrato and slides.
Some songs drift in tempo (no click track) or are slightly out of tune. Needs numpy, scipy and
ffmpeg.

usage: make_test_song.py OUTDIR COUNT [FIRST_SEED]
Writes OUTDIR/pNN.mp3 and OUTDIR/pNN.json (bpm, beats, chords with start/end times).
Set NOVOX=1 to leave out the voice.
tests/data/sung_pop_drifting_tempo.* is seed 3, re-encoded as 48 kbit/s mono.
"""
import json, os, subprocess, sys
import numpy as np
from scipy.signal import lfilter, fftconvolve

SR = 44100
NAMES = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'G#', 'A', 'Bb', 'B']
QUAL = {'': [0, 4, 7], 'm': [0, 3, 7], '7': [0, 4, 7, 10], 'm7': [0, 3, 7, 10], 'maj7': [0, 4, 7, 11],
        'sus4': [0, 5, 7], 'sus2': [0, 2, 7], 'dim': [0, 3, 6], 'aug': [0, 4, 8]}
MAJOR = [0, 2, 4, 5, 7, 9, 11]
MINOR = [0, 2, 3, 5, 7, 8, 10]

# progressions as (semitones above key, quality) per chord
PROG_MAJ = [
    [(0, ''), (7, ''), (9, 'm'), (5, '')], [(9, 'm'), (5, ''), (0, ''), (7, '')], [(0, ''), (5, ''), (7, ''), (5, '')],
    [(0, ''), (9, 'm'), (5, ''), (7, '')], [(2, 'm'), (7, ''), (0, ''), (0, '')], [(5, ''), (7, ''), (4, 'm'), (9, 'm')],
    [(0, ''), (4, 'm'), (5, ''), (7, '')], [(0, ''), (10, ''), (5, ''), (0, '')], [(5, ''), (0, ''), (7, ''), (9, 'm')],
    [(0, ''), (2, ''), (5, ''), (0, '')], [(9, 'm'), (2, 'm'), (7, ''), (0, '')], [(0, ''), (5, ''), (5, 'm'), (0, '')],
]
PROG_MIN = [
    [(0, 'm'), (8, ''), (3, ''), (10, '')], [(0, 'm'), (10, ''), (8, ''), (7, '')], [(0, 'm'), (5, 'm'), (7, ''), (0, 'm')],
    [(0, 'm'), (8, ''), (10, ''), (0, 'm')], [(0, 'm'), (3, ''), (10, ''), (5, 'm')], [(8, ''), (10, ''), (0, 'm'), (0, 'm')],
]
EXT = {'': ['7', 'maj7', 'sus4', 'sus2'], 'm': ['m7'], }


def midi_hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


class Song:
    def __init__(self, seed, drift=False, detune=0.0):
        self.r = np.random.default_rng(seed)
        r = self.r
        self.minor = r.random() < 0.3
        self.key = int(r.integers(12))
        self.bpm = float(r.uniform(72, 148))
        self.bpb = 3 if r.random() < 0.1 else 4
        self.detune = detune
        self.drift = drift
        form = ['intro', 'verse', 'chorus', 'verse', 'chorus', 'bridge', 'chorus', 'outro']
        lens = {'intro': 4, 'verse': 8, 'chorus': 8, 'bridge': 4, 'outro': 2}
        progs = {}
        for sec in set(form):
            base = PROG_MIN if self.minor else PROG_MAJ
            p = [list(c) for c in base[r.integers(len(base))]]
            for c in p:
                if c[1] in EXT and r.random() < 0.22:
                    c[1] = str(r.choice(EXT[c[1]]))
                elif c[1] == '' and (c[0] - (7)) % 12 == 0 and r.random() < 0.35:
                    c[1] = '7'  # dominant seventh on V
            progs[sec] = p
        # chord per bar or two per bar
        self.bars = []  # list of list of (beats, chordname)
        self.sections = []
        for sec in form:
            p = progs[sec]
            two = r.random() < 0.25 and self.bpb == 4
            nb = lens[sec]
            self.sections.append((sec, len(self.bars), nb))
            for b in range(nb):
                if two:
                    c1 = p[(2 * b) % 4]; c2 = p[(2 * b + 1) % 4]
                    self.bars.append([(2, c1), (2, c2)])
                elif sec == 'outro' and b == nb - 1:
                    self.bars.append([(self.bpb, (0, 'm' if self.minor else ''))])
                else:
                    self.bars.append([(self.bpb, p[b % 4])])
        nbeats = len(self.bars) * self.bpb + 1
        # beat times, optional slow tempo drift (+-4 %)
        T = 60.0 / self.bpm
        if drift:
            x = np.arange(nbeats)
            dev = 0.03 * np.sin(2 * np.pi * x / r.uniform(40, 90) + r.uniform(0, 6)) + 0.015 * np.sin(2 * np.pi * x / 17)
            ioi = T * (1 + dev)
        else:
            ioi = np.full(nbeats, T)
        self.start = 0.5
        self.beats = self.start + np.concatenate([[0], np.cumsum(ioi)])
        self.length = self.beats[-1] + 2.5
        self.n = int(self.length * SR)

    def beat_time(self, b):
        i = int(np.floor(b)); fr = b - i
        i = min(i, len(self.beats) - 2)
        return self.beats[i] + fr * (self.beats[i + 1] - self.beats[i])

    def chords(self):
        """(start beat, beats, root pc, quality, name)"""
        out = []
        b = 0.0
        for bar in self.bars:
            for beats, (deg, q) in bar:
                root = (self.key + deg) % 12
                out.append((b, beats, root, q, NAMES[root] + q))
                b += beats
        return out

    def scale(self):
        return [(self.key + s) % 12 for s in (MINOR if self.minor else MAJOR)]

    def hz(self, m):
        return midi_hz(m + self.detune / 100.0)


# ---------------- instruments
def env_adsr(n, a, d, s, rel, sus_len):
    a = max(1, int(a * SR)); d = max(1, int(d * SR)); rel = max(1, int(rel * SR))
    sus = max(0, int(sus_len * SR) - a - d)
    e = np.concatenate([np.linspace(0, 1, a), np.linspace(1, s, d), np.full(sus, s), np.linspace(s, 0, rel)])
    return e[:n] if len(e) >= n else np.concatenate([e, np.zeros(n - len(e))])


def add(buf, sig, t):
    i = int(t * SR)
    if i >= len(buf):
        return
    if i < 0:
        sig = sig[-i:]; i = 0
    m = min(len(sig), len(buf) - i)
    buf[i:i + m] += sig[:m]


def piano_note(f, dur, vel, r):
    n = int((dur + 0.6) * SR)
    t = np.arange(n) / SR
    y = np.zeros(n)
    B = 0.0004
    for k in range(1, 16):
        fk = f * k * np.sqrt(1 + B * k * k)
        if fk > 9000:
            break
        amp = (1.0 / k ** 1.1) * (1 if k % 7 else 0.3)
        dec = 1.5 + 0.9 * k + f / 300
        y += amp * np.sin(2 * np.pi * fk * t + r.uniform(0, 6)) * np.exp(-dec * t * 0.35)
    rel = np.ones(n)
    k0 = int(dur * SR)
    if k0 < n:
        rel[k0:] = np.exp(-np.arange(n - k0) / (0.08 * SR))
    atk = np.minimum(1, t / 0.004)
    return vel * y * rel * atk * 0.25


def ks_pluck(f, dur, vel, r, bright=0.5):
    """Karplus-Strong string via lfilter."""
    N = int(SR / f)
    n = int((dur + 0.3) * SR)
    exc = np.zeros(n)
    burst = r.uniform(-1, 1, N)
    burst = lfilter([1 - bright], [1, -bright], burst)
    exc[:N] = burst
    g = 0.996 ** (440 / max(f, 60))
    a = np.zeros(N + 2); a[0] = 1; a[N] = -g * 0.5; a[N + 1] = -g * 0.5
    y = lfilter([1], a, exc)
    k0 = int(dur * SR)
    if k0 < n:
        y[k0:] *= np.exp(-np.arange(n - k0) / (0.03 * SR))
    return vel * y * 0.35


def pad_chord(freqs, dur, vel, r):
    n = int((dur + 0.4) * SR)
    t = np.arange(n) / SR
    y = np.zeros(n)
    for f in freqs:
        for det in (-0.08, 0.08):
            ff = f * 2 ** (det / 12)
            ph = r.uniform(0, 1)
            saw = 2 * ((ff * t + ph) % 1) - 1
            y += saw
    y = lfilter([0.08], [1, -0.92], y)
    y = lfilter([0.15], [1, -0.85], y)
    return vel * y * env_adsr(n, 0.25, 0.3, 0.8, 0.4, dur) * 0.5 / len(freqs)


def bass_note(f, dur, vel, r):
    n = int((dur + 0.15) * SR)
    t = np.arange(n) / SR
    y = np.sin(2 * np.pi * f * t) + 0.5 * np.sin(4 * np.pi * f * t) + 0.25 * np.sin(6 * np.pi * f * t) + 0.12 * np.sin(8 * np.pi * f * t)
    return vel * y * env_adsr(n, 0.005, 0.2, 0.7, 0.06, dur) * 0.5


def kick(r):
    n = int(0.35 * SR); t = np.arange(n) / SR
    f = 50 + 110 * np.exp(-t * 30)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 9) * 1.1


def snare(r):
    n = int(0.25 * SR); t = np.arange(n) / SR
    nz = r.uniform(-1, 1, n)
    nz = lfilter([1, -1], [1, -0.6], nz)
    return (0.6 * nz * np.exp(-t * 18) + 0.5 * np.sin(2 * np.pi * 185 * t) * np.exp(-t * 25)) * 0.8


def hat(r, open_=False):
    n = int((0.25 if open_ else 0.06) * SR); t = np.arange(n) / SR
    nz = r.uniform(-1, 1, n)
    nz = lfilter([1, -2, 1], [1, 0.3, 0.1], nz) * 0.25
    return nz * np.exp(-t * (12 if open_ else 70)) * 0.35


def crash(r):
    n = int(1.6 * SR); t = np.arange(n) / SR
    nz = lfilter([1, -1], [1, 0.2], r.uniform(-1, 1, n))
    return nz * np.exp(-t * 2.5) * 0.18


# ---------------- singing voice (additive formant synthesis)
VOWELS = {  # F1, F2, F3 (Hz), bandwidths
    'a': (800, 1150, 2800), 'e': (400, 2000, 2600), 'i': (300, 2300, 3000),
    'o': (450, 800, 2830), 'u': (350, 600, 2700)}
BW = (80, 90, 120)


def formant_gain(f, F):
    g = np.zeros_like(f)
    for Fi, B, amp in zip(F, BW, (1.0, 0.6, 0.35)):
        g += amp / np.sqrt(1 + ((f - Fi) / B) ** 2)
    return g + 0.02


def sing_phrase(notes, song, r, gender_shift=0):
    """notes: list of (t_start, t_end, midi). Returns (signal, start time)."""
    t0 = notes[0][0] - 0.05
    t1 = notes[-1][1] + 0.25
    n = int((t1 - t0) * SR)
    ctl = 64
    nc = n // ctl + 2
    tc = t0 + np.arange(nc) * ctl / SR
    # pitch contour (in midi) with portamento, scoops and vibrato
    pitch = np.zeros(nc)
    amp = np.zeros(nc)
    vow = np.zeros((nc, 3))
    for i, (a, b, m) in enumerate(notes):
        sel = (tc >= a) & (tc < b)
        tt = tc[sel] - a
        prev = notes[i - 1][2] if i > 0 and notes[i - 1][1] > a - 0.05 else m - r.choice([1, 2, 0.5])
        glide = r.uniform(0.03, 0.09)
        base = m + (prev - m) * np.exp(-tt / glide) + r.normal(0, 0.08)
        vib_depth = r.uniform(0.25, 0.6) if b - a > 0.35 else 0.1
        vib = vib_depth * np.sin(2 * np.pi * r.uniform(5, 6.5) * tt) * np.clip((tt - 0.15) / 0.2, 0, 1)
        pitch[sel] = base + vib
        e = np.clip(tt / 0.03, 0, 1) * np.clip((b - a - tt) / 0.06, 0.05, 1)
        amp[sel] = e * r.uniform(0.7, 1.0)
        v = VOWELS[r.choice(list(VOWELS))]
        vow[sel] = v
    # fill gaps between notes in a phrase (legato, keep last pitch, silence)
    last = notes[0][2]
    for k in range(nc):
        if pitch[k] == 0:
            pitch[k] = last
            vow[k] = vow[k - 1] if k else VOWELS['a']
        else:
            last = pitch[k]
    amp = lfilter([0.3], [1, -0.7], amp)
    vow = lfilter([0.15], [1, -0.85], vow, axis=0)
    f0c = np.array([song.hz(p) for p in pitch])
    f0 = np.interp(np.arange(n) / ctl, np.arange(nc), f0c)
    ph = 2 * np.pi * np.cumsum(f0) / SR
    y = np.zeros(n)
    K = int(5000 / f0c.min())
    for k in range(1, K + 1):
        fk = f0c * k
        g = formant_gain(fk, vow.T) * (1.0 / k ** 0.7) * (fk < 6000)
        gs = np.interp(np.arange(n) / ctl, np.arange(nc), g * amp)
        y += gs * np.sin(k * ph)
    # breath noise and consonants
    nz = r.normal(0, 1, n)
    nz = lfilter([1, -0.9], [1], nz)
    ampn = np.interp(np.arange(n) / ctl, np.arange(nc), amp)
    y += 0.03 * nz * ampn
    for a, b, m in notes:
        if r.random() < 0.6:
            k = int((a - t0) * SR)
            L = int(0.04 * SR)
            if k + L < n:
                y[k:k + L] += 0.25 * lfilter([1, -1], [1], r.normal(0, 1, L)) * np.linspace(1, 0, L)
    return y * 0.12, t0


def melody(song, r):
    """Vocal phrases following the chords."""
    chords = song.chords()
    scale = song.scale()
    low = 57 + (r.integers(-3, 4))
    phrases = []
    cur = []
    center = low + 7
    prev = center
    for sec, first, nb in song.sections:
        if sec in ('intro', 'outro'):
            continue
        b = first * song.bpb
        end = (first + nb) * song.bpb
        while b < end:
            # a phrase of 2 bars then a rest
            plen = 2 * song.bpb
            pos = b + r.choice([0, 0.5, 1])
            notes = []
            while pos < min(b + plen - 0.5, end):
                dur = r.choice([0.5, 0.5, 1, 1, 1.5, 2, 0.25])
                ch = next(c for c in chords if c[0] <= pos < c[0] + c[1])
                tones = [(ch[2] + i) % 12 for i in QUAL[ch[3]]]
                strong = abs(pos - round(pos)) < 1e-6
                if strong and r.random() < 0.72:
                    cands = tones
                elif r.random() < 0.5:
                    cands = [p for p in scale if p not in tones]
                else:
                    cands = scale
                # nearest candidate to prev with a random step
                target = prev + r.integers(-4, 5)
                best = None
                for m in range(low, low + 17):
                    if m % 12 in cands:
                        d = abs(m - target)
                        if best is None or d < best[0]:
                            best = (d, m)
                m = best[1]
                t_a = song.beat_time(pos)
                t_b = song.beat_time(min(pos + dur, end)) - 0.03
                notes.append((t_a, t_b, m))
                prev = m
                pos += dur
            if notes:
                phrases.append(notes)
            b += plen
    return phrases


def render(song, path):
    r = song.r
    n = song.n
    mix = {k: np.zeros(n) for k in ('drums', 'bass', 'keys', 'gtr', 'pad', 'vox', 'lead')}
    chords = song.chords()
    has = {'piano': r.random() < 0.6, 'gtr': r.random() < 0.6, 'pad': r.random() < 0.5, 'drums': r.random() < 0.9}
    if not (has['piano'] or has['gtr']):
        has['piano'] = True
    style = r.choice(['block', 'eighths', 'arp'])
    strum = r.choice(['DDUUDU', 'DD', 'DUDU'])
    for (b0, beats, root, q, name) in chords:
        tones = QUAL[q]
        t_a = song.beat_time(b0)
        t_b = song.beat_time(b0 + beats)
        # voicing around C4
        base = 48 + ((root - 48) % 12)
        if base > 55:
            base -= 12
        vo = sorted(set([base + 12 + i for i in tones] + [base + i for i in tones[:2]] + [base + 24]))
        if has['piano']:
            if style == 'block':
                for m in vo:
                    add(mix['keys'], piano_note(song.hz(m), t_b - t_a, 0.5, r), t_a)
            elif style == 'eighths':
                k = 0.0
                while k < beats:
                    ta = song.beat_time(b0 + k)
                    tb = song.beat_time(b0 + k + 0.5)
                    for m in vo[1:]:
                        add(mix['keys'], piano_note(song.hz(m), tb - ta, 0.35 if k % 1 else 0.5, r), ta)
                    k += 0.5
            else:
                k = 0.0
                i = 0
                while k < beats:
                    ta = song.beat_time(b0 + k)
                    m = vo[i % len(vo)]
                    add(mix['keys'], piano_note(song.hz(m), 0.6, 0.5, r), ta)
                    k += 0.5; i += 1
        if has['gtr']:
            # open-position-ish voicing: 5-6 notes E2..E4
            gv = []
            for m in range(40, 66):
                if (m - root) % 12 in tones:
                    gv.append(m)
            gv = gv[-6:] if len(gv) > 6 else gv
            pattern = {'DDUUDU': [0, 1, 1.5, 2.5, 3, 3.5], 'DD': [0, 2], 'DUDU': [0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5]}[strum]
            for p in pattern:
                if p >= beats:
                    continue
                ta = song.beat_time(b0 + p)
                up = (p % 1) != 0
                order = gv[::-1] if up else gv
                for j, m in enumerate(order[:5] if up else order):
                    add(mix['gtr'], ks_pluck(song.hz(m), max(0.2, song.beat_time(b0 + beats) - ta), 0.5, r), ta + j * 0.008)
        if has['pad']:
            add(mix['pad'], pad_chord([song.hz(base + 12 + i) for i in tones], t_b - t_a, 0.5, r), t_a)
        # bass: root, sometimes fifth / approach
        k = 0.0
        while k < beats:
            ta = song.beat_time(b0 + k)
            step = 1.0 if song.bpm > 110 else 0.5
            m = 28 + ((root - 28) % 12)
            if r.random() < 0.15:
                m += 7
            elif r.random() < 0.08:
                m += tones[1]
            dur = song.beat_time(b0 + k + step) - ta - 0.02
            add(mix['bass'], bass_note(song.hz(m), dur, 0.7 if k == 0 else 0.55, r), ta)
            k += step
    # drums
    if has['drums']:
        K, S, H, Ho, C = kick(r), snare(r), hat(r), hat(r, True), crash(r)
        half = r.random() < 0.2
        total = len(song.bars) * song.bpb
        for sec, first, nb in song.sections:
            add(mix['drums'], C, song.beat_time(first * song.bpb))
        for beat in range(total):
            bi = beat % song.bpb
            tb = song.beat_time(beat)
            if bi == 0 or (bi == 2 and not half and song.bpb == 4) or (r.random() < 0.15):
                add(mix['drums'], K, tb)
            if (bi in (1, 3) and not half) or (half and bi == 2):
                add(mix['drums'], S, tb)
            add(mix['drums'], H, tb)
            add(mix['drums'], H * 0.6, song.beat_time(beat + 0.5))
            if r.random() < 0.1:
                add(mix['drums'], Ho, song.beat_time(beat + 0.5))
    # vocals
    for ph in ([] if os.environ.get("NOVOX") else melody(song, r)):
        y, t0 = sing_phrase(ph, song, r)
        add(mix['vox'], y, t0)
    # levels (rms-normalised stems)
    lv = {'drums': 0.9, 'bass': 0.55, 'keys': 0.45, 'gtr': 0.45, 'pad': 0.3, 'vox': 0.8, 'lead': 0.4}
    out = np.zeros(n)
    for k, v in mix.items():
        rms = np.sqrt(np.mean(v ** 2)) + 1e-9
        if np.max(np.abs(v)) > 0:
            out += v / rms * lv[k] * 0.1 * (1 + 0.3 * r.normal())
    # reverb
    ir_n = int(1.4 * SR)
    ir = r.normal(0, 1, ir_n) * np.exp(-np.arange(ir_n) / (0.35 * SR))
    ir = lfilter([0.3], [1, -0.7], ir)
    wet = fftconvolve(out, ir)[:n]
    out = out + 0.12 * wet / (np.sqrt(np.mean(wet ** 2)) + 1e-9) * np.sqrt(np.mean(out ** 2))
    # soft limiting
    out = np.tanh(out / (np.max(np.abs(out)) + 1e-9) * 2.0) * 0.8
    wav = path + '.wav'
    import wave
    with wave.open(wav, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((out * 32767).astype(np.int16).tobytes())
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', wav, '-b:a', '128k', path + '.mp3'], check=True)
    os.remove(wav)
    truth = {'bpm': song.bpm, 'bpb': song.bpb, 'key': NAMES[song.key] + ('m' if song.minor else ''),
             'detune': song.detune, 'drift': song.drift, 'beats': list(map(float, song.beats)),
             'chords': [[float(song.beat_time(b0)), float(song.beat_time(b0 + beats)), name] for b0, beats, root, q, name in chords],
             'has': {k: bool(v) for k, v in has.items()}}
    json.dump(truth, open(path + '.json', 'w'))


if __name__ == '__main__':
    out = sys.argv[1]
    n = int(sys.argv[2])
    first = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    os.makedirs(out, exist_ok=True)
    for i in range(first, first + n):
        rr = np.random.default_rng(1000 + i)
        drift = rr.random() < 0.3
        detune = float(rr.uniform(-40, 40)) if rr.random() < 0.4 else 0.0
        s = Song(i, drift=drift, detune=detune)
        render(s, f'{out}/p{i:02d}')
        print('song', i, 'bpm %.0f' % s.bpm, 'key', NAMES[s.key] + ('m' if s.minor else ''), 'drift', drift, 'detune %.0f' % detune, flush=True)
