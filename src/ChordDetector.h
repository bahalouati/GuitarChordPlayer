#pragma once

#include "AudioTrack.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

// Result of analysing a recording: a steady beat grid and the chords on it.
struct DetectedSong
{
    double bpm = 120.0;
    int beatsPerBar = 4;
    double offset = 0.0;          // seconds into the recording where bar 1 starts
    int capo = 0;                 // suggested capo; chords below are the shapes to play
    QString key;                  // e.g. "Am" (sounding key)
    QVector<QStringList> bars;    // one or two chords per bar ("N.C." = no chord)
    double confidence = 0.0;      // 0..1, how well the chords fit the audio on average
};

// Finds tempo, beats, bar lines and chords in a recording.
// progress gets 0..1; set *cancel to stop early. Runs on any thread.
bool detectChords(const AudioClip &clip, DetectedSong *out, QString *error,
                  const std::function<void(double)> &progress = {}, const std::atomic<bool> *cancel = nullptr);

// Writes a song file that plays along with the recording (audioFileName is relative to the song file).
QString detectedSongToXml(const DetectedSong &song, const QString &title, const QString &artist,
                          const QString &audioFileName);
