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
- the **lyrics**, if the song has them: the current line with the chords above the words and the current bar
  highlighted, plus the next line. During an intro the first line is shown early, with the number of bars left before singing starts.
- the **section** you are in (Intro, Verse 1/2, Chorus ...) and the bar number.

Practice tools: tempo slider (25-150 %, the pitch stays the same), loop the current section, metronome,
count-in, jump to any section, and export to WAV. Save a song file while the app is running and it
reloads automatically, which makes writing your own songs quick.

**Play along with the real song.** A song can have its recording (MP3, M4A, OGG, FLAC, WAV...) attached. The app plays
it in sync with the guitar, through looping, jumping and the count-in. The tempo slider slows the recording down
without changing its pitch. The mixer switches the guitar and the recording on and off separately and sets their
volumes. **Sync** nudges the recording a few milliseconds earlier or later if needed, and is saved with the song.

**New song from an MP3.** *File → New song from MP3 (Ctrl+I)* listens to a recording and writes the song for you. It
finds the tempo, the beats, the bar lines (4/4 or 3/4) and the chords (major and minor), picks an easy capo position,
and saves the song with the recording attached so you can play along right away. On test recordings with drums,
bass and a melody it names the right chord about 93% of the time. Real songs vary, so fix any wrong chord by
ear in the live editor while it plays. *File → Attach recording to this song* adds a recording to an existing song.
It finds where bar 1 starts and offers the recording's tempo if it differs from the song's.

The guitar sound is generated in real time, so no audio files are needed and any chord or pattern you write just
plays. Each string is a physical model: two slightly detuned vibrating polarizations, frequency-dependent damping,
string stiffness on the wound strings, and a pick attack. The strums take a realistic time across the strings,
speed up as the pick moves, and vary in timing and strength like a human player. The sound gets guitar body
resonance and a small stereo room.

## Adding your own songs

**The quick way: File → New song (Ctrl+N)**, or click *+ Create a new song...* in the song list.
Fill in the title and tempo, then type the chords as a simple chord sheet:

```
[Intro] arpeggio
G | Cadd9 | Em | D

[Verse] folk x2
G | D | Em | C

[Chorus] drive 96bpm
C | G | D | Em

[Verse]
[Chorus] x2
```

- `[Name]` starts a section. After it you can add a **pattern** (from the list in the dialog), **`x2`** to
  repeat it, and **`96bpm`** to change the tempo.
- Bars are separated by `|`. Two chords in one bar split it (`Em C`), or set lengths in beats (`D:2 Dsus4:2`).
- To play a section again, write just its name (`[Verse]`).

Press *Create & Play*. The app saves the song as an XML file in **Documents\Guitar Chord Player\My Songs**
and starts playing it.

To add lyrics, put a line of words directly under the chords it goes with, with `|` where each new bar starts:

```
[Verse 1] folk
G | D | Em | C
Here are the | words for the | first | line
```

**Let an LLM write it:** click **Copy LLM prompt** in the New Song dialog (or *Help → Copy LLM prompt*),
paste it into ChatGPT, Claude, Gemini or another LLM, and fill in the song name. Add lyrics you have and it lines them
up with the chords. Copy the whole answer and click **Paste LLM answer** (or *File → New song from LLM answer*,
Ctrl+Shift+V). The song is saved and starts playing. The prompt is also in
[`docs/LLM_PROMPT.md`](docs/LLM_PROMPT.md).

**Changing a song: Edit song (Ctrl+E)** opens the song's XML in a built-in editor. It checks what you type
and shows problems in red. With **Live** ticked (the default), every valid change plays straight away
without stopping or losing your place, so you can fix a chord or a lyric while the song keeps playing.
**Ctrl+S** saves. **Click a lyric line** in the player to jump to it in the editor with its words selected.
*Insert* adds snippets (section, bars, lyric line, pattern, chord fingering). If a song file has a mistake, the app says what's wrong and opens
it in the editor.

**Other ways to add songs:**

- Drag and drop `.xml` files onto the window. They are copied into *My Songs*.
- Put files in the *My Songs* folder yourself (*File → Open My Songs folder*). The list updates automatically.
- Edit in any text editor you like. Saving the file reloads it in the app.

**Chords:** any common chord name works. Around 40 open shapes are built in, and other chords (`F#m7`,
`Ebmaj7`, `C#9`, `Bbsus4`, `Amin`, `A#m` …) get a barre shape worked out automatically. Use
**Tools → Chord finder (Ctrl+K)** to check a chord's fingering and hear it strummed. For a fingering of your own,
add `<chord name="..." frets="x32010"/>` to the song.

**Patterns:** these built-in patterns can be used by name without defining them:

| Name | Steps | Use |
|---|---|---|
| `folk` | `D - D U - U D U` | the classic strum (default for 4/4) |
| `pop` | `D - D U D U D U` | busier eighths |
| `rock` | `D D D D D D D D` | driving down-strums |
| `drive` | `>D - D U X U D U` | accent + muted chuck |
| `ballad` | `D - - - D - D U` | slow songs |
| `whole` / `half` / `quarters` | | 1, 2 or 4 strums per bar |
| `reggae` | `- u - u - u - u` | off-beat |
| `country` | `B - d u A - d u` | bass-strum |
| `sixteenths` | `>D - d u X - u d - u d u X - d u` | funky 16ths |
| `arpeggio` / `arpeggio-slow` | `B 3 2 3 1 3 2 3` / `B 3 2 1` | picked chords |
| `travis` | `B+1 3 A 2 B 3 A 2` | fingerpicking |
| `waltz` / `waltz-pick` | `B - d u d u` / `B 3 2 1 2 3` | 3/4 (default for 3/4) |
| `six-eight` / `six-eight-pick` | `D - U D - U` / `B 3 2 1 2 3` | 6/8 (default for 6/8) |

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
| Ctrl+N | New song |
| Ctrl+E | Show / hide the song editor |
| Ctrl+S | Save and replay (in the editor) |
| Ctrl+K | Chord finder |
| Ctrl+Shift+V | New song from an LLM answer on the clipboard |
| Ctrl+I | New song from an MP3 (detect chords) |
| Ctrl+Shift+L | Add lyrics to the current song |

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

**Don't want to build it?** Download **`GuitarChordPlayer-Setup.exe`** from the
[latest release](https://github.com/bahalouati/GuitarChordPlayer/releases/latest) and run it. It installs for your
user (no admin rights needed), adds Start menu and optional desktop shortcuts, and has an uninstaller.
There's also a portable `GuitarChordPlayer-windows.zip`: unzip it anywhere you can write to (e.g. Documents or Desktop,
not Program Files) and run `GuitarChordPlayer.exe`.

### Updates

Every push to the main branch is built by GitHub Actions and published as a release (`build-<number>`).
The app checks for a newer release when it starts (at most every 12 hours; switch this off under *Help*),
and *Help → Check for updates* checks right away. When there is a new build, a green **Update available** button
appears in the status bar. *Install and restart* downloads the new version, replaces the app's files and starts it
again. Your own songs in *Documents\Guitar Chord Player* are not touched. Automatic install is Windows only. On
Linux, `git pull` and rebuild.

Linux: `sudo apt install qt6-base-dev qt6-multimedia-dev`, then run the same two `cmake` commands.

Command line, without the GUI:

- `GuitarChordPlayer --render songs/amazing_grace.xml out.wav` exports a song (with its recording, if it has one).
- `GuitarChordPlayer --detect song.mp3 song.xml` detects the chords of a recording and writes a song file.

## Song file format

This is the file format that *New song* writes and the editor edits. Here is a complete example:

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
| `pattern` | Pattern for sections that don't name one | `folk` |
| `tuning` | Six notes, low to high, e.g. `"D2 A2 D3 G3 B3 E4"` for drop D | standard |

### `<chord>`

`frets` lists the strings **from low E to high e**, as in normal chord charts: `x` = don't play,
`0` = open, a number = fret. For frets above 9, separate the values with spaces: `frets="x 10 12 12 12 10"`.
`fingers` (optional) gives the finger numbers in the same order: `1`-`4`, `T` = thumb, `0` = none.
When one finger covers several strings on the same fret, the diagram draws a barre.

Chord names not in this list are worked out as barre chords (roots A-G with `#`/`b`, qualities
`m 7 m7 maj7 sus2 sus4 5 6 m6 9 dim aug`, plus spellings like `min`, `-`, `M7`, and slash chords).
Built-in shapes: C, Cmaj7, C7, Cadd9, Cm, C#m, D, Dm, D7, Dm7, Dsus2, Dsus4, D/F#, E, Em, E7, Em7, Esus4,
E5, F, Fm, Fmaj7, F#, F#m, G, G7, G/B, Gm, Gsus4, G#m, A, Am, A7, Am7, Asus2, Asus4, A5, Bb, B, Bm, B7.

### `<pattern>`

You only need this for patterns of your own. The built-in ones listed above work by name.
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

Attributes: `name`, plus optional `pattern` (defaults to the song's), `bpm` and `beatsPerBar` for that
section only.

- `<bars>G | D | Em C | C</bars>`: bars are separated by `|`. Chords in the same bar split it evenly.
  Without any `|`, each chord is one bar: `<bars>G D Em C</bars>`.
- `C:3 G:1`: set how many beats each chord lasts.
- `%` repeats the previous chord. `N.C.` means no chord (strums are silent, picks are skipped).
- `repeat="2"` on `<bars>` repeats those bars. `<bar>C G</bar>` adds exactly one bar.

### Adding lyrics to an existing song

*Tools → Add lyrics to this song (Ctrl+Shift+L)*: paste the words, one sung line per line, with an empty line between
verses. The app spreads them over the song's bars. It skips an intro and guesses how many bars each line lasts, and a
live preview shows which chords each line gets. Change *Singing starts at bar*, *Bars per lyric line* and the pause
between verses until it fits, then play the song and fix single lines in the editor. The Spacetoon and other example
songs get a copy in My Songs, so updates never overwrite your lyrics. For songs already in My Songs, the previous
version is kept as `.bak`. Arabic (and other right-to-left) lyrics are shown right to left.

The included songs have no lyrics because the words are copyrighted. Paste in lyrics you have yourself.

### Lyrics: `<line>`

Use `<line>` instead of `<bars>` for bars that have words:

```xml
<section name="Verse 1" pattern="waltz">
  <line chords="G | G | C | G">A-mazing | grace, how | sweet the | sound that</line>
  <line chords="G | G | D | D">saved a | wretch like | me. | I</line>
</section>
```

`chords` uses the same syntax as `<bars>`. The lyrics are split with `|`, one piece per bar: the words sung
while that bar plays (a piece can be empty). Without any `|`, the whole line is shown with a progress bar
under it. Verses with different words need their own sections (`Verse 1`, `Verse 2`). *Amazing Grace* in the
examples shows how it works.

### Recording: `<audio>`

```xml
<audio file="my song.mp3" offset="0.350"/>
```

`file` is relative to the song file (or a full path). `offset` is where bar 1 starts in the recording, in seconds.
It can be negative if the song starts before the recording. The Sync control in the mixer changes it for you.
Keep the song's tempo steady (one `bpm`) for songs with a recording. Sections with a different `bpm` work too,
as long as the recording really changes tempo there.

### `<arrangement>`

The order the sections are played in: `<play section="Chorus" repeat="2"/>`. If you leave it out, the
sections play once each, in file order.

When a string's fret changes at a chord change, the app stops that string ringing, the way lifting a
finger would.

## Included songs

- *House of the Rising Sun* (traditional): 6/8 arpeggios
- *Amazing Grace* (traditional): 3/4 waltz strum and picking, with lyrics
- *Pop Practice Song*: five sections with different strums, sixteenths, accents, chucks and tempo changes
- *Travis Picking Study*: alternating-bass fingerpicking
- **Spacetoon** (`songs/spacetoon/`): 51 songs from the Spacetoon opening themes playlist.
  - 11 are arranged from human-written chord charts on Guitarabia and Ultimate Guitar (Remi, Hunter x Hunter,
    Ahd Al Asdiqaa, Hazim Al Raad, Ana wa Akhi, Sabiq wa Lahiq, Digimon, Treasure Island, Nawar, Asrar Al Muhit,
    Omi Kam Ahwaha). The chords are reliable, but bar lengths and tempos are estimates.
  - 40 were generated from the chords that [ChordU](https://chordu.com) detected automatically in the YouTube recordings
    (Captain Majid, Grendizer, Slam Dunk, Shoot!, Mowgli, Babar, Tiger Mask, ...). They follow the recording's form
    and tempo, use major/minor chords only, and a capo where it makes the shapes easier. Some chords will be off; fix
    them by ear with the live editor. Each file names the video it came from.
  - None of them include lyrics (they are copyrighted). Paste your own with *Tools → Add lyrics to this song*.

## Code layout

| File | Role |
|---|---|
| `src/Song.*` | XML parsing, validation, built-in patterns, flattening into a playable `Timeline` |
| `src/ChordSheet.*` | Plain-text chord sheet → song XML (New Song dialog) |
| `src/NewSongDialog.*`, `src/SongEditor.*`, `src/ChordFinderDialog.*` | Song creation and editing tools |
| `src/LyricsWidget.*` | Lyrics view (left-to-right and right-to-left) |
| `src/LyricsAligner.*`, `src/LyricsDialog.*` | Spreading pasted lyrics over a song's bars |
| `src/Updater.*` | Checks GitHub releases and installs updates |
| `src/AudioTrack.*` | Decoding recordings (Qt Multimedia) and WSOLA time stretching |
| `src/ChordDetector.*` | Beat tracking, chroma and chord recognition for *New song from MP3* |
| `installer.iss`, `resources/` | Windows installer (Inno Setup), icon and version info |
| `src/LlmPrompt.*`, `docs/LLM_PROMPT.md` | The LLM prompt (embedded in the app) and reading LLM answers |
| `src/ChordLibrary.*` | Built-in chord shapes, name normalisation, generated barre chords |
| `src/GuitarSynth.*` | Physically modelled strings, pick noise, body resonance, room reverb, metronome |
| `src/Sequencer.*` | Sample-accurate playback: patterns → strums/picks, recording sync, loop, count-in |
| `src/AudioEngine.*` | `QAudioSink` in pull mode; converts output latency into what you actually hear |
| `src/ChordDiagramWidget.*`, `src/PatternWidget.*` | Custom-painted views |
| `src/MainWindow.*` | UI, transport, file watching |
