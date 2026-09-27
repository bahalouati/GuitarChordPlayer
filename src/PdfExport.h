#pragma once

#include <QString>
#include <QStringList>
#include <functional>

// Prints songs as chord sheets to a PDF file, one or more pages per song:
//  - three boxes with the chord charts: the chords as written, Simplify, and Simplify+ (with
//    the capo it chooses),
//  - then every line of the song with the three versions of its chords stacked above the
//    lyrics (an empty lyrics line when the song has none, to write on).
// progress(i) is called before song i; return false to stop. Returns false on error.
bool exportSongsToPdf(const QStringList &songFiles, const QString &pdfPath, QString *error,
                      const std::function<bool(int)> &progress = {});
