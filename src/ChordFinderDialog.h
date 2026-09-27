#pragma once

#include <QDialog>

class AudioEngine;
class ChordDiagramWidget;
class QLabel;
class QLineEdit;

// Type a chord name to see (and hear) the fingering the player will use.
class ChordFinderDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ChordFinderDialog(AudioEngine *engine, QWidget *parent = nullptr);

private:
    void showChord(const QString &name, bool strum);

    AudioEngine *m_engine;
    QLineEdit *m_name;
    ChordDiagramWidget *m_diagram;
    QLabel *m_info;
};
