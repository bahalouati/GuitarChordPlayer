#include "LyricsAligner.h"

#include <QRegularExpression>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>

namespace LyricsAligner {

QStringList splitLyrics(const QString &pasted)
{
    QStringList out;
    bool lastBlank = true;
    for (QString line : pasted.split(QLatin1Char('\n'))) {
        line = line.simplified();
        line.remove(QLatin1Char('|')); // "|" separates bars in song files
        // Chord-site markers such as "[Chorus]" or "x2" are not sung.
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
            line.clear();
        if (line.isEmpty()) {
            if (!lastBlank)
                out << QString();
            lastBlank = true;
            continue;
        }
        out << line;
        lastBlank = false;
    }
    while (!out.isEmpty() && out.last().isEmpty())
        out.removeLast();
    return out;
}

static bool isIntroName(const QString &name)
{
    const QString n = name.toLower();
    return n.contains(QLatin1String("intro")) || n.contains(QStringLiteral("مقدمة"))
        || n.contains(QStringLiteral("مقدمه"));
}

Options suggestOptions(const Timeline &tl, const QStringList &lines)
{
    Options o;
    const int bars = int(tl.bars.size());
    if (!tl.plays.isEmpty() && isIntroName(tl.plays.first().section))
        o.startBar = tl.plays.first().barCount;
    // Otherwise start at the first bar that has a chord.
    if (o.startBar == 0) {
        for (int b = 0; b < bars; ++b) {
            bool any = false;
            for (int c : tl.bars[b].chordAtStep)
                any = any || c >= 0;
            if (any) {
                o.startBar = b;
                break;
            }
        }
    }
    int sung = 0, blanks = 0;
    for (const QString &l : lines)
        (l.isEmpty() ? blanks : sung)++;
    const int available = std::max(1, bars - o.startBar - blanks);
    const double per = sung > 0 ? double(available) / sung : 2.0;
    // Lyric lines almost always last 1, 2, 4 or 8 bars: pick the nearest that fits.
    int best = 1;
    for (int c : {1, 2, 4, 8})
        if (c <= per + 0.25)
            best = c;
    o.barsPerLine = best;
    o.pauseBars = blanks > 0 && available - sung * best >= blanks ? 1 : 0;
    return o;
}

QVector<Placement> place(const Timeline &tl, const QStringList &lines, const Options &opt)
{
    QVector<Placement> out;
    int bar = std::max(0, opt.startBar);
    const int total = int(tl.bars.size());
    for (const QString &l : lines) {
        if (l.isEmpty()) {
            bar += std::max(0, opt.pauseBars);
            continue;
        }
        const int n = std::max(1, opt.barsPerLine);
        if (bar >= total)
            break;
        out.push_back({l, bar, std::min(n, total - bar)});
        bar += n;
    }
    return out;
}

QString barChordText(const Timeline &tl, int bar)
{
    const Timeline::Bar &b = tl.bars[bar];
    struct Run { int chord; int steps; };
    QVector<Run> runs;
    for (int c : b.chordAtStep) {
        if (!runs.isEmpty() && runs.last().chord == c)
            ++runs.last().steps;
        else
            runs.push_back({c, 1});
    }
    auto name = [&](int c) { return c >= 0 ? tl.chords[c].name : QStringLiteral("N.C."); };
    if (runs.size() <= 1)
        return runs.isEmpty() ? QStringLiteral("N.C.") : name(runs.first().chord);
    const bool even = std::all_of(runs.begin(), runs.end(), [&](const Run &r) { return r.steps == runs.first().steps; });
    QStringList parts;
    for (const Run &r : runs) {
        if (even) {
            parts << name(r.chord);
        } else {
            const double beats = double(r.steps) / b.subdivision;
            parts << name(r.chord) + QLatin1Char(':') + QString::number(beats, 'g', 4);
        }
    }
    return parts.join(QLatin1Char(' '));
}

// Spreads the words of a line over its bars, as evenly as possible.
static QStringList splitWords(const QString &text, int bars)
{
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList out;
    int w = 0;
    for (int b = 0; b < bars; ++b) {
        const int remainingBars = bars - b;
        const int take = int(std::ceil(double(words.size() - w) / remainingBars));
        out << QStringList(words.mid(w, take)).join(QLatin1Char(' '));
        w += take;
    }
    return out;
}

static QString noteName(int midi)
{
    static const char *n[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return QString::fromLatin1(n[midi % 12]) + QString::number(midi / 12 - 1);
}

QString songXmlWithLyrics(const Song &song, const Timeline &tl, const QVector<Placement> &lyrics,
                          const QString &audioFile)
{
    // Which lyric (and which piece of it) each bar gets.
    const int total = int(tl.bars.size());
    QVector<int> lineOf(total, -1);
    QVector<QString> pieceOf(total);
    for (int i = 0; i < lyrics.size(); ++i) {
        const Placement &p = lyrics[i];
        const QStringList pieces = splitWords(p.text, p.barCount);
        for (int k = 0; k < p.barCount && p.firstBar + k < total; ++k) {
            lineOf[p.firstBar + k] = i;
            pieceOf[p.firstBar + k] = pieces.value(k);
        }
    }

    QString xml;
    QXmlStreamWriter w(&xml);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(2);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("song"));
    w.writeAttribute(QStringLiteral("title"), song.title);
    if (!song.artist.isEmpty())
        w.writeAttribute(QStringLiteral("artist"), song.artist);
    w.writeAttribute(QStringLiteral("bpm"), QString::number(song.bpm, 'g', 6));
    w.writeAttribute(QStringLiteral("beatsPerBar"), QString::number(song.beatsPerBar));
    if (song.capo > 0)
        w.writeAttribute(QStringLiteral("capo"), QString::number(song.capo));
    const std::array<int, 6> standard{40, 45, 50, 55, 59, 64};
    if (song.tuning != standard) {
        QStringList t;
        for (int m : song.tuning)
            t << noteName(m);
        w.writeAttribute(QStringLiteral("tuning"), t.join(QLatin1Char(' ')));
    }
    w.writeAttribute(QStringLiteral("pattern"), song.defaultPattern);

    if (!song.audioFile.isEmpty()) {
        if (song.audioBeats.size() < 2) {
            w.writeEmptyElement(QStringLiteral("audio"));
        } else {
            w.writeStartElement(QStringLiteral("audio"));
        }
        w.writeAttribute(QStringLiteral("file"), audioFile);
        w.writeAttribute(QStringLiteral("offset"), QString::number(song.audioOffset, 'f', 3));
        if (song.audioBeats.size() >= 2) {
            QString text;
            for (int i = 0; i < song.audioBeats.size(); ++i) {
                text += QString::number(song.audioBeats[i], 'f', 3);
                text += (i + 1) % qMax(1, song.beatsPerBar) == 0 ? QStringLiteral("\n") : QStringLiteral(" ");
            }
            w.writeTextElement(QStringLiteral("beats"), QStringLiteral("\n") + text.trimmed() + QStringLiteral("\n"));
            w.writeEndElement();
        }
    }
    if (!song.chords.isEmpty()) {
        w.writeStartElement(QStringLiteral("chords"));
        for (const ChordShape &c : song.chords) {
            w.writeEmptyElement(QStringLiteral("chord"));
            w.writeAttribute(QStringLiteral("name"), c.name);
            const bool wide = std::any_of(c.frets.begin(), c.frets.end(), [](int f) { return f > 9; });
            QStringList frets, fingers;
            for (int i = 0; i < 6; ++i) {
                frets << (c.frets[size_t(i)] < 0 ? QStringLiteral("x") : QString::number(c.frets[size_t(i)]));
                fingers << (c.fingers[size_t(i)] == 5 ? QStringLiteral("T") : QString::number(c.fingers[size_t(i)]));
            }
            w.writeAttribute(QStringLiteral("frets"), frets.join(wide ? QStringLiteral(" ") : QString()));
            w.writeAttribute(QStringLiteral("fingers"), fingers.join(QString()));
        }
        w.writeEndElement();
    }
    if (!song.patterns.isEmpty()) {
        w.writeStartElement(QStringLiteral("patterns"));
        for (const Pattern &p : song.patterns) {
            QStringList tokens;
            for (const PatternStep &st : p.steps)
                tokens << st.token;
            w.writeStartElement(QStringLiteral("pattern"));
            w.writeAttribute(QStringLiteral("name"), p.name);
            w.writeAttribute(QStringLiteral("subdivision"), QString::number(p.subdivision));
            w.writeCharacters(tokens.join(QLatin1Char(' ')));
            w.writeEndElement();
        }
        w.writeEndElement();
    }

    // One section per pass through a section, so every pass can have its own words.
    w.writeStartElement(QStringLiteral("sections"));
    for (const Timeline::Play &play : tl.plays) {
        if (play.barCount <= 0)
            continue;
        const Timeline::Bar &first = tl.bars[play.firstBar];
        w.writeStartElement(QStringLiteral("section"));
        w.writeAttribute(QStringLiteral("name"), play.passes > 1
                         ? QStringLiteral("%1 (%2)").arg(play.section).arg(play.pass) : play.section);
        if (first.pattern >= 0)
            w.writeAttribute(QStringLiteral("pattern"), tl.patterns[first.pattern].name);
        if (std::abs(first.bpm - song.bpm) > 1e-6)
            w.writeAttribute(QStringLiteral("bpm"), QString::number(first.bpm, 'g', 6));
        if (first.beatsPerBar != song.beatsPerBar)
            w.writeAttribute(QStringLiteral("beatsPerBar"), QString::number(first.beatsPerBar));

        int b = play.firstBar;
        const int end = play.firstBar + play.barCount;
        while (b < end) {
            // A run of bars that share the same lyric line (or have none).
            const int line = lineOf[b];
            int e = b + 1;
            while (e < end && lineOf[e] == line && (line >= 0 || e - b < 4))
                ++e;
            QStringList chords, pieces;
            for (int k = b; k < e; ++k) {
                chords << barChordText(tl, k);
                pieces << pieceOf[k];
            }
            // Without any "|", "Am F" would be read as two bars: a trailing "|" keeps it as one.
            QString chordText = chords.join(QStringLiteral(" | "));
            if (chords.size() == 1 && chordText.contains(QLatin1Char(' ')))
                chordText += QStringLiteral(" |");
            if (line < 0) {
                w.writeTextElement(QStringLiteral("bars"), chordText);
            } else {
                w.writeStartElement(QStringLiteral("line"));
                w.writeAttribute(QStringLiteral("chords"), chordText);
                w.writeCharacters(e - b > 1 ? pieces.join(QStringLiteral(" | ")) : pieces.first());
                w.writeEndElement();
            }
            b = e;
        }
        w.writeEndElement(); // section
    }
    w.writeEndElement(); // sections
    w.writeEndElement(); // song
    w.writeEndDocument();
    return xml;
}

} // namespace LyricsAligner
