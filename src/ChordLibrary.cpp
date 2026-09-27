#include "ChordLibrary.h"

#include <QRegularExpression>
#include <QStringList>

int ChordShape::bassIndex() const
{
    for (int i = 0; i < 6; ++i)
        if (frets[i] >= 0)
            return i;
    return -1;
}

int ChordShape::trebleIndex() const
{
    for (int i = 5; i >= 0; --i)
        if (frets[i] >= 0)
            return i;
    return -1;
}

namespace ChordLibrary {

std::optional<std::array<int, 6>> parseFrets(const QString &text)
{
    const QString t = text.trimmed();
    QStringList parts;
    if (t.contains(QRegularExpression(QStringLiteral("[\\s,]"))))
        parts = t.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
    else
        for (QChar c : t)
            parts << QString(c);

    if (parts.size() != 6)
        return std::nullopt;

    std::array<int, 6> out{};
    for (int i = 0; i < 6; ++i) {
        const QString &p = parts[i];
        if (p.compare(QLatin1String("x"), Qt::CaseInsensitive) == 0 || p == QLatin1String("-")) {
            out[i] = -1;
            continue;
        }
        bool ok = false;
        const int f = p.toInt(&ok);
        if (!ok || f < 0 || f > 24)
            return std::nullopt;
        out[i] = f;
    }
    return out;
}

std::optional<std::array<int, 6>> parseFingers(const QString &text)
{
    const QString t = text.trimmed().remove(QRegularExpression(QStringLiteral("[\\s,]")));
    if (t.size() != 6)
        return std::nullopt;
    std::array<int, 6> out{};
    for (int i = 0; i < 6; ++i) {
        const QChar c = t[i].toUpper();
        if (c == QLatin1Char('T'))
            out[i] = 5;
        else if (c == QLatin1Char('X') || c == QLatin1Char('-'))
            out[i] = 0;
        else if (c >= QLatin1Char('0') && c <= QLatin1Char('4'))
            out[i] = c.digitValue();
        else
            return std::nullopt;
    }
    return out;
}

const QHash<QString, ChordShape> &builtIn()
{
    static const QHash<QString, ChordShape> lib = [] {
        struct Def { const char *name, *frets, *fingers; };
        static const Def defs[] = {
            {"C", "x32010", "032010"},     {"Cmaj7", "x32000", "032000"},
            {"C7", "x32310", "032410"},    {"Cadd9", "x32033", "021034"},
            {"Cm", "x35543", "013421"},
            {"D", "xx0232", "000132"},     {"Dm", "xx0231", "000231"},
            {"D7", "xx0212", "000213"},    {"Dm7", "xx0211", "000211"},
            {"Dsus2", "xx0230", "000130"}, {"Dsus4", "xx0233", "000134"},
            {"D/F#", "2x0232", "T00132"},
            {"E", "022100", "023100"},     {"Em", "022000", "023000"},
            {"E7", "020100", "020100"},    {"Em7", "022030", "012030"},
            {"Esus4", "022200", "023400"},
            {"F", "133211", "134211"},     {"Fmaj7", "xx3210", "003210"},
            {"Fm", "133111", "134111"},    {"F#m", "244222", "134111"},
            {"F#", "244322", "134211"},
            {"G", "320003", "210003"},     {"G7", "320001", "320001"},
            {"G/B", "x20003", "010003"},   {"Gm", "355333", "134111"},
            {"Gsus4", "330013", "230014"},
            {"A", "x02220", "001230"},     {"Am", "x02210", "002310"},
            {"A7", "x02020", "002030"},    {"Am7", "x02010", "002010"},
            {"Asus2", "x02200", "001200"}, {"Asus4", "x02230", "001240"},
            {"Bb", "x13331", "013331"},    {"B", "x24442", "013331"},
            {"Bm", "x24432", "013421"},    {"B7", "x21202", "021304"},
            {"C#m", "x46654", "013421"},   {"G#m", "466444", "134111"},
            {"E5", "022xxx", "011000"},    {"A5", "x022xx", "001100"},
        };
        QHash<QString, ChordShape> h;
        for (const Def &d : defs) {
            ChordShape s;
            s.name = QString::fromLatin1(d.name);
            s.frets = *parseFrets(QString::fromLatin1(d.frets));
            s.fingers = *parseFingers(QString::fromLatin1(d.fingers));
            h.insert(s.name, s);
        }
        return h;
    }();
    return lib;
}

} // namespace ChordLibrary
