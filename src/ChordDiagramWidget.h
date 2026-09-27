#pragma once

#include "ChordLibrary.h"

#include <QColor>
#include <QFont>
#include <QWidget>
#include <array>
#include <optional>

class QPainter;

// How a chord chart is drawn (the widget's look by default).
struct DiagramStyle
{
    QColor foreground = Qt::black;
    QColor dots = QColor(40, 120, 220);
    QFont font;
    bool glowing = true;     // show plucked strings
    double minFont = 8.0;    // smallest text, in screen points
    double fontScale = 1.0;
    double lineScale = 1.0;  // line widths
    bool fingerNumbers = true;
};

// Draws a chord chart (name on top, then the fretboard) into r. Used by the widget and for
// printing. chord = nullptr draws "N.C.".
void drawChordDiagram(QPainter &p, const QRectF &r, const ChordShape *chord, const DiagramStyle &style,
                      const QString &caption = QString(), const std::array<float, 6> &glow = {});

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
