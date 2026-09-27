#include "ChordSheet.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVector>
#include <QXmlStreamWriter>

namespace {

struct SheetSection
{
    QString name;
    QString pattern;
    double bpm = 0;
    QStringList lines;
};

struct SheetPlay
{
    QString section;
    int repeat = 1;
};

} // namespace

bool chordSheetToXml(const ChordSheetInfo &info, const QString &sheet, QString *xml, QString *error)
{
    static const QRegularExpression headerRe(QStringLiteral("^\\[([^\\]]+)\\]\\s*(.*)$"));
    static const QRegularExpression repeatRe(QStringLiteral("^[xX](\\d+)$"));
    static const QRegularExpression bpmRe(QStringLiteral("^(?:bpm=)?(\\d+(?:\\.\\d+)?)\\s*(?:bpm)?$"),
                                          QRegularExpression::CaseInsensitiveOption);

    QVector<SheetSection> sections;
    QVector<SheetPlay> plays;
    int current = -1;          // section receiving chord lines, or -1
    bool currentIsReuse = false;

    const QStringList lines = sheet.split(QLatin1Char('\n'));
    for (int ln = 0; ln < lines.size(); ++ln) {
        const QString line = lines[ln].trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const auto m = headerRe.match(line);
        if (m.hasMatch()) {
            const QString name = m.captured(1).trimmed();
            SheetSection opts;
            int repeat = 1;
            for (const QString &tok : m.captured(2).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
                const auto rm = repeatRe.match(tok);
                const auto bm = bpmRe.match(tok);
                if (rm.hasMatch())
                    repeat = qMax(1, rm.captured(1).toInt());
                else if (bm.hasMatch() && (tok.contains(QLatin1String("bpm"), Qt::CaseInsensitive)))
                    opts.bpm = bm.captured(1).toDouble();
                else
                    opts.pattern = tok;
            }
            plays << SheetPlay{name, repeat};

            current = -1;
            for (int i = 0; i < sections.size(); ++i)
                if (sections[i].name == name)
                    current = i;
            currentIsReuse = current >= 0;
            if (current < 0) {
                opts.name = name;
                sections << opts;
                current = sections.size() - 1;
            }
            continue;
        }

        // A line of chords.
        if (current < 0) {
            sections << SheetSection{QStringLiteral("Song"), QString(), 0, {}};
            plays << SheetPlay{QStringLiteral("Song"), 1};
            current = sections.size() - 1;
            currentIsReuse = false;
        }
        if (currentIsReuse) {
            if (error)
                *error = QStringLiteral("Line %1: section [%2] already has chords above. To play it again, "
                                        "write just [%2]; for new chords, give it a new name like [%2 2].")
                        .arg(ln + 1).arg(sections[current].name);
            return false;
        }
        QString bars = line;
        while (bars.startsWith(QLatin1Char('|')))
            bars.remove(0, 1);
        while (bars.endsWith(QLatin1Char('|')))
            bars.chop(1);
        sections[current].lines << bars.trimmed();
    }

    for (const SheetSection &s : sections) {
        if (s.lines.isEmpty()) {
            if (error)
                *error = QStringLiteral("Section [%1] has no chords. Put a line like  G | D | Em | C  under it.").arg(s.name);
            return false;
        }
    }
    if (sections.isEmpty()) {
        if (error)
            *error = QStringLiteral("Write at least one line of chords, e.g.  G | D | Em | C");
        return false;
    }

    QString out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(2);
    w.writeStartDocument();
    w.writeComment(QStringLiteral(" Made with Guitar Chord Player. Edit it any way you like - see Help > Song XML format. "));
    w.writeStartElement(QStringLiteral("song"));
    w.writeAttribute(QStringLiteral("title"), info.title);
    if (!info.artist.isEmpty())
        w.writeAttribute(QStringLiteral("artist"), info.artist);
    w.writeAttribute(QStringLiteral("bpm"), QString::number(info.bpm));
    w.writeAttribute(QStringLiteral("beatsPerBar"), QString::number(info.beatsPerBar));
    if (info.capo > 0)
        w.writeAttribute(QStringLiteral("capo"), QString::number(info.capo));
    w.writeAttribute(QStringLiteral("pattern"), info.defaultPattern);

    w.writeStartElement(QStringLiteral("sections"));
    for (const SheetSection &s : sections) {
        w.writeStartElement(QStringLiteral("section"));
        w.writeAttribute(QStringLiteral("name"), s.name);
        if (!s.pattern.isEmpty())
            w.writeAttribute(QStringLiteral("pattern"), s.pattern);
        if (s.bpm > 0)
            w.writeAttribute(QStringLiteral("bpm"), QString::number(s.bpm));
        for (const QString &l : s.lines)
            w.writeTextElement(QStringLiteral("bars"), l);
        w.writeEndElement();
    }
    w.writeEndElement();

    w.writeStartElement(QStringLiteral("arrangement"));
    for (const SheetPlay &p : plays) {
        w.writeEmptyElement(QStringLiteral("play"));
        w.writeAttribute(QStringLiteral("section"), p.section);
        if (p.repeat > 1)
            w.writeAttribute(QStringLiteral("repeat"), QString::number(p.repeat));
    }
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();

    *xml = out;
    return true;
}

QString exampleChordSheet()
{
    return QStringLiteral(
        "# One section per [Name]. After the name you can add a pattern,\n"
        "# x2 to repeat it, or 100bpm to change the tempo.\n"
        "# Chords: bars are separated by |  -  two chords in one bar share it.\n"
        "\n"
        "[Intro] arpeggio\n"
        "G | Cadd9 | Em | D\n"
        "\n"
        "[Verse] folk x2\n"
        "G | D | Em | C\n"
        "\n"
        "[Chorus] drive\n"
        "C | G | D | Em\n"
        "C | G | D | D\n"
        "\n"
        "# Use a section again by writing just its name:\n"
        "[Verse]\n"
        "[Chorus] x2\n"
        "\n"
        "[Outro] whole\n"
        "G | G\n");
}
