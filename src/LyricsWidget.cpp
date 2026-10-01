#include "LyricsWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

LyricsWidget::LyricsWidget(QWidget *parent) : QWidget(parent)
{
    // Diagrams and timelines read left to right in every language.
    setLayoutDirection(Qt::LeftToRight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Click a lyric line to edit it"));
}

void LyricsWidget::setTimeline(std::shared_ptr<const Timeline> tl)
{
    m_tl = std::move(tl);
    update();
}

void LyricsWidget::setPosition(int bar, double barFraction, bool active)
{
    if (bar == m_bar && active == m_active && std::abs(barFraction - m_fraction) < 0.01)
        return;
    m_bar = bar;
    m_fraction = barFraction;
    m_active = active;
    update();
}

QVector<LyricsWidget::Item> LyricsWidget::itemsFor(int line) const
{
    QVector<Item> items;
    const Timeline::LyricLine &l = m_tl->lyricLines[line];
    for (int b = l.firstBar; b < l.firstBar + l.barCount && b < m_tl->bars.size(); ++b) {
        const Timeline::Bar &bar = m_tl->bars[b];
        QStringList names;
        int prev = -2;
        for (int c : bar.chordAtStep) {
            if (c != prev)
                names << (c >= 0 ? m_tl->chords[c].name : QStringLiteral("N.C."));
            prev = c;
        }
        items << Item{names.join(QLatin1Char(' ')), bar.lyric, b};
    }
    return items;
}

double LyricsWidget::drawLine(QPainter &p, const QRectF &r, int line, int highlightBar, double fraction,
                              double pointSize, bool dim, bool measureOnly)
{
    const Timeline::LyricLine &l = m_tl->lyricLines[line];
    const QVector<Item> items = itemsFor(line);

    QFont textFont = font();
    textFont.setPointSizeF(pointSize);
    QFont chordFont = font();
    chordFont.setPointSizeF(pointSize * 0.7);
    chordFont.setBold(true);
    QFont boldText = textFont;
    boldText.setBold(true);
    // Widths use the bold font so the highlighted (bold) bar never runs into the next one.
    const QFontMetricsF tm(textFont), bm(boldText), cm(chordFont);
    const double gap = tm.horizontalAdvance(QLatin1Char(' ')) * 1.5;
    const double rowH = cm.height() + tm.height() + 4;

    const QColor fg = palette().color(dim ? QPalette::Disabled : QPalette::Normal, QPalette::WindowText);
    const QColor chordColor = dim ? fg : palette().color(QPalette::Link);
    const QColor hot(255, 140, 40);

    // Arabic, Hebrew, Persian...: lay the line out from the right.
    bool rtl = false;
    for (const Item &it : items)
        if (!it.text.isEmpty()) {
            rtl = it.text.isRightToLeft();
            break;
        }
    // Absolute: right means right also when the whole interface is right to left.
    const Qt::Alignment hAlign = (rtl ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignAbsolute;
    // Draw the words in their own direction (an English line in the Arabic interface keeps
    // its full stop at the end).
    p.setLayoutDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);

    if (!l.split) {
        // One lyric for the whole line: chords spread above it, progress underline below.
        QString chordsRow;
        for (const Item &it : items)
            chordsRow += it.chord + QStringLiteral("   ");
        const QString text = items.isEmpty() ? QString() : items.first().text;
        if (measureOnly)
            return rowH + 6;
        p.setFont(chordFont);
        p.setPen(chordColor);
        p.drawText(QRectF(r.left(), r.top(), r.width(), cm.height()), hAlign | Qt::AlignVCenter,
                   cm.elidedText(chordsRow.trimmed(), Qt::ElideRight, r.width()));
        p.setFont(textFont);
        p.setPen(highlightBar >= 0 ? hot : fg);
        const QRectF tr(r.left(), r.top() + cm.height() + 2, r.width(), tm.height());
        p.drawText(tr, hAlign | Qt::AlignVCenter, tm.elidedText(text, Qt::ElideRight, r.width()));
        if (highlightBar >= 0 && l.barCount > 0) {
            const double prog = std::clamp((highlightBar - l.firstBar + fraction) / l.barCount, 0.0, 1.0);
            const double w = std::min(r.width(), tm.horizontalAdvance(text));
            p.setPen(QPen(hot, 3, Qt::SolidLine, Qt::RoundCap));
            if (rtl)
                p.drawLine(QPointF(r.right(), tr.bottom() + 3), QPointF(r.right() - w * prog, tr.bottom() + 3));
            else
                p.drawLine(QPointF(r.left(), tr.bottom() + 3), QPointF(r.left() + w * prog, tr.bottom() + 3));
        }
        return rowH + 6;
    }

    // Lyrics split per bar: lay the pieces out like a chord sheet, wrapping when needed.
    // For right-to-left scripts the first bar is on the right and the line flows leftwards.
    double x = 0, y = r.top(); // x = distance from the starting edge
    for (const Item &it : items) {
        const double w = std::max(bm.horizontalAdvance(it.text), cm.horizontalAdvance(it.chord)) + gap;
        if (x > 0 && x + w > r.width()) {
            x = 0;
            y += rowH;
        }
        if (!measureOnly) {
            const double cellW = w - gap;
            const double left = rtl ? r.right() - x - cellW : r.left() + x;
            const bool current = it.bar == highlightBar;
            const bool past = highlightBar >= 0 && it.bar < highlightBar;
            if (current) {
                QColor bg = hot;
                bg.setAlphaF(0.18);
                p.setPen(Qt::NoPen);
                p.setBrush(bg);
                p.drawRoundedRect(QRectF(left - 3, y, cellW + 6, rowH - 2), 4, 4);
            }
            p.setFont(chordFont);
            p.setPen(chordColor);
            p.drawText(QRectF(left, y, cellW, cm.height()), hAlign | Qt::AlignVCenter, it.chord);
            QFont f = textFont;
            f.setBold(current);
            p.setFont(f);
            QColor c = current ? hot : fg;
            if (past)
                c.setAlphaF(0.5);
            p.setPen(c);
            p.drawText(QRectF(left, y + cm.height() + 2, cellW, tm.height()), hAlign | Qt::AlignVCenter, it.text);
        }
        x += w;
    }
    return y - r.top() + rowH + 6;
}

void LyricsWidget::paintEvent(QPaintEvent *)
{
    m_currentLine = m_nextLine = -1;
    if (!m_tl || m_tl->lyricLines.isEmpty() || m_bar < 0 || m_bar >= m_tl->bars.size())
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect().adjusted(8, 4, -8, -4);

    // The line being sung, or the next one coming up during an instrumental part.
    int current = m_tl->bars[m_bar].lyricLine;
    bool upcoming = false;
    if (current < 0) {
        for (int i = 0; i < m_tl->lyricLines.size(); ++i) {
            if (m_tl->lyricLines[i].firstBar > m_bar) {
                current = i;
                upcoming = true;
                break;
            }
        }
    }
    if (current < 0)
        return;
    const int next = current + 1 < m_tl->lyricLines.size() ? current + 1 : -1;

    // Font size: as big as fits, shrinking for long lines (and leaving room for the
    // "singing starts" note and the next line).
    double size = std::clamp(r.height() * 0.16, 10.0, 24.0);
    auto availFor = [&](double sz) {
        return (r.height() - (upcoming ? sz * 0.9 : 0.0)) * (next >= 0 ? 0.62 : 1.0);
    };
    while (size > 9 && drawLine(p, r, current, -1, 0, size, false, true) > availFor(size))
        size -= 1;
    const double avail = availFor(size);

    if (upcoming) {
        QFont f = font();
        f.setPointSizeF(std::max(8.0, size * 0.55));
        p.setFont(f);
        p.setPen(palette().color(QPalette::Disabled, QPalette::WindowText));
        const int bars = m_tl->lyricLines[current].firstBar - m_bar;
        p.drawText(QRectF(r.left(), r.top(), r.width(), f.pointSizeF() * 1.6), Qt::AlignLeft | Qt::AlignVCenter,
                   tr("Singing starts in %n bar(s)", nullptr, bars));
    }
    const double top = upcoming ? r.top() + size * 0.9 : r.top();
    const double h = drawLine(p, QRectF(r.left(), top, r.width(), avail), current,
                              upcoming || !m_active ? -1 : m_bar, m_fraction, size, upcoming, false);
    m_currentLine = m_tl->lyricLines[current].sourceLine;
    m_currentRect = QRectF(r.left(), top, r.width(), h);

    if (next >= 0) {
        const QRectF nr(r.left(), top + h + 2, r.width(), r.bottom() - (top + h + 2));
        // The next line, smaller, shrunk further if needed to fit what is left.
        double nsize = std::max(8.0, size * 0.7);
        while (nsize > 7 && drawLine(p, nr, next, -1, 0, nsize, true, true) > nr.height())
            nsize -= 0.5;
        if (nr.height() > 10 && drawLine(p, nr, next, -1, 0, nsize, true, true) <= nr.height() + 1) {
            const double nh = drawLine(p, nr, next, -1, 0, nsize, true, false);
            m_nextLine = m_tl->lyricLines[next].sourceLine;
            m_nextRect = QRectF(nr.left(), nr.top(), nr.width(), nh);
        }
    }
}

void LyricsWidget::mousePressEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    if (m_currentLine > 0 && m_currentRect.contains(pos))
        emit lineClicked(m_currentLine);
    else if (m_nextLine > 0 && m_nextRect.contains(pos))
        emit lineClicked(m_nextLine);
}
