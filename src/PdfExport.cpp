#include "PdfExport.h"

#include "Arranger.h"
#include "ChordDiagramWidget.h"
#include "Song.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSet>
#include <QTextOption>
#include <algorithm>
#include <cmath>

namespace {

QString tr(const char *s)
{
    return QCoreApplication::translate("PdfExport", s);
}

// Page geometry in points (1/72 inch); the painter is scaled so one unit is one point.
constexpr double kMargin = 36.0;
constexpr double kLabelW = 20.0;     // the N / S / S+ labels in front of each line
constexpr double kChordRowH = 11.5;
constexpr double kLyricRowH = 14.0;
constexpr double kLineGap = 7.0;
constexpr int kMaxBarsPerRow = 6;
constexpr int kBarsPerLineWithoutLyrics = 4;

// One way of playing the song: as written, Simplify or Simplify+.
struct Version
{
    QString tag;     // N, S, S+
    QString title;
    QColor color;
    std::shared_ptr<const Timeline> tl;
};

// The chords of a bar: where each starts (0..1 of the bar) and its name.
struct BarChord
{
    double at;
    QString name;
};

QVector<BarChord> barChords(const Timeline &tl, int bar)
{
    QVector<BarChord> out;
    const Timeline::Bar &b = tl.bars[bar];
    const int steps = std::max(1, int(b.chordAtStep.size()));
    int prev = -2;
    for (int s = 0; s < b.chordAtStep.size(); ++s) {
        const int c = b.chordAtStep[s];
        if (c == prev)
            continue;
        prev = c;
        if (c >= 0)
            out.push_back({double(s) / steps, tl.chords[c].name});
        else if (s == 0)
            out.push_back({0.0, QStringLiteral("N.C.")});
    }
    return out;
}

// Chords in the order they first appear.
QVector<ChordShape> chordsInOrder(const Timeline &tl)
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

QString capoText(int capo)
{
    return capo > 0 ? tr("Capo %1").arg(capo) : tr("No capo");
}

// A line of the sheet: some bars, and the lyrics sung over them.
struct SheetLine
{
    QVector<int> bars;
    bool split = false;   // one lyric piece per bar (else one text for the whole line)
    QString text;         // the whole line's lyric when !split
};

class Printer
{
public:
    explicit Printer(QPdfWriter *writer) : m_writer(writer), m_painter(writer)
    {
        const double scale = writer->resolution() / 72.0;
        m_painter.scale(scale, scale);
        const QSizeF page = QPageSize(QPageSize::A4).size(QPageSize::Point);
        m_pageW = page.width();
        m_pageH = page.height();
        m_contentW = m_pageW - 2 * kMargin;
    }

    bool ok() const { return m_painter.isActive(); }

    void printSong(const Timeline &base)
    {
        const Song &song = *base.song;
        m_title = song.title.isEmpty() ? QFileInfo(song.filePath).completeBaseName() : song.title;
        newPage(false);
        header(song);

        // The three versions.
        QVector<Version> versions;
        auto make = [&](ChordName::Level level, const QString &tag, const QString &name, const QColor &color) {
            Arranger::Settings s;
            s.level = level;
            auto tl = Arranger::arrange(std::make_shared<Timeline>(base), s);
            const int capo = tl->song ? tl->song->capo : 0;
            versions.push_back({tag, name + sep() + capoText(capo), color, tl});
        };
        make(ChordName::Level::AsWritten, QStringLiteral("N"), tr("As written"), QColor(30, 30, 30));
        make(ChordName::Level::Simplify, QStringLiteral("S"), tr("Simplify"), QColor(30, 110, 200));
        make(ChordName::Level::SimplifyPlus, QStringLiteral("S+"), tr("Simplify+"), QColor(210, 100, 20));
        chordBoxes(versions);

        // The song, section by section in playing order. Repeats in a row are printed once
        // ("x2"); a section that comes back later is only named.
        QSet<QString> printed;
        for (const Timeline::Play &play : base.plays) {
            if (play.pass > 1)
                continue;
            QString heading = play.section;
            if (play.passes > 1)
                heading += QStringLiteral("  ×%1").arg(play.passes);
            if (printed.contains(play.section)) {
                sectionHeading(heading, tr("(as above)"));
                continue;
            }
            printed.insert(play.section);
            const QVector<SheetLine> lines = sheetLines(base, play);
            // Keep the heading with the first line.
            ensureSpace(16 + lineHeight(lines.isEmpty() ? SheetLine() : lines.first()));
            sectionHeading(heading, QString());
            for (const SheetLine &l : lines)
                sheetLine(base, versions, l);
        }
        footer();
    }

private:
    QFont font(double size, bool bold = false) const
    {
        QFont f;
        // Sizes are in points of the page: the painter is scaled from the device's pixels.
        f.setPointSizeF(size * 72.0 / m_writer->logicalDpiY());
        f.setBold(bold);
        return f;
    }
    QFontMetricsF metrics(const QFont &f) const { return QFontMetricsF(f, m_writer); }

    // Every text gets an explicit direction: chord names and tags left to right, lyrics and
    // titles by their own script, headings and page numbers in the interface's direction.
    // Positions are computed here, so alignment is absolute (left means left).
    void draw(const QRectF &r, Qt::Alignment align, const QString &text, Qt::LayoutDirection dir)
    {
        QTextOption o(align | Qt::AlignAbsolute);
        o.setTextDirection(dir);
        o.setWrapMode(QTextOption::NoWrap);
        m_painter.drawText(r, text, o);
    }
    void drawLtr(const QRectF &r, Qt::Alignment align, const QString &text) { draw(r, align, text, Qt::LeftToRight); }
    static Qt::LayoutDirection dirOf(const QString &text) { return text.isRightToLeft() ? Qt::RightToLeft : Qt::LeftToRight; }
    // A separator between parts in different scripts (a direction mark on both sides).
    QString sep() const
    {
        const QChar mark(m_ui == Qt::RightToLeft ? 0x200F : 0x200E);
        return mark + QStringLiteral(" · ") + mark;
    }

    void newPage(bool continued)
    {
        if (m_pages > 0) {
            footer();
            m_writer->newPage();
        }
        ++m_pages;
        m_y = kMargin;
        if (continued) {
            m_painter.setFont(font(9));
            m_painter.setPen(QColor(120, 120, 120));
            draw(QRectF(kMargin, m_y, m_contentW, 12), Qt::AlignLeft | Qt::AlignVCenter, tr("%1 (continued)").arg(m_title),
                 dirOf(m_title));
            m_y += 18;
        }
    }

    void footer()
    {
        if (m_footerDone == m_pages)
            return;
        m_footerDone = m_pages;
        m_painter.setFont(font(8));
        m_painter.setPen(QColor(140, 140, 140));
        draw(QRectF(kMargin, m_pageH - kMargin + 8, m_contentW, 12), Qt::AlignCenter,
             tr("%1 · page %2").arg(m_title + QChar(m_ui == Qt::RightToLeft ? 0x200F : 0x200E)).arg(m_pages), m_ui);
    }

    void ensureSpace(double h)
    {
        if (m_y + h > m_pageH - kMargin)
            newPage(true);
    }

    void header(const Song &song)
    {
        m_painter.setPen(Qt::black);
        m_painter.setFont(font(20, true));
        const double titleH = metrics(font(20, true)).height();
        draw(QRectF(kMargin, m_y, m_contentW, titleH), Qt::AlignLeft | Qt::AlignVCenter, m_title, dirOf(m_title));
        m_y += titleH;
        QStringList info;
        if (!song.artist.isEmpty())
            info << song.artist;
        info << tr("%1 BPM").arg(qRound(song.bpm)) << QStringLiteral("%1/4").arg(song.beatsPerBar);
        m_painter.setFont(font(10));
        m_painter.setPen(QColor(100, 100, 100));
        drawLtr(QRectF(kMargin, m_y, m_contentW, 14), Qt::AlignLeft | Qt::AlignVCenter, info.join(QStringLiteral("\u200E  ·  \u200E")));
        m_y += 22;
    }

    // Three boxes side by side with the chord charts of each version.
    void chordBoxes(const QVector<Version> &versions)
    {
        const double gap = 8, boxW = (m_contentW - 2 * gap) / 3.0;
        const double cellW = 40, cellH = 58, titleH = 20;
        const int cols = std::max(1, int((boxW - 6) / cellW));
        int rows = 1;
        QVector<QVector<ChordShape>> chords;
        for (const Version &v : versions) {
            chords.push_back(chordsInOrder(*v.tl));
            rows = std::max(rows, int((chords.last().size() + cols - 1) / cols));
        }
        const double boxH = titleH + rows * cellH + 6;
        ensureSpace(boxH + 10);
        for (int i = 0; i < versions.size(); ++i) {
            const Version &v = versions[i];
            const QRectF box(kMargin + i * (boxW + gap), m_y, boxW, boxH);
            QColor fill = v.color;
            fill.setAlphaF(0.06);
            m_painter.setPen(QPen(v.color, 0.8));
            m_painter.setBrush(fill);
            m_painter.drawRoundedRect(box, 5, 5);
            m_painter.setBrush(Qt::NoBrush);
            // Title: "S+  Simplify+ · Capo 2"
            m_painter.setFont(font(9.5, true));
            m_painter.setPen(v.color);
            // The tag (N, S, S+) at the start, then the title, in the interface's direction.
            const QFontMetricsF tm = metrics(font(9.5, true));
            const double tagW = tm.horizontalAdvance(v.tag) + 8;
            const bool rtlUi = m_ui == Qt::RightToLeft;
            const QRectF inner(box.left() + 6, box.top() + 3, box.width() - 12, titleH - 4);
            drawLtr(inner, (rtlUi ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, v.tag);
            const QRectF titleRect = rtlUi ? inner.adjusted(0, 0, -tagW, 0) : inner.adjusted(tagW, 0, 0, 0);
            draw(titleRect, (rtlUi ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                 tm.elidedText(v.title, Qt::ElideRight, titleRect.width()), m_ui);
            DiagramStyle style;
            style.foreground = QColor(30, 30, 30);
            style.dots = v.color;
            style.glowing = false;
            style.minFont = 4.0;
            style.lineScale = 0.45;
            style.fingerNumbers = false;  // too small to read at this size
            const double left = box.left() + (box.width() - cols * cellW) / 2;
            for (int k = 0; k < chords[i].size(); ++k) {
                const QRectF cell(left + (k % cols) * cellW, box.top() + titleH + (k / cols) * cellH, cellW, cellH);
                drawChordDiagram(m_painter, cell.adjusted(1, 1, -1, -1), &chords[i][k], style);
            }
            if (chords[i].isEmpty()) {
                m_painter.setFont(font(9));
                m_painter.setPen(QColor(140, 140, 140));
                draw(box.adjusted(0, titleH, 0, 0), Qt::AlignCenter, tr("No chords"), m_ui);
            }
        }
        m_y += boxH + 12;
    }

    void sectionHeading(const QString &name, const QString &note)
    {
        ensureSpace(18);
        m_painter.setFont(font(11, true));
        m_painter.setPen(Qt::black);
        const double w = metrics(font(11, true)).horizontalAdvance(name);
        draw(QRectF(kMargin, m_y, m_contentW, 16), Qt::AlignLeft | Qt::AlignVCenter, name, dirOf(name));
        if (!note.isEmpty()) {
            m_painter.setFont(font(9.5));
            m_painter.setPen(QColor(120, 120, 120));
            draw(QRectF(kMargin + w + 8, m_y, m_contentW - w - 8, 16), Qt::AlignLeft | Qt::AlignVCenter, note, m_ui);
        }
        m_y += note.isEmpty() ? 17 : 20;
    }

    static QVector<SheetLine> sheetLines(const Timeline &tl, const Timeline::Play &play)
    {
        QVector<SheetLine> lines;
        const int end = play.firstBar + play.barCount;
        for (int b = play.firstBar; b < end;) {
            SheetLine l;
            const int li = tl.bars[b].lyricLine;
            if (li >= 0) {
                const Timeline::LyricLine &ll = tl.lyricLines[li];
                l.split = ll.split;
                for (int k = b; k < end && tl.bars[k].lyricLine == li; ++k)
                    l.bars << k;
                if (!l.split)
                    l.text = tl.bars[b].lyric;
            } else {
                for (int k = b; k < end && tl.bars[k].lyricLine < 0 && l.bars.size() < kBarsPerLineWithoutLyrics; ++k)
                    l.bars << k;
            }
            b += std::max(1, int(l.bars.size()));
            lines << l;
        }
        return lines;
    }

    static int rowsOf(const SheetLine &l)
    {
        return std::max(1, int((l.bars.size() + kMaxBarsPerRow - 1) / kMaxBarsPerRow));
    }

    static double lineHeight(const SheetLine &l)
    {
        return rowsOf(l) * (3 * kChordRowH + kLyricRowH + 2) + kLineGap;
    }

    // Three rows of chords (N, S, S+) above the lyrics, bar by bar.
    void sheetLine(const Timeline &tl, const QVector<Version> &versions, const SheetLine &l)
    {
        // Right-to-left lyrics (Arabic...): the first bar is on the right.
        bool rtl = l.text.isRightToLeft();
        if (l.split)
            for (int b : l.bars)
                if (!tl.bars[b].lyric.trimmed().isEmpty()) {
                    rtl = tl.bars[b].lyric.isRightToLeft();
                    break;
                }
        const int rows = rowsOf(l);
        const int perRow = (int(l.bars.size()) + rows - 1) / std::max(1, rows);
        const double barsW = m_contentW - kLabelW;
        const double cellW = barsW / std::max(1, perRow);
        const QFont chordFont = font(9, true), lyricFont = font(10.5), labelFont = font(7, true);
        const QFontMetricsF cm = metrics(chordFont), lm = metrics(lyricFont);

        for (int r = 0; r < rows; ++r) {
            const double rowH = 3 * kChordRowH + kLyricRowH + 2;
            ensureSpace(rowH + kLineGap);
            const double top = m_y;
            const double labelX = rtl ? kMargin + barsW : kMargin;
            const double barsLeft = rtl ? kMargin : kMargin + kLabelW;
            // Version labels.
            m_painter.setFont(labelFont);
            for (int v = 0; v < versions.size(); ++v) {
                m_painter.setPen(versions[v].color);
                drawLtr(QRectF(labelX, top + v * kChordRowH, kLabelW - 4, kChordRowH),
                        (rtl ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, versions[v].tag);
            }
            const int first = r * perRow, last = std::min(int(l.bars.size()), first + perRow);
            for (int k = first; k < last; ++k) {
                const int bar = l.bars[k];
                const int slot = k - first;
                const double cellLeft = rtl ? barsLeft + barsW - (slot + 1) * cellW : barsLeft + slot * cellW;
                // Bar line.
                m_painter.setPen(QPen(QColor(200, 200, 200), 0.6));
                const double lineX = rtl ? cellLeft + cellW : cellLeft;
                m_painter.drawLine(QPointF(lineX, top + 1), QPointF(lineX, top + 3 * kChordRowH));
                // Chords of each version; the same chord as the row above is printed faded.
                QVector<BarChord> above;
                for (int v = 0; v < versions.size(); ++v) {
                    const QVector<BarChord> chords = barChords(*versions[v].tl, bar);
                    m_painter.setFont(chordFont);
                    for (int c = 0; c < chords.size(); ++c) {
                        const BarChord &bc = chords[c];
                        const bool same = std::any_of(above.begin(), above.end(), [&](const BarChord &o) {
                            return std::abs(o.at - bc.at) < 1e-6 && o.name == bc.name;
                        });
                        QColor col = bc.name == QLatin1String("N.C.") ? QColor(150, 150, 150) : versions[v].color;
                        if (same)
                            col.setAlphaF(0.35);
                        m_painter.setPen(col);
                        const double w = cm.horizontalAdvance(bc.name);
                        const double pos = 3 + bc.at * (cellW - 6);
                        const double x = rtl ? cellLeft + cellW - pos - w : cellLeft + pos;
                        drawLtr(QRectF(x, top + v * kChordRowH, w + 2, kChordRowH), Qt::AlignLeft | Qt::AlignVCenter,
                                bc.name);
                    }
                    above = chords;
                }
                // Lyrics of this bar.
                if (l.split) {
                    const QString text = tl.bars[bar].lyric.trimmed();
                    m_painter.setFont(lyricFont);
                    m_painter.setPen(Qt::black);
                    draw(QRectF(cellLeft + 3, top + 3 * kChordRowH, cellW - 6, kLyricRowH),
                         (rtl ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                         lm.elidedText(text, Qt::ElideRight, cellW - 6), rtl ? Qt::RightToLeft : Qt::LeftToRight);
                }
            }
            // One lyric for the whole line, under its first row.
            if (!l.split && r == 0 && !l.text.isEmpty()) {
                m_painter.setFont(lyricFont);
                m_painter.setPen(Qt::black);
                draw(QRectF(barsLeft + 3, top + 3 * kChordRowH, barsW - 6, kLyricRowH),
                     (rtl ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                     lm.elidedText(l.text.trimmed(), Qt::ElideRight, barsW - 6), rtl ? Qt::RightToLeft : Qt::LeftToRight);
            }
            // A faint rule for the lyrics (left empty when the song has none, to write on).
            m_painter.setPen(QPen(QColor(225, 225, 225), 0.5));
            m_painter.drawLine(QPointF(barsLeft, top + 3 * kChordRowH + kLyricRowH + 1),
                               QPointF(barsLeft + barsW, top + 3 * kChordRowH + kLyricRowH + 1));
            m_y += rowH;
        }
        m_y += kLineGap;
    }

    QPdfWriter *m_writer;
    QPainter m_painter;
    const Qt::LayoutDirection m_ui = QGuiApplication::layoutDirection();
    double m_pageW = 0, m_pageH = 0, m_contentW = 0;
    double m_y = 0;
    int m_pages = 0;
    int m_footerDone = 0;
    QString m_title;
};

} // namespace

bool exportSongsToPdf(const QStringList &songFiles, const QString &pdfPath, QString *error,
                      const std::function<bool(int)> &progress)
{
    // Load everything first, so a broken song doesn't leave half a file.
    QVector<std::shared_ptr<Timeline>> timelines;
    for (const QString &path : songFiles) {
        QString err;
        auto song = loadSong(path, &err);
        auto tl = song ? buildTimeline(song, &err) : nullptr;
        if (!tl) {
            if (error)
                *error = tr("%1: %2").arg(QFileInfo(path).fileName(), err);
            return false;
        }
        timelines << tl;
    }
    if (timelines.isEmpty()) {
        if (error)
            *error = tr("No songs to export.");
        return false;
    }

    QPdfWriter writer(pdfPath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setResolution(300);
    writer.setCreator(QStringLiteral("Guitar Chord Player"));
    writer.setTitle(timelines.size() == 1 ? timelines.first()->song->title : tr("Songbook"));
    Printer printer(&writer);
    if (!printer.ok()) {
        if (error)
            *error = tr("Could not write %1").arg(pdfPath);
        return false;
    }
    for (int i = 0; i < timelines.size(); ++i) {
        if (progress && !progress(i))
            return false;
        printer.printSong(*timelines[i]);
    }
    return true;
}
