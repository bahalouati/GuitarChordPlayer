#include "StageWidgets.h"

#include "ChordDiagramWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace Stage {

QPalette palette(Theme theme, const QPalette &base)
{
    QPalette p = base;
    auto set = [&p](QPalette::ColorRole role, const QColor &c, const QColor &disabled = QColor()) {
        p.setColor(QPalette::Active, role, c);
        p.setColor(QPalette::Inactive, role, c);
        p.setColor(QPalette::Disabled, role, disabled.isValid() ? disabled : c);
    };
    if (theme == Theme::Dark) {
        set(QPalette::Window, QColor(18, 21, 28));
        set(QPalette::WindowText, QColor(236, 240, 246), QColor(120, 129, 145));
        set(QPalette::Base, QColor(30, 35, 45));
        set(QPalette::AlternateBase, QColor(38, 44, 56));
        set(QPalette::Text, QColor(236, 240, 246), QColor(120, 129, 145));
        set(QPalette::Mid, QColor(64, 72, 88));
        set(QPalette::Link, QColor(92, 176, 255));
        set(QPalette::Highlight, hot());
    } else {
        set(QPalette::Window, QColor(246, 247, 250));
        set(QPalette::WindowText, QColor(24, 27, 33), QColor(150, 155, 165));
        set(QPalette::Base, QColor(255, 255, 255));
        set(QPalette::AlternateBase, QColor(240, 243, 248));
        set(QPalette::Text, QColor(24, 27, 33), QColor(150, 155, 165));
        set(QPalette::Mid, QColor(200, 205, 214));
        set(QPalette::Link, QColor(40, 120, 220));
        set(QPalette::Highlight, hot());
    }
    return p;
}

QColor hot()
{
    return QColor(255, 140, 40);
}

} // namespace Stage

QVector<ChordShape> chordsInSong(const Timeline &tl)
{
    QVector<ChordShape> out;
    QSet<QString> seen;
    for (const Timeline::Bar &b : tl.bars)
        for (int c : b.chordAtStep)
            if (c >= 0 && !seen.contains(tl.chords[c].name)) {
                seen.insert(tl.chords[c].name);
                out.push_back(tl.chords[c]);
            }
    return out;
}

// ---------------------------------------------------------------------------------------------

StageWidget::StageWidget(QWidget *parent) : QWidget(parent)
{
    setTheme(Stage::Theme::Dark);
}

void StageWidget::setTheme(Stage::Theme theme)
{
    m_theme = theme;
    setPalette(Stage::palette(theme, palette()));
    update();
}

void StageWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRectF r = rect();
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    if (m_theme == Stage::Theme::Dark) {
        g.setColorAt(0.0, QColor(28, 33, 44));
        g.setColorAt(0.55, QColor(17, 20, 27));
        g.setColorAt(1.0, QColor(10, 12, 16));
    } else {
        g.setColorAt(0.0, QColor(252, 252, 254));
        g.setColorAt(1.0, QColor(234, 237, 243));
    }
    p.fillRect(r, g);
    // A soft light behind the chord cards.
    QRadialGradient glow(QPointF(r.center().x(), r.height() * 0.35), r.width() * 0.45);
    QColor c = m_theme == Stage::Theme::Dark ? QColor(92, 176, 255) : QColor(40, 120, 220);
    c.setAlphaF(m_theme == Stage::Theme::Dark ? 0.07 : 0.04);
    glow.setColorAt(0.0, c);
    c.setAlphaF(0.0);
    glow.setColorAt(1.0, c);
    p.fillRect(r, glow);
}

// ---------------------------------------------------------------------------------------------

ChordStripWidget::ChordStripWidget(QWidget *parent) : QWidget(parent)
{
    // Diagrams read left to right in every language.
    setLayoutDirection(Qt::LeftToRight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void ChordStripWidget::setChords(const QVector<ChordShape> &chords)
{
    m_chords = chords;
    update();
}

void ChordStripWidget::setActive(const QString &current, const QString &next)
{
    if (current == m_current && next == m_next)
        return;
    m_current = current;
    m_next = next;
    update();
}

void ChordStripWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF r = rect().adjusted(6, 2, -6, -4);

    // Heading
    QFont hf = font();
    hf.setPointSizeF(std::max(8.0, hf.pointSizeF() * 0.9));
    hf.setBold(true);
    hf.setLetterSpacing(QFont::PercentageSpacing, 108);
    p.setFont(hf);
    p.setPen(pal.color(QPalette::Disabled, QPalette::WindowText));
    const double headH = QFontMetricsF(hf).height() + 4;
    p.drawText(QRectF(r.left(), r.top(), r.width(), headH), Qt::AlignLeft | Qt::AlignVCenter,
               tr("Chords in this song (%n)", nullptr, int(m_chords.size())));
    if (m_chords.isEmpty())
        return;

    // As many rows as needed to keep the charts a readable size.
    const QRectF area(r.left(), r.top() + headH, r.width(), r.height() - headH);
    const int n = int(m_chords.size());
    int rows = 1;
    auto cellFor = [&](int rowsCount) {
        const int perRow = (n + rowsCount - 1) / rowsCount;
        const double h = area.height() / rowsCount;
        return std::min(area.width() / perRow, h * 0.78);
    };
    while (rows < 3 && cellFor(rows) < 58 && cellFor(rows + 1) > cellFor(rows))
        ++rows;
    const int perRow = (n + rows - 1) / rows;
    const double cellW = cellFor(rows);
    const double cellH = area.height() / rows;
    const double gap = std::max(4.0, cellW * 0.08);

    for (int i = 0; i < n; ++i) {
        const int row = i / perRow, col = i % perRow;
        const int inRow = std::min(perRow, n - row * perRow);
        const double rowW = inRow * cellW;
        const double left = area.left() + (area.width() - rowW) / 2 + col * cellW;
        const QRectF card(left + gap / 2, area.top() + row * cellH + 2, cellW - gap, cellH - 4);
        const ChordShape &c = m_chords[i];
        const bool current = c.name == m_current;
        const bool next = !current && c.name == m_next;

        QColor bg = pal.color(QPalette::Base);
        bg.setAlphaF(current ? 0.95 : 0.55);
        p.setBrush(bg);
        if (current)
            p.setPen(QPen(Stage::hot(), 2.5));
        else if (next)
            p.setPen(QPen(pal.color(QPalette::Link), 1.5, Qt::DashLine));
        else
            p.setPen(QPen(pal.color(QPalette::Mid), 1));
        p.drawRoundedRect(card, 8, 8);

        DiagramStyle style;
        style.foreground = current || next ? pal.color(QPalette::WindowText)
                                           : pal.color(QPalette::Disabled, QPalette::WindowText).lighter(115);
        style.dots = current ? Stage::hot() : pal.color(QPalette::Link);
        if (!current && !next)
            style.dots.setAlphaF(0.75);
        style.font = font();
        style.glowing = false;
        style.minFont = 6.0;
        style.lineScale = std::clamp(card.width() / 110.0, 0.55, 1.0);
        style.fingerNumbers = card.width() > 70;
        drawChordDiagram(p, card.adjusted(3, 3, -3, -3), &c, style);
    }
}

// ---------------------------------------------------------------------------------------------

SongProgressWidget::SongProgressWidget(QWidget *parent) : QWidget(parent)
{
    setLayoutDirection(Qt::LeftToRight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(40);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Click a section to jump there"));
}

void SongProgressWidget::setTimeline(std::shared_ptr<const Timeline> tl)
{
    m_tl = std::move(tl);
    update();
}

void SongProgressWidget::setPosition(int bar, double barFraction)
{
    if (bar == m_bar && std::abs(barFraction - m_fraction) < 0.01)
        return;
    m_bar = bar;
    m_fraction = barFraction;
    update();
}

QRectF SongProgressWidget::segmentRect(int play) const
{
    const QRectF r = rect().adjusted(6, 0, -6, 0);
    if (!m_tl || m_tl->bars.isEmpty())
        return {};
    const double total = m_tl->bars.size();
    const Timeline::Play &pl = m_tl->plays[play];
    const double x0 = r.left() + r.width() * pl.firstBar / total;
    const double x1 = r.left() + r.width() * (pl.firstBar + pl.barCount) / total;
    return QRectF(x0, r.top(), x1 - x0, r.height());
}

void SongProgressWidget::paintEvent(QPaintEvent *)
{
    if (!m_tl || m_tl->bars.isEmpty())
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    QFont f = font();
    f.setPointSizeF(std::max(7.5, f.pointSizeF() * 0.85));
    p.setFont(f);
    const QFontMetricsF fm(f);
    const double barH = 7, barTop = height() - barH - 5;
    const int curPlay = m_tl->bars[std::clamp(m_bar, 0, int(m_tl->bars.size()) - 1)].play;
    const double total = m_tl->bars.size();
    const double pos = std::clamp((m_bar + m_fraction) / total, 0.0, 1.0);
    const QRectF all = rect().adjusted(6, 0, -6, 0);
    const double playX = all.left() + all.width() * pos;

    for (int i = 0; i < m_tl->plays.size(); ++i) {
        const QRectF seg = segmentRect(i);
        const QRectF track(seg.left() + 1.5, barTop, std::max(1.0, seg.width() - 3), barH);
        QColor base = pal.color(QPalette::Mid);
        base.setAlphaF(0.6);
        p.setPen(Qt::NoPen);
        p.setBrush(base);
        p.drawRoundedRect(track, barH / 2, barH / 2);
        // Played part.
        const double filled = std::clamp(playX - track.left(), 0.0, track.width());
        if (filled > 0) {
            QColor done = i == curPlay ? Stage::hot() : pal.color(QPalette::Link);
            if (i != curPlay)
                done.setAlphaF(0.7);
            p.setBrush(done);
            p.drawRoundedRect(QRectF(track.left(), track.top(), filled, barH), barH / 2, barH / 2);
        }
        // Section name, if it fits.
        const Timeline::Play &pl = m_tl->plays[i];
        QString name = pl.section;
        if (pl.passes > 1)
            name += QStringLiteral(" %1/%2").arg(pl.pass).arg(pl.passes);
        const QString shown = fm.elidedText(name, Qt::ElideRight, seg.width() - 6);
        if (shown.size() >= 3 || shown == name) {
            p.setPen(i == curPlay ? Stage::hot() : pal.color(QPalette::Disabled, QPalette::WindowText));
            QFont nf = f;
            nf.setBold(i == curPlay);
            p.setFont(nf);
            p.drawText(QRectF(seg.left() + 3, 0, seg.width() - 6, barTop - 2), Qt::AlignLeft | Qt::AlignBottom, shown);
            p.setFont(f);
        }
    }
    // Playhead.
    p.setPen(QPen(pal.color(QPalette::Window), 2));
    p.setBrush(Stage::hot());
    p.drawEllipse(QPointF(playX, barTop + barH / 2), barH * 0.95, barH * 0.95);
}

void SongProgressWidget::mousePressEvent(QMouseEvent *e)
{
    if (!m_tl)
        return;
    for (int i = 0; i < m_tl->plays.size(); ++i)
        if (segmentRect(i).contains(QPointF(e->position().x(), height() / 2.0))) {
            emit playClicked(i);
            return;
        }
}
