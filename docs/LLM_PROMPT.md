# Prompt for making songs with an LLM

Copy the prompt below into ChatGPT, Claude, Gemini or another LLM, and fill in the song at the end.
You can also copy it from the app: **Help → Copy LLM prompt**, or the **Copy LLM prompt** button in the New Song dialog.
Save the answer as a `.xml` file in `Documents\Guitar Chord Player\My Songs`, or drag it onto the app window.
If something is wrong, the app says what and opens the song editor so you can fix it.

- **Lyrics:** paste lyrics you have after "Lyrics:" and the LLM lines them up with the chords, one piece per bar.
  With no lyrics pasted, you get the chords only.
- **Checking:** LLMs sometimes get the chords of real songs wrong. If something sounds off, compare with a chord
  site and fix it in the editor (Ctrl+E). With *Live* on, you hear each change as you type.
- **Steering:** add wishes after the song name, e.g. "beginner version, open chords only, fingerpicked intro"
  or "capo 2 like the original".

````text
You write song files for "Guitar Chord Player", an app that plays chords on a synthesized guitar
and shows the chords and lyrics while it plays.
Output ONLY one XML file in a single ```xml code block: no explanations before or after.

## File structure (only these tags exist; any other tag is an error)
<?xml version="1.0" encoding="UTF-8"?>
<song title="..." artist="..." bpm="92" beatsPerBar="4" capo="0" pattern="folk">
  <chords>   <!-- optional: only for chords that need a custom fingering -->
    <chord name="Cadd9" frets="x32033" fingers="021034"/>
  </chords>
  <patterns> <!-- optional: only for strum patterns that are not built in -->
    <pattern name="mystrum" subdivision="2">D - D U - U D U</pattern>
  </patterns>
  <sections>
    <section name="Intro" pattern="arpeggio">
      <bars>G | Cadd9 | Em | D</bars>
    </section>
    <section name="Verse 1" pattern="folk">
      <line chords="G | D | Em | C">Words sung in | bar two | bar three | bar four</line>
      <line chords="G | D | C | C">second lyric | line of the | verse |</line>
    </section>
    <section name="Chorus" pattern="drive" bpm="96">
      <line chords="C | G | D:2 Dsus4:2 | Em">chorus | words | go | here</line>
    </section>
  </sections>
  <arrangement>
    <play section="Intro"/>
    <play section="Verse 1"/>
    <play section="Chorus" repeat="2"/>
  </arrangement>
</song>

## <song> attributes
- title, artist: plain text; write & as &amp; and " as &quot;
- bpm: tempo in beats per minute
- beatsPerBar: 4 for 4/4, 3 for 3/4. For 6/8 use beatsPerBar="2" with a subdivision="3" pattern, and bpm counts dotted quarter notes.
- capo: capo fret (0 = none). Write chords as the SHAPES played with the capo on.
- pattern: default pattern for sections that don't name one.
- tuning (optional): six notes low to high, e.g. "D2 A2 D3 G3 B3 E4" for drop D. Leave it out for standard tuning.

## Sections and bars
- Every section needs a unique name. It can also have pattern="...", bpm="..." (tempo change) and beatsPerBar="...".
- <bars> holds chords without lyrics. Bars are separated by |, and chords in the same bar share it evenly ("Em C").
  Or give each chord's length in beats: "C:3 G:1". The lengths in a bar must not add up to more than beatsPerBar.
- "%" repeats the previous chord, and "N.C." means no chord (silence).
- repeat="2" on <bars> or <line> repeats it inside the section.
- A section can mix several <bars> and <line> elements; they play in order.
- <arrangement> lists the sections in playing order. <play section="..."> must match a section name exactly,
  and repeat="N" plays it N times. Reuse sections here instead of copying them.

## Lyrics
- If lyrics are given below, write every sung part with <line> instead of <bars>, one <line> per lyric line:
  <line chords="G | D | Em | C">Almost | heaven, West | Virginia, Blue | Ridge Mountains</line>
- The chords attribute uses the same syntax as <bars>. The lyric text is split with | into exactly one piece per bar:
  the words sung while that bar plays. A piece may be empty when nothing is sung, e.g. "last words | ".
  Put each word in the bar where its stressed syllable falls, so the chord changes land on the right words.
- A verse with different words needs its own section ("Verse 1", "Verse 2"), even when the chords are the same.
  A chorus with the same words can be one section played several times.
- Instrumental parts (intro, solo, outro) use <bars>.
- Use the given lyrics exactly as written; do not invent, complete or correct them.
  If no lyrics are given, use only <bars> (no <line>), except for public-domain songs.

## Chord names
Use standard names: a root A-G with an optional # or b, then one of:
(nothing), m, 7, m7, maj7, sus2, sus4, 5, 6, m6, 9, dim, aug, or a slash chord like G/B or D/F#.
These all work without defining them. Also built in: Cadd9 and D/F#.
Only for anything else (add9 on other roots, 11, 13, m7b5, unusual voicings) add
<chord name="..." frets="..." fingers="..."/> inside <chords>. In it:
- frets lists 6 strings from low E to high e: x = muted, 0 = open, a number = fret.
  If any fret is 10 or higher, separate the values with spaces: "x 10 12 12 11 10".
- fingers (optional) uses the same order: 1-4 = fingers, T = thumb, 0 = none.

## Patterns: one token per step, separated by spaces
subdivision = steps per beat (2 = eighth notes, 4 = sixteenths, 3 = triplets / 6/8).
Make each pattern exactly one bar long: beatsPerBar × subdivision tokens.
D = down-strum all strings | U = up-strum top strings | d / u = light strums
X = muted chuck | - = rest (the strings keep ringing)
1-6 = pick one string (1 = high e, 6 = low E) | B = bass note | A = alternate bass
B+1 or 3+2+1 = pinch several strings together | >D = accent (a leading > on any token)

Built-in patterns (use them by name, don't redefine them):
folk (D - D U - U D U), pop (D - D U D U D U), rock (D D D D D D D D),
drive (>D - D U X U D U), ballad (D - - - D - D U), whole (>D - - - - - - -),
half (D - - - D - - -), quarters (D - D - D - D -), reggae (- u - u - u - u),
country (B - d u A - d u), sixteenths (subdivision 4: >D - d u X - u d - u d u X - d u),
arpeggio (B 3 2 3 1 3 2 3), arpeggio-slow (subdivision 1: B 3 2 1),
travis (B+1 3 A 2 B 3 A 2),
waltz (3/4: B - d u d u), waltz-pick (3/4: B 3 2 1 2 3),
six-eight (6/8, subdivision 3: D - U D - U), six-eight-pick (6/8: B 3 2 1 2 3).
If a section has a different time signature from the song, its pattern must fit it.

## Guidelines
- Write the song's real structure (Intro, Verse, Pre-chorus, Chorus, Bridge, Solo, Outro) with the correct
  chords and number of bars.
- Choose patterns that match the song's feel: arpeggio or travis for fingerpicked songs, folk or pop
  for strummed ones, drive or rock for energetic parts, whole for endings.
  Put a short XML comment (<!-- ... -->) above a section when it helps a learner.
- Use the song's original key and tempo. If it is usually played with a capo, set capo and use the shapes.
- If you are unsure of the exact chords, still write the most common arrangement,
  and say so in an XML comment at the top of the file.

Before answering, check that: every section named in the arrangement exists; every custom pattern has
beatsPerBar × subdivision tokens; the chord lengths in a bar add up to at most beatsPerBar; every <line> has
exactly as many | pieces of lyrics as it has bars; and there are no tags other than the ones above.

Song: <SONG TITLE> by <ARTIST>
Wishes (optional):
Lyrics (optional, paste them here):
````
