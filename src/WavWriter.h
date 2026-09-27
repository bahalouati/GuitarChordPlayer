#pragma once

#include <QString>
#include <vector>

// Writes stereo float samples as a 16-bit PCM WAV file.
bool writeWav(const QString &path, const std::vector<float> &left, const std::vector<float> &right,
              int sampleRate, QString *error);

#include <memory>
struct Timeline;
struct AudioClip;

// Renders a whole song offline (no count-in) and saves it as a WAV file.
// With a recording, it is mixed in at the given offset (seconds where bar 1 starts).
bool exportSongToWav(std::shared_ptr<const Timeline> tl, const QString &path, double tempoScale,
                     QString *error, std::shared_ptr<const AudioClip> audio = nullptr, double audioOffset = 0.0);
