#pragma once

#include <QHash>
#include <QString>
#include <array>
#include <optional>

// A fingering for one chord on a 6-string guitar.
// Index 0 is the low E string (string 6), index 5 is the high e string (string 1),
// matching the usual "x32010" chord notation read left to right.
struct ChordShape
{
    QString name;
    std::array<int, 6> frets{};   // -1 = muted, 0 = open, n = fret number
    std::array<int, 6> fingers{}; // 0 = none, 1..4 = finger, 5 = thumb

    // Lowest and highest string index (0..5) that is actually played, or -1.
    int bassIndex() const;
    int trebleIndex() const;
};

namespace ChordLibrary {

// Parses "x32010" or "x 10 12 12 11 10" style fret strings. Returns nullopt on error.
std::optional<std::array<int, 6>> parseFrets(const QString &text);
// Parses "032010" style finger strings ('T' = thumb, 'x' treated as 0).
std::optional<std::array<int, 6>> parseFingers(const QString &text);

// Built-in shapes for common open and barre chords, keyed by chord name.
const QHash<QString, ChordShape> &builtIn();

// Finds a shape for any chord name: the built-in library first (also trying spelling
// variants such as "Amin" -> "Am" and "A#" -> "Bb"), then a generated barre chord for
// roots A-G with #/b and the qualities m, 7, m7, maj7, sus2, sus4, 5, 6, m6, 9, dim, aug.
// Slash chords ("G/B") fall back to the chord without the bass note.
std::optional<ChordShape> lookup(const QString &name);

} // namespace ChordLibrary
