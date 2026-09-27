#include "ChordDiagramWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

ChordDiagramWidget::ChordDiagramWidget(QWidget *parent) : QWidget(parent)
{
    // Diagrams and timelines read left to right in every language.
    setLayoutDirection(Qt::LeftToRight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ChordDiagramWidget::setChord(const ChordShape *chord)
{
    if (chord)
        m_chord = *chord;
    else
        m_chord.reset();
    update();
}

void ChordDiagramWidget::setCaption(const QString &caption)
{
    m_caption = caption;
    update();
}

void ChordDiagramWidget::setGlow(const std::array<float, 6> &glow)
{
    if (glow != m_glow) {
        m_glow = glow;
        update();
    }
}

void ChordDiagramWidget::setDimmed(bool dimmed)
{
    m_dimmed = dimmed;
    update();
}

void ChordDiagramWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor fg = m_dimmed ? palette().color(QPalette::Disabled, QPalette::WindowText)
                               : palette().color(QPalette::WindowText);
    const QColor accent(255, 140, 40);
    const QColor dotColor = m_dimmed ? fg : QColor(40, 120, 220);

    const QRectF r = rect().adjusted(8, 4, -8, -4);
    QFont f = font();

    // Caption and chord name.
    double y = r.top();
    if (!m_caption.isEmpty()) {
        f.setPointSizeF(std::max(8.0, r.height() * 0.04));
        // Shrink the caption until it fits the width.
        while (f.pointSizeF() > 6.0 && QFontMetricsF(f).horizontalAdvance(m_caption) > r.width())
            f.setPointSizeF(f.pointSizeF() - 0.5);
        p.setFont(f);
        p.setPen(fg);
        p.drawText(QRectF(r.left(), y, r.width(), r.height() * 0.07), Qt::AlignCenter, m_caption);
        y += r.height() * 0.07;
    }
    const QString name = m_chord ? m_chord->name : tr("N.C.");
    f.setPointSizeF(std::max(10.0, r.height() * 0.08));
    f.setBold(true);
    p.setFont(f);
    p.setPen(fg);
    p.drawText(QRectF(r.left(), y, r.width(), r.height() * 0.13), Qt::AlignCenter, name);
    y += r.height() * 0.14;
    f.setBold(false);

    if (!m_chord)
        return;
    const ChordShape &c = *m_chord;

    // Which frets to show: start at the nut unless the shape sits higher up.
    int maxFret = 0, minFret = 99;
    for (int fr : c.frets) {
        if (fr > 0) {
            maxFret = std::max(maxFret, fr);
            minFret = std::min(minFret, fr);
        }
    }
    const int numFrets = std::max(4, maxFret <= 4 ? 4 : maxFret - minFret + 1);
    const int baseFret = maxFret <= 4 ? 1 : minFret;

    // Grid geometry, keeping a pleasant aspect ratio.
    const double markH = r.height() * 0.08;
    double gridTop = y + markH;
    double gridH = r.bottom() - gridTop - 4;
    double gridW = std::min(r.width() * 0.72, gridH * 0.75);
    gridH = std::min(gridH, gridW / 0.75);
    const double gridLeft = r.center().x() - gridW / 2;
    const double sx = gridW / 5.0;
    const double fy = gridH / numFrets;

    // Frets
    p.setPen(QPen(fg, 1.5));
    for (int i = 0; i <= numFrets; ++i)
        p.drawLine(QPointF(gridLeft, gridTop + i * fy), QPointF(gridLeft + gridW, gridTop + i * fy));
    if (baseFret == 1) {
        p.setPen(QPen(fg, std::max(4.0, fy * 0.12)));
        p.drawLine(QPointF(gridLeft, gridTop), QPointF(gridLeft + gridW, gridTop));
    } else {
        f.setPointSizeF(std::max(8.0, fy * 0.28));
        p.setFont(f);
        p.setPen(fg);
        p.drawText(QRectF(gridLeft - sx * 1.5, gridTop, sx * 1.35, fy), Qt::AlignRight | Qt::AlignVCenter,
                   tr("%1fr").arg(baseFret));
    }

    // Strings (thicker for bass), glowing when plucked.
    for (int i = 0; i < 6; ++i) {
        const double x = gridLeft + i * sx;
        const float g = std::clamp(m_glow[size_t(i)], 0.f, 1.f);
        const double w = 1.0 + (5 - i) * 0.35;
        if (g > 0.02f && !m_dimmed) {
            QColor halo = accent;
            halo.setAlphaF(0.35 * g);
            p.setPen(QPen(halo, w + 10 * g, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(x, gridTop), QPointF(x, gridTop + gridH));
            // A little vibration wiggle.
            QPainterPath path(QPointF(x, gridTop));
            const int segs = 24;
            for (int k = 1; k <= segs; ++k) {
                const double t = double(k) / segs;
                const double amp = sx * 0.12 * g * std::sin(t * 3.14159);
                path.lineTo(x + ((k % 2) ? amp : -amp), gridTop + t * gridH);
            }
            QColor hot = accent;
            p.setPen(QPen(hot, w + 1));
            p.drawPath(path);
        } else {
            p.setPen(QPen(fg, w));
            p.drawLine(QPointF(x, gridTop), QPointF(x, gridTop + gridH));
        }
    }

    // Open / muted markers above the nut.
    const double mr = std::min(sx, markH) * 0.28;
    for (int i = 0; i < 6; ++i) {
        const QPointF ctr(gridLeft + i * sx, gridTop - markH * 0.55);
        const float g = std::clamp(m_glow[size_t(i)], 0.f, 1.f);
        if (c.frets[size_t(i)] == 0) {
            p.setPen(QPen(g > 0.05f && !m_dimmed ? accent : fg, 1.8));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(ctr, mr, mr);
        } else if (c.frets[size_t(i)] < 0) {
            p.setPen(QPen(fg, 1.8));
            p.drawLine(ctr + QPointF(-mr, -mr), ctr + QPointF(mr, mr));
            p.drawLine(ctr + QPointF(-mr, mr), ctr + QPointF(mr, -mr));
        }
    }

    // Barres: the same finger on the same fret across several strings.
    const double dotR = std::min(sx, fy) * 0.36;
    std::array<bool, 6> inBarre{};
    for (int finger = 1; finger <= 4; ++finger) {
        int lo = -1, hi = -1, fret = -1;
        bool ok = true;
        for (int i = 0; i < 6; ++i) {
            if (c.fingers[size_t(i)] != finger || c.frets[size_t(i)] <= 0)
                continue;
            if (fret < 0)
                fret = c.frets[size_t(i)];
            else if (fret != c.frets[size_t(i)])
                ok = false;
            if (lo < 0)
                lo = i;
            hi = i;
        }
        if (!ok || lo < 0 || hi == lo)
            continue;
        const double cy = gridTop + (fret - baseFret + 0.5) * fy;
        QRectF bar(gridLeft + lo * sx - dotR, cy - dotR, (hi - lo) * sx + 2 * dotR, 2 * dotR);
        p.setPen(Qt::NoPen);
        p.setBrush(dotColor);
        p.drawRoundedRect(bar, dotR, dotR);
        for (int i = lo; i <= hi; ++i)
            if (c.fingers[size_t(i)] == finger && c.frets[size_t(i)] == fret)
                inBarre[size_t(i)] = true;
    }

    // Finger dots.
    f.setPointSizeF(std::max(7.0, dotR * 0.9));
    f.setBold(true);
    p.setFont(f);
    for (int i = 0; i < 6; ++i) {
        const int fr = c.frets[size_t(i)];
        if (fr <= 0)
            continue;
        const QPointF ctr(gridLeft + i * sx, gridTop + (fr - baseFret + 0.5) * fy);
        const float g = std::clamp(m_glow[size_t(i)], 0.f, 1.f);
        if (!inBarre[size_t(i)]) {
            p.setPen(Qt::NoPen);
            p.setBrush(dotColor);
            p.drawEllipse(ctr, dotR, dotR);
        }
        if (g > 0.05f && !m_dimmed) {
            p.setPen(QPen(accent, 2 + 3 * g));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(ctr, dotR + 2, dotR + 2);
        }
        const int finger = c.fingers[size_t(i)];
        // Label a barre only once, on its lowest string.
        bool firstOfBarre = true;
        for (int j = 0; j < i; ++j)
            if (inBarre[size_t(j)] && c.fingers[size_t(j)] == finger && c.frets[size_t(j)] == fr)
                firstOfBarre = false;
        if (finger > 0 && (!inBarre[size_t(i)] || firstOfBarre)) {
            p.setPen(Qt::white);
            p.drawText(QRectF(ctr.x() - dotR, ctr.y() - dotR, 2 * dotR, 2 * dotR), Qt::AlignCenter,
                       finger == 5 ? QStringLiteral("T") : QString::number(finger));
        }
    }
}
