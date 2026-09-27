#pragma once

#include "LyricsAligner.h"

#include <QDialog>
#include <memory>

class QLabel;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;

// Paste the words of a song; they are spread over its bars with a live preview.
class LyricsDialog : public QDialog
{
    Q_OBJECT
public:
    LyricsDialog(std::shared_ptr<const Timeline> tl, QWidget *parent = nullptr);

    QVector<LyricsAligner::Placement> placements() const { return m_placements; }

private:
    void autoSettings();
    void updatePreview();

    std::shared_ptr<const Timeline> m_tl;
    QPlainTextEdit *m_text;
    QSpinBox *m_start;
    QSpinBox *m_perLine;
    QSpinBox *m_pause;
    QTableWidget *m_preview;
    QLabel *m_summary;
    QVector<LyricsAligner::Placement> m_placements;
    bool m_userTouched = false;
};
