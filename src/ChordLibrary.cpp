#include "ChordLibrary.h"

#include <QRegularExpression>
#include <QStringList>

#include <iterator>

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

namespace {

// Pitch class of a root note like "C", "F#", "Bb", or -1.
int rootPitch(const QString &root)
{
    static const int base[] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
    if (root.isEmpty() || root[0] < QLatin1Char('A') || root[0] > QLatin1Char('G'))
        return -1;
    int pc = base[root[0].unicode() - 'A'];
    if (root.size() > 1)
        pc += root[1] == QLatin1Char('#') ? 1 : -1;
    return (pc + 12) % 12;
}

// Splits "F#m7" into root "F#" and quality "m7", normalising common spellings.
bool splitChord(const QString &name, QString *root, QString *quality)
{
    static const QRegularExpression re(QStringLiteral("^([A-G])([#b]?)(.*)$"));
    const auto m = re.match(name.trimmed());
    if (!m.hasMatch())
        return false;
    *root = m.captured(1) + m.captured(2);
    QString q = m.captured(3);
    static const QList<QPair<QString, QString>> aliases = {
        {QStringLiteral("min7"), QStringLiteral("m7")}, {QStringLiteral("-7"), QStringLiteral("m7")},
        {QStringLiteral("M7"), QStringLiteral("maj7")}, {QStringLiteral("Maj7"), QStringLiteral("maj7")},
        {QStringLiteral("min"), QStringLiteral("m")},   {QStringLiteral("mi"), QStringLiteral("m")},
        {QStringLiteral("-"), QStringLiteral("m")},     {QStringLiteral("maj"), QString()},
        {QStringLiteral("M"), QString()},               {QStringLiteral("sus"), QStringLiteral("sus4")},
        {QStringLiteral("dom7"), QStringLiteral("7")},  {QStringLiteral("°"), QStringLiteral("dim")},
        {QStringLiteral("+"), QStringLiteral("aug")},
    };
    for (const auto &a : aliases) {
        if (q == a.first) {
            q = a.second;
            break;
        }
    }
    *quality = q;
    return true;
}

// Movable barre shapes. Offsets from the barre fret, low E first.
struct Template { const char *quality; int frets[6]; int fingers[6]; };

constexpr int X = -99; // muted string

const Template eShapes[] = {
    {"",     {0, 2, 2, 1, 0, 0},   {1, 3, 4, 2, 1, 1}},
    {"m",    {0, 2, 2, 0, 0, 0},   {1, 3, 4, 1, 1, 1}},
    {"7",    {0, 2, 0, 1, 0, 0},   {1, 3, 1, 2, 1, 1}},
    {"m7",   {0, 2, 0, 0, 0, 0},   {1, 3, 1, 1, 1, 1}},
    {"maj7", {0, X, 1, 1, 0, X}, {1, 0, 3, 4, 2, 0}},
    {"sus4", {0, 2, 2, 2, 0, 0},   {1, 2, 3, 4, 1, 1}},
    {"5",    {0, 2, 2, X, X, X},{1, 3, 4, 0, 0, 0}},
    {"6",    {0, X, X, 1, 2, 0}, {1, 0, 0, 2, 3, 1}},
    {"m6",   {0, 2, 2, 0, 2, 0},   {1, 2, 3, 1, 4, 1}},
    {"9",    {0, 2, 0, 1, 0, 2},   {1, 3, 1, 2, 1, 4}},
    {"dim",  {0, 1, 2, 0, X, X}, {1, 2, 4, 1, 0, 0}},
    {"aug",  {0, 3, 2, 1, 1, 0},   {1, 4, 3, 2, 2, 1}},
};
const Template aShapes[] = {
    {"",     {X, 0, 2, 2, 2, 0},  {0, 1, 3, 3, 3, 1}},
    {"m",    {X, 0, 2, 2, 1, 0},  {0, 1, 3, 4, 2, 1}},
    {"7",    {X, 0, 2, 0, 2, 0},  {0, 1, 3, 1, 4, 1}},
    {"m7",   {X, 0, 2, 0, 1, 0},  {0, 1, 3, 1, 2, 1}},
    {"maj7", {X, 0, 2, 1, 2, 0},  {0, 1, 3, 2, 4, 1}},
    {"sus2", {X, 0, 2, 2, 0, 0},  {0, 1, 3, 4, 1, 1}},
    {"sus4", {X, 0, 2, 2, 3, 0},  {0, 1, 2, 3, 4, 1}},
    {"5",    {X, 0, 2, 2, X, X},{0, 1, 3, 4, 0, 0}},
    {"6",    {X, 0, 2, 2, 2, 2},  {0, 1, 3, 3, 3, 3}},
    {"m6",   {X, 0, 2, X, 1, 2}, {0, 1, 3, 0, 2, 4}},
    {"9",    {X, 0, -1, 0, 0, 0},  {0, 2, 1, 3, 3, 3}},
    {"dim",  {X, 0, 1, 2, 1, X}, {0, 1, 2, 4, 3, 0}},
    {"aug",  {X, 0, 3, 2, 2, 1},  {0, 1, 4, 2, 3, 1}},
};

std::optional<ChordShape> fromTemplate(const Template *list, int count, const QString &quality, int fret)
{
    for (int i = 0; i < count; ++i) {
        if (quality != QLatin1String(list[i].quality))
            continue;
        ChordShape c;
        for (int s = 0; s < 6; ++s) {
            c.frets[size_t(s)] = list[i].frets[s] == X ? -1 : fret + list[i].frets[s];
            c.fingers[size_t(s)] = list[i].frets[s] == X ? 0 : list[i].fingers[s];
        }
        return c;
    }
    return std::nullopt;
}

} // namespace

std::optional<ChordShape> lookup(const QString &rawName)
{
    const QString name = rawName.trimmed();
    const auto &lib = builtIn();
    auto found = [&](const QString &key) -> std::optional<ChordShape> {
        auto it = lib.constFind(key);
        if (it == lib.constEnd())
            return std::nullopt;
        ChordShape c = *it;
        c.name = name;
        return c;
    };
    if (auto c = found(name))
        return c;

    // Slash chord: try the full name's pieces, keep the displayed name.
    QString main = name;
    const int slash = name.indexOf(QLatin1Char('/'));
    if (slash > 0)
        main = name.left(slash);

    QString root, quality;
    if (!splitChord(main, &root, &quality))
        return std::nullopt;
    const int pc = rootPitch(root);
    if (pc < 0)
        return std::nullopt;

    // Same chord with a different spelling of the root (A# = Bb) or quality (Amin = Am).
    static const char *sharpNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *flatNames[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    for (const char *r : {sharpNames[pc], flatNames[pc]})
        if (auto c = found(QLatin1String(r) + quality))
            return c;

    // Generate a barre chord, choosing whichever of the E or A shape sits lower on the neck.
    int eFret = (pc - 4 + 12) % 12;
    int aFret = (pc - 9 + 12) % 12;
    if (eFret == 0)
        eFret = 12;
    if (aFret == 0)
        aFret = 12;
    auto e = fromTemplate(eShapes, int(std::size(eShapes)), quality, eFret);
    auto a = fromTemplate(aShapes, int(std::size(aShapes)), quality, aFret);
    std::optional<ChordShape> best;
    if (e && a)
        best = eFret <= aFret ? e : a;
    else
        best = e ? e : a;
    if (best)
        best->name = name;
    return best;
}

} // namespace ChordLibrary
