#pragma once

#include "ChordLibrary.h"

#include <QString>
#include <QVector>
#include <memory>
#include <optional>

// One step (sub-beat slot) of a strumming / picking pattern.
struct PatternStep
{
    enum class Kind { Rest, Down, Up, Mute, Pick };

    Kind kind = Kind::Rest;
    bool light = false;   // lowercase d / u: fewer strings, softer
    bool accent = false;  // '>' prefix
    // For Kind::Pick: guitar string numbers 1..6 (1 = high e). 0 = bass, -1 = alternate bass.
    QVector<int> strings;
    QString token;        // original text, used for display
};

struct Pattern
{
    QString name;
    int subdivision = 2;  // steps per beat
    QVector<PatternStep> steps;
};

// One bar of a section: which chord starts at which step.
struct BarDef
{
    struct Slot { QString chord; double beats = 0.0; }; // beats <= 0: split the rest evenly
    QVector<Slot> parts;
    QString lyric;        // words sung during this bar (from a <line>)
    int lyricLine = -1;   // index into Section::lines, -1 = no lyrics
};

// A <line> of lyrics covering some bars of a section.
struct LyricLineDef
{
    int firstBar = 0;     // within the section
    int barCount = 0;
    int sourceLine = 0;   // line number in the XML file, for jumping to it in the editor
    bool split = false;   // lyrics divided per bar with '|'
};

struct Section
{
    QString name;
    QString pattern;      // pattern name (empty = the song's default pattern)
    double bpm = 0.0;     // 0 = use the song tempo
    int beatsPerBar = 0;  // 0 = use the song meter
    QVector<BarDef> bars;
    QVector<LyricLineDef> lines;
};

struct ArrangementItem
{
    QString section;
    int repeat = 1;
};

struct Song
{
    QString filePath;
    QString title;
    QString artist;
    double bpm = 90.0;
    int beatsPerBar = 4;
    int capo = 0;
    QString defaultPattern = QStringLiteral("folk"); // used by sections without a pattern
    std::array<int, 6> tuning{40, 45, 50, 55, 59, 64}; // MIDI notes, low E first

    QVector<ChordShape> chords;       // chords defined in the file (override built-ins)
    QVector<Pattern> patterns;
    QVector<Section> sections;
    QVector<ArrangementItem> arrangement;

    // Chords defined in the file win over the built-in library.
    std::optional<ChordShape> findChord(const QString &name) const;
    // Patterns defined in the file win over the built-in presets.
    const Pattern *findPattern(const QString &name) const;
    int sectionIndex(const QString &name) const;
};

// Ready-made patterns that songs can use by name without defining them.
struct PatternPreset
{
    const char *name;
    int subdivision;
    const char *steps;
    const char *description;
};
const QVector<PatternPreset> &patternPresets();

// Parses a pattern text such as "D - D U - U D U" or "B 3 2 >1 A 3 2+1 3".
bool parsePatternSteps(const QString &text, QVector<PatternStep> *out, QString *error);

// Loads and validates a song XML file.
std::shared_ptr<Song> loadSong(const QString &path, QString *error);
// Same, from XML text in memory (path is only remembered, not read).
std::shared_ptr<Song> loadSongFromData(const QByteArray &xml, const QString &path, QString *error);

// A song flattened into a list of bars ready for playback.
struct Timeline
{
    struct Bar
    {
        int play = 0;          // index into plays
        int barInSection = 0;  // bar number within this pass of the section
        double bpm = 90.0;
        int beatsPerBar = 4;
        int subdivision = 2;
        int pattern = -1;      // index into patterns
        QVector<int> chordAtStep; // index into chords, -1 = no chord
        int lyricLine = -1;       // index into lyricLines, -1 = none
        QString lyric;

        int stepCount() const { return beatsPerBar * subdivision; }
    };

    struct Play
    {
        QString section;
        int pass = 1;          // 1-based repeat number
        int passes = 1;
        int firstBar = 0;
        int barCount = 0;
    };

    struct LyricLine
    {
        int firstBar = 0;
        int barCount = 0;
        int sourceLine = 0;
        bool split = false;
    };

    std::shared_ptr<const Song> song;
    QVector<LyricLine> lyricLines;
    QVector<ChordShape> chords;  // every chord used, resolved
    QVector<Pattern> patterns;   // every pattern used, resolved
    QVector<Bar> bars;
    QVector<Play> plays;

    const PatternStep *stepAt(int bar, int step) const;
};

std::shared_ptr<Timeline> buildTimeline(std::shared_ptr<const Song> song, QString *error);
