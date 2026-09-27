# Guitar Chord Player

A Qt 6 / C++ app that **plays songs on a synthesized guitar** so you can see and hear how they're strummed
and picked. Each song is a small XML file with its chords, strumming/picking patterns, tempo, and
sections (intro, verse, chorus, ...).

While a song plays you see:

- **the current chord** as a chord-box diagram with finger numbers, barres, and open/muted strings.
  Each string **lights up and vibrates as it is plucked**, so you can follow the strumming hand.
- **the next chord**, plus how many beats until the change.
- **the strum/pick pattern** for this bar and the next (`↓ ↑ ✕ B 3 2 1 ...`) with the beat count
  (`1 & 2 & ...`) and a moving highlight.
- the **section** you are in (Intro, Verse 1/2, Chorus ...) and the bar number.

Practice tools: tempo slider (25-150 %, the pitch stays the same), loop the current section, metronome,
count-in, jump to any section, and export to WAV. Save a song file while the app is running and it
reloads automatically, which makes writing your own songs quick.

The guitar sound is generated in real time (Karplus-Strong plucked-string synthesis with a simple body
resonance), so no audio files are needed and any chord or pattern you write just plays.

## Keyboard

| Key | Action |
|---|---|
| Space | Play / pause |
| Esc | Stop (back to the start) |
| Left / Right | Previous / next section |
| Up / Down | Tempo +5 % / -5 % |
| 0 | Reset tempo to 100 % |
| L | Loop current section |
| M | Metronome |
| F5 | Reload song |

## Building on Windows (MSVC)

1. Install **Visual Studio 2022** (Community is fine) with the "Desktop development with C++" workload.
2. Install Qt with the [Qt Online Installer](https://www.qt.io/download-qt-installer). Pick
   **Qt 6.5 or newer → MSVC 2022 64-bit**, and under *Additional Libraries* tick **Qt Multimedia**.
3. Either:
   - **Qt Creator:** *File → Open File or Project* → `CMakeLists.txt`, choose the MSVC kit, press Run, or
   - **Command line** (in "x64 Native Tools Command Prompt for VS 2022"):
     ```bat
     cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\msvc2022_64
     cmake --build build --config Release
     C:\Qt\6.8.0\msvc2022_64\bin\windeployqt.exe build\Release\GuitarChordPlayer.exe
     build\Release\GuitarChordPlayer.exe
     ```

The `songs/` folder is copied next to the executable after every build.

**Don't want to build it?** Every push runs GitHub Actions, which builds a ready-to-run Windows version.
Open the repository's *Actions* tab, click the latest `build` run, and download the
`GuitarChordPlayer-windows` artifact.

Linux: `sudo apt install qt6-base-dev qt6-multimedia-dev`, then run the same two `cmake` commands.

Command-line export without the GUI: `GuitarChordPlayer --render songs/amazing_grace.xml out.wav`.

## Song file format

Put `.xml` files in the `songs/` folder (*File → Open songs folder*), or open one from anywhere with
*File → Open song*. Here is a complete example:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<song title="My Song" artist="Me" bpm="92" beatsPerBar="4" capo="0">

  <!-- Optional: chords not in the built-in library (or your own fingering). -->
  <chords>
    <chord name="Em7(open)" frets="022033" fingers="012034"/>
  </chords>

  <patterns>
    <pattern name="folk"  subdivision="2">D - D U - U D U</pattern>
    <pattern name="picky" subdivision="2">B 3 2 3 1 3 2 3</pattern>
    <pattern name="drive" subdivision="2">>D - D U X U D U</pattern>
  </patterns>

  <sections>
    <section name="Intro" pattern="picky" bpm="84">
      <bars>G | Cadd9 | Em7(open) | D</bars>
    </section>
    <section name="Verse" pattern="folk">
      <bars repeat="2">G | D | Em C | C</bars>
    </section>
    <section name="Chorus" pattern="drive" bpm="96">
      <bars>C | G | D:2 Dsus4:2 | Em</bars>
    </section>
  </sections>

  <arrangement>
    <play section="Intro"/>
    <play section="Verse"/>
    <play section="Chorus" repeat="2"/>
  </arrangement>
</song>
```

### `<song>`

| Attribute | Meaning | Default |
|---|---|---|
| `title`, `artist` | Shown in the app | file name |
| `bpm` | Tempo in beats per minute | 90 |
| `beatsPerBar` | 4 for 4/4, 3 for 3/4. For 6/8, use `2` with `subdivision="3"` patterns (bpm = dotted quarters) | 4 |
| `capo` | Capo fret. The sound is transposed and the shapes stay the same | 0 |
| `tuning` | Six notes, low to high, e.g. `"D2 A2 D3 G3 B3 E4"` for drop D | standard |

### `<chord>`

`frets` lists the strings **from low E to high e**, as in normal chord charts: `x` = don't play,
`0` = open, a number = fret. For frets above 9, separate the values with spaces: `frets="x 10 12 12 12 10"`.
`fingers` (optional) gives the finger numbers in the same order: `1`-`4`, `T` = thumb, `0` = none.
When one finger covers several strings on the same fret, the diagram draws a barre.

Built-in chords: C, Cmaj7, C7, Cadd9, Cm, C#m, D, Dm, D7, Dm7, Dsus2, Dsus4, D/F#, E, Em, E7, Em7, Esus4,
E5, F, Fm, Fmaj7, F#, F#m, G, G7, G/B, Gm, Gsus4, G#m, A, Am, A7, Am7, Asus2, Asus4, A5, Bb, B, Bm, B7.

### `<pattern>`

One token per step, separated by spaces. `subdivision` is the number of steps per beat
(2 = eighth notes, 4 = sixteenths, 3 = triplets / 6/8). A pattern shorter than a bar repeats, and a
longer one runs across bars. `|` can be used for readability and is ignored.

| Token | Meaning |
|---|---|
| `D` | Down-strum, all strings of the chord (low → high) |
| `U` | Up-strum, top 4 strings (high → low) |
| `d` / `u` | Light down / up strum (fewer strings, softer) |
| `X` | Muted "chuck" (percussive strum) |
| `-` or `.` | Rest. Strings keep ringing |
| `1` … `6` | Pick one string (1 = high e, 6 = low E) |
| `B` | Bass note: the lowest string of the chord |
| `A` | Alternate bass: the next bass string (the 5th of the chord when possible) |
| `B+1`, `3+2+1` | Pick several strings together (pinch) |
| `>` prefix | Accent, e.g. `>D` |

Examples: `D - D U - U D U` (the common folk strum), `B 3 2 3 1 3 2 3` (arpeggio),
`B+1 3 A 2 B 3 A 2` (Travis picking), `B - d u d u` (waltz in 3/4).

### `<section>` and bars

Attributes: `name`, `pattern`, plus optional `bpm` and `beatsPerBar` for that section only.

- `<bars>G | D | Em C | C</bars>`: bars are separated by `|`. Chords in the same bar split it evenly.
  Without any `|`, each chord is one bar: `<bars>G D Em C</bars>`.
- `C:3 G:1`: set how many beats each chord lasts.
- `%` repeats the previous chord. `N.C.` means no chord (strums are silent, picks are skipped).
- `repeat="2"` on `<bars>` repeats those bars. `<bar>C G</bar>` adds exactly one bar.

### `<arrangement>`

The order the sections are played in: `<play section="Chorus" repeat="2"/>`. If you leave it out, the
sections play once each, in file order.

When a string's fret changes at a chord change, the app stops that string ringing, the way lifting a
finger would.

## Included songs

- *House of the Rising Sun* (traditional): 6/8 arpeggios
- *Amazing Grace* (traditional): 3/4 waltz strum and picking
- *Pop Practice Song*: five sections with different strums, sixteenths, accents, chucks and tempo changes
- *Travis Picking Study*: alternating-bass fingerpicking

## Code layout

| File | Role |
|---|---|
| `src/Song.*` | XML parsing, validation, flattening into a playable `Timeline` |
| `src/ChordLibrary.*` | Built-in chord shapes |
| `src/GuitarSynth.*` | Karplus-Strong strings, body resonance, metronome click |
| `src/Sequencer.*` | Sample-accurate playback: patterns → strums/picks, loop, count-in, position history |
| `src/AudioEngine.*` | `QAudioSink` in pull mode; converts output latency into what you actually hear |
| `src/ChordDiagramWidget.*`, `src/PatternWidget.*` | Custom-painted views |
| `src/MainWindow.*` | UI, transport, file watching |
