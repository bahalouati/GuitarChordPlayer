#pragma once

#include <QString>

// Turns a plain-text chord sheet into song XML, so a song can be written like this:
//
//   [Intro] arpeggio
//   G | Cadd9 | Em | D
//
//   [Verse] folk x2
//   G | D | Em | C
//
//   [Chorus] drive 96bpm
//   C | G | D | Em
//
//   [Verse]          <- a section used again: just its name
//   [Chorus] x2
//
// After the [Name]: an optional pattern name, "xN" to repeat, "NNbpm" for a tempo change.
// Lines starting with # are comments.
struct ChordSheetInfo
{
    QString title;
    QString artist;
    double bpm = 90;
    int beatsPerBar = 4;
    int capo = 0;
    QString defaultPattern = QStringLiteral("folk");
};

bool chordSheetToXml(const ChordSheetInfo &info, const QString &sheet, QString *xml, QString *error);

// Example text shown in the New Song dialog.
QString exampleChordSheet();
