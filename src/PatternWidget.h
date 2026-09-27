#pragma once

#include "Song.h"

#include <QWidget>
#include <memory>

// Shows the strum / pick pattern of the current bar and the next one,
// with the chord changes and a moving playhead.
class PatternWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PatternWidget(QWidget *parent = nullptr);

    void setTimeline(std::shared_ptr<const Timeline> tl);
    void setPosition(int bar, int step, double fraction, bool active);

    QSize sizeHint() const override { return {700, 190}; }
    QSize minimumSizeHint() const override { return {300, 130}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void drawBar(QPainter &p, const QRectF &r, int bar, bool current);

    std::shared_ptr<const Timeline> m_tl;
    int m_bar = 0;
    int m_step = 0;
    double m_fraction = 0.0;
    bool m_active = false;
};
