#include "PatternWidget.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

PatternWidget::PatternWidget(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void PatternWidget::setTimeline(std::shared_ptr<const Timeline> tl)
{
    m_tl = std::move(tl);
    update();
}

void PatternWidget::setPosition(int bar, int step, double fraction, bool active)
{
    if (bar == m_bar && step == m_step && active == m_active && std::abs(fraction - m_fraction) < 0.02)
        return;
    m_bar = bar;
    m_step = step;
    m_fraction = fraction;
    m_active = active;
    update();
}

static QString countLabel(int step, int subdivision)
{
    const int beat = step / subdivision + 1;
    const int sub = step % subdivision;
    if (sub == 0)
        return QString::number(beat);
    switch (subdivision) {
    case 2: return QStringLiteral("&");
    case 3: return sub == 1 ? QStringLiteral("&") : QStringLiteral("a");
    case 4: return sub == 1 ? QStringLiteral("e") : sub == 2 ? QStringLiteral("&") : QStringLiteral("a");
    default: return QStringLiteral("·");
    }
}

void PatternWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect().adjusted(4, 4, -4, -4);
    if (!m_tl)
        return;
    const double rowH = r.height() / 2.0;
    drawBar(p, QRectF(r.left(), r.top(), r.width(), rowH - 4), m_bar, true);
    drawBar(p, QRectF(r.left(), r.top() + rowH + 4, r.width(), rowH - 4), m_bar + 1, false);
}

void PatternWidget::drawBar(QPainter &p, const QRectF &r, int barIndex, bool current)
{
    const QPalette pal = palette();
    QColor fg = pal.color(QPalette::WindowText);
    if (!current)
        fg = pal.color(QPalette::Disabled, QPalette::WindowText);
    const QColor accent(255, 140, 40);
    const QColor chordColor = current ? QColor(40, 120, 220) : fg;

    const double labelW = std::min(70.0, r.width() * 0.1);
    QFont f = font();
    f.setPointSizeF(std::max(8.0, r.height() * 0.11));
    p.setFont(f);
    p.setPen(fg);
    p.drawText(QRectF(r.left(), r.top(), labelW, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
               current ? tr("Now") : tr("Next"));

    if (barIndex < 0 || barIndex >= m_tl->bars.size())
        return;
    const Timeline::Bar &bar = m_tl->bars[barIndex];
    const int steps = bar.stepCount();
    const QRectF area(r.left() + labelW, r.top(), r.width() - labelW, r.height());
    const double cw = area.width() / steps;
    const double chordH = area.height() * 0.25;
    const double symH = area.height() * 0.5;
    const double countH = area.height() * 0.25;

    // Background
    p.setPen(Qt::NoPen);
    p.setBrush(pal.color(QPalette::Base));
    p.drawRoundedRect(area.adjusted(0, chordH, 0, 0), 6, 6);

    for (int s = 0; s < steps; ++s) {
        const QRectF cell(area.left() + s * cw, area.top(), cw, area.height());
        const QRectF sym(cell.left(), cell.top() + chordH, cw, symH);
        const QRectF cnt(cell.left(), sym.bottom(), cw, countH);

        if (current && m_active && s == m_step) {
            QColor hl = accent;
            hl.setAlphaF(0.35 + 0.4 * (1.0 - m_fraction));
            p.setPen(Qt::NoPen);
            p.setBrush(hl);
            p.drawRoundedRect(sym.united(cnt).adjusted(2, 2, -2, -2), 5, 5);
        }
        // Beat separators
        if (s > 0 && s % bar.subdivision == 0) {
            p.setPen(QPen(pal.color(QPalette::Mid), 1));
            p.drawLine(QPointF(cell.left(), sym.top() + 4), QPointF(cell.left(), cnt.bottom() - 4));
        }

        // Chord name at the start of the bar and wherever it changes.
        const int chord = bar.chordAtStep.value(s, -1);
        if (s == 0 || chord != bar.chordAtStep.value(s - 1, -1)) {
            QFont cf = font();
            cf.setBold(true);
            cf.setPointSizeF(std::max(9.0, chordH * 0.5));
            p.setFont(cf);
            p.setPen(chordColor);
            p.drawText(QRectF(cell.left() + 2, cell.top(), area.right() - cell.left(), chordH),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       chord >= 0 ? m_tl->chords[chord].name : tr("N.C."));
        }

        // Pattern symbol
        const PatternStep *st = m_tl->stepAt(barIndex, s);
        QString txt;
        double scale = 0.55;
        if (st) {
            switch (st->kind) {
            case PatternStep::Kind::Rest: txt = QStringLiteral("·"); break;
            case PatternStep::Kind::Down: txt = QStringLiteral("↓"); scale = st->light ? 0.5 : 0.75; break;
            case PatternStep::Kind::Up: txt = QStringLiteral("↑"); scale = st->light ? 0.5 : 0.75; break;
            case PatternStep::Kind::Mute: txt = QStringLiteral("✕"); break;
            case PatternStep::Kind::Pick:
                txt = st->token;
                txt.remove(QLatin1Char('>'));
                scale = txt.size() > 2 ? 0.3 : 0.45;
                break;
            }
        }
        QFont sf = font();
        sf.setBold(st && st->accent);
        sf.setPointSizeF(std::max(8.0, std::min(symH, cw * 1.4) * scale));
        p.setFont(sf);
        // Accented steps are drawn in red with a small ">" in the corner.
        const QColor accentText = current ? QColor(220, 50, 50) : fg;
        p.setPen(st && st->accent ? accentText : fg);
        p.drawText(sym, Qt::AlignCenter, txt);
        if (st && st->accent) {
            QFont af = font();
            af.setBold(true);
            af.setPointSizeF(std::max(7.0, symH * 0.2));
            p.setFont(af);
            p.drawText(sym.adjusted(4, 2, 0, 0), Qt::AlignTop | Qt::AlignLeft, QStringLiteral(">"));
        }

        QFont nf = font();
        nf.setPointSizeF(std::max(7.0, std::min(countH, cw) * 0.45));
        nf.setBold(s % bar.subdivision == 0);
        p.setFont(nf);
        p.setPen(fg);
        p.drawText(cnt, Qt::AlignCenter, countLabel(s, bar.subdivision));
    }
}
