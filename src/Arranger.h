#pragma once

#include "ChordName.h"
#include "Song.h"

#include <memory>

// Re-fingers a song for the player: a different capo (the song sounds the same, only the
// shapes change) and optionally simpler chords. Works on a copy; the song file is untouched.
namespace Arranger {

constexpr int kCapoAsSong = -1;  // keep the song's capo
constexpr int kCapoAuto = -2;    // pick the capo with the easiest shapes

struct Settings
{
    int capo = kCapoAsSong;
    ChordName::Level level = ChordName::Level::AsWritten;
    bool displayNames = true;    // show chord names in English letters, however the song spells them
};

// How hard a shape is to play: 0 = easy open chord, higher = barres, stretches, high frets.
double difficulty(const ChordShape &shape);

// The capo (0..7) that makes the song's chords easiest at the given level.
int easiestCapo(const Timeline &tl, ChordName::Level level);

// Returns tl itself when nothing changes, otherwise a re-fingered copy.
std::shared_ptr<const Timeline> arrange(std::shared_ptr<const Timeline> tl, const Settings &s);

// The capo the arranged timeline uses.
inline int capoOf(const Timeline &tl) { return tl.song ? tl.song->capo : 0; }

} // namespace Arranger
