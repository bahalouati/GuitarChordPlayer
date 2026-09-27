#pragma once

#include <QString>
#include <QStringList>

// Chord names: parsing (English letters, solfège "Do Ré Mi" and Arabic "دو ري مي"),
// transposing, simplifying and display in the chosen notation.
namespace ChordName {

struct Parsed
{
    int root = -1;       // pitch class 0..11 (C = 0), -1 = not a chord
    QString quality;     // normalised: "", "m", "7", "maj7", "m7", "sus4", "dim", ...
    int bass = -1;       // slash-chord bass pitch class, -1 = none
    QString suffix;      // anything after the chord that we keep, e.g. "(open)"
};

// Parses "F#m7", "Bb/D", "Dom", "Lam7", "صول", "لا م", "ري 7"... Returns root -1 if not a chord.
Parsed parse(const QString &name);

// English spelling ("F#m7", "Bb/D"), sharps or flats chosen by the root.
QString toEnglish(const Parsed &p);

// Joins chord names that were written with a space, e.g. {"لا", "م"} -> {"لا م"},
// {"Do", "m7"} -> {"Do m7"}, so bar text can be split on spaces safely.
QStringList joinTokens(const QStringList &tokens);

// Converts any accepted spelling to English letters; unknown names are returned unchanged.
QString normalise(const QString &name);

// Moves a chord up (semitones > 0) or down.
QString transpose(const QString &name, int semitones);

enum class Level { AsWritten = 0, Simplify = 1, SimplifyPlus = 2 };

// Simplify: plain major or minor (Cmaj7 -> C, Am7 -> Am, Dsus4 -> D, G/B -> G, Bdim -> Bm).
QString simplify(const QString &name);

enum class Notation { English = 0, Solfege = 1, Arabic = 2 };
void setNotation(Notation n);
Notation notation();
// A chord name as the player should see it, in the chosen notation.
QString display(const QString &englishName);

} // namespace ChordName
