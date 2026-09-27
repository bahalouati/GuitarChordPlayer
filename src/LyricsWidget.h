#pragma once

#include "Song.h"

#include <QWidget>
#include <memory>

// Shows the lyric line being sung, with chord names above the words and the current bar
// highlighted, plus the next line underneath. Clicking a line asks to edit it.
class LyricsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LyricsWidget(QWidget *parent = nullptr);

    void setTimeline(std::shared_ptr<const Timeline> tl);
    void setPosition(int bar, double barFraction, bool active);

    QSize sizeHint() const override { return {700, 150}; }
    QSize minimumSizeHint() const override { return {300, 90}; }

signals:
    void lineClicked(int sourceLine);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    struct Item
    {
        QString chord;
        QString text;
        int bar;
    };
    QVector<Item> itemsFor(int line) const;
    // Draws a line inside r; returns the height used. highlightBar < 0: nothing highlighted.
    double drawLine(QPainter &p, const QRectF &r, int line, int highlightBar, double fraction,
                    double pointSize, bool dim, bool measureOnly);

    std::shared_ptr<const Timeline> m_tl;
    int m_bar = 0;
    double m_fraction = 0;
    bool m_active = false;
    QRectF m_currentRect, m_nextRect;
    int m_currentLine = -1, m_nextLine = -1;
};
