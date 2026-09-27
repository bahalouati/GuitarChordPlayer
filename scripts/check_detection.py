#!/usr/bin/env python3
"""Checks chord detection against test recordings with known chords and beats.

For every tests/data/NAME.mp3 that has a NAME.json next to it (chords and beat times, see
scripts/make_test_song.py), runs `GuitarChordPlayer --detect`, then measures
  - chords: share of the song's time where the detected chord has the right root and
    major/minor quality (the usual "majmin" score),
  - beats: share of the real beats with a detected beat within 70 ms (F-measure).
Fails if either is below the limits given, and renders the detected song to check it plays.

usage: check_detection.py APP [--min-chords 0.85] [--min-beats 0.90]
"""
import bisect, json, os, re, shutil, subprocess, sys, tempfile, xml.etree.ElementTree as ET

PC = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
MAJOR = {'', '7', 'maj7', '6', '9', 'add9', 'maj9'}
MINOR = {'m', 'm7', 'm6', 'm9'}


def parse(name):
    """Chord name -> (pitch class, 'maj' / 'min' / other quality), or (None, 'N')."""
    if name in ('N.C.', 'N'):
        return None, 'N'
    m = re.match(r'^([A-G])([#b]?)([^/]*)', name)
    root = (PC[m.group(1)] + {'#': 1, 'b': -1, '': 0}[m.group(2)]) % 12
    q = m.group(3)
    return root, 'maj' if q in MAJOR else 'min' if q in MINOR else q


def detected_segments(xml_path):
    """(start, end, chord) in seconds of the recording, and the beat times."""
    song = ET.parse(xml_path).getroot()
    bpb = int(song.get('beatsPerBar', '4'))
    capo = int(song.get('capo', '0'))
    period = 60.0 / float(song.get('bpm'))
    audio = song.find('audio')
    offset = float(audio.get('offset', '0'))
    beats_el = audio.find('beats')
    beats = [float(v) for v in beats_el.text.split()] if beats_el is not None else []
    bars = [b.strip().split() for line in song.iter('bars') for b in line.text.split('|') if b.strip()]
    total = len(bars) * bpb + 1
    if not beats:
        beats = [offset + k * period for k in range(total)]
    while len(beats) < total:
        beats.append(beats[-1] + (beats[-1] - beats[-2] if len(beats) > 1 else period))

    def at(k):
        i = int(k)
        return beats[i] + (k - i) * (beats[i + 1] - beats[i]) if k != i else beats[i]

    segs = []
    for i, bar in enumerate(bars):
        lengths = []
        for c in bar:
            name, _, n = c.partition(':')
            lengths.append((name, float(n) if n else 0.0))
        fixed = sum(n for _, n in lengths)
        flexible = sum(1 for _, n in lengths if n == 0)
        beat = i * bpb
        for name, n in lengths:
            n = n or (bpb - fixed) / flexible
            root, q = parse(name)
            segs.append((at(beat), at(beat + n), ((root + capo) % 12 if root is not None else None, q)))
            beat += n
    return segs, beats


def chord_score(truth, segs):
    right = total = 0
    t, end = truth[0][0], truth[-1][1]
    while t < end:
        ref = next((parse(c) for a, b, c in truth if a <= t < b), None)
        est = next((c for a, b, c in segs if a <= t < b), (None, 'N'))
        t += 0.01
        if ref is None or ref[1] not in ('maj', 'min'):
            continue
        total += 1
        right += ref == est
    return right / max(1, total)


def beat_score(truth, est, tol=0.07):
    est = sorted(t for t in est if truth[0] - 1 <= t <= truth[-1] + 1)
    used, hits = set(), 0
    for t in truth:
        i = bisect.bisect_left(est, t - tol)
        while i < len(est) and est[i] <= t + tol:
            if i not in used:
                used.add(i)
                hits += 1
                break
            i += 1
    p, r = hits / max(1, len(est)), hits / max(1, len(truth))
    return 2 * p * r / max(1e-9, p + r)


def main():
    args = sys.argv[1:]
    app = args[0]
    min_chords = float(args[args.index('--min-chords') + 1]) if '--min-chords' in args else 0.85
    min_beats = float(args[args.index('--min-beats') + 1]) if '--min-beats' in args else 0.90
    data = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tests', 'data')
    failed = False
    for name in sorted(os.listdir(data)):
        if not name.endswith('.json'):
            continue
        audio = os.path.join(data, name[:-5] + '.mp3')
        truth = json.load(open(os.path.join(data, name)))
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, 'detected.xml')
            subprocess.run([app, '--detect', audio, out], check=True)
            segs, beats = detected_segments(out)
            chords, beat_f = chord_score(truth['chords'], segs), beat_score(truth['beats'], beats)
            ok = chords >= min_chords and beat_f >= min_beats
            print(f'{name[:-5]}: chords {100 * chords:.1f}% (min {100 * min_chords:.0f}), '
                  f'beats {100 * beat_f:.1f}% (min {100 * min_beats:.0f}) -> {"ok" if ok else "FAILED"}')
            failed |= not ok
            # The detected song must load and play along with the recording (with its beat map
            # when the tempo moves).
            shutil.copy(audio, tmp)
            subprocess.run([app, '--render', out, os.path.join(tmp, 'detected.wav')], check=True)
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
