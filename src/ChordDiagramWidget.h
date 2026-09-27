#pragma once

#include "ChordLibrary.h"

#include <QWidget>
#include <array>
#include <optional>

// Classic vertical chord chart: strings run top to bottom, low E on the left.
// Strings light up when they are plucked.
class ChordDiagramWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ChordDiagramWidget(QWidget *parent = nullptr);

    void setChord(const ChordShape *chord);   // nullptr = no chord
    void setCaption(const QString &caption);  // small text above, e.g. "Now" / "Next"
    void setGlow(const std::array<float, 6> &glow);
    void setDimmed(bool dimmed);

    QSize sizeHint() const override { return {260, 340}; }
    QSize minimumSizeHint() const override { return {120, 160}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    std::optional<ChordShape> m_chord;
    QString m_caption;
    std::array<float, 6> m_glow{};
    bool m_dimmed = false;
};
