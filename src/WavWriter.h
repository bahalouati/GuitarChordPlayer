#pragma once

#include <QString>
#include <vector>

// Writes mono float samples as a 16-bit PCM WAV file.
bool writeWav(const QString &path, const std::vector<float> &samples, int sampleRate, QString *error);

#include <memory>
struct Timeline;

// Renders a whole song offline (no count-in) and saves it as a WAV file.
bool exportSongToWav(std::shared_ptr<const Timeline> tl, const QString &path, double tempoScale,
                     QString *error);
