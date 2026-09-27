#pragma once

#include "Song.h"

#include <QString>
#include <QStringList>
#include <QVector>

// Places pasted lyrics on a song's bars and writes the song back out with <line> elements.
namespace LyricsAligner {

struct Options
{
    int startBar = 0;         // 0-based bar where singing starts
    int barsPerLine = 2;      // bars each lyric line lasts
    int pauseBars = 0;        // bars of rest for each blank line (between verses)
};

struct Placement
{
    QString text;             // the lyric line
    int firstBar = 0;
    int barCount = 0;
};

// Non-empty lines of the pasted text; blank lines are kept as empty strings (verse breaks).
QStringList splitLyrics(const QString &pasted);

// Sensible defaults: skip an intro, and spread the lines over the rest of the song.
Options suggestOptions(const Timeline &tl, const QStringList &lines);

// Where each lyric line goes. Lines that don't fit before the end of the song are dropped.
QVector<Placement> place(const Timeline &tl, const QStringList &lines, const Options &opt);

// Chord text of one bar in <bars> syntax, e.g. "G", "C G" or "C:3 G:1".
QString barChordText(const Timeline &tl, int bar);

// The whole song as XML, one section per section pass, with the lyrics in <line> elements.
// audioFile is written as given (relative to where the new file will be saved).
QString songXmlWithLyrics(const Song &song, const Timeline &tl, const QVector<Placement> &lyrics,
                          const QString &audioFile);

} // namespace LyricsAligner
