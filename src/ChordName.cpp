#include "ChordName.h"

#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QStringList>

namespace ChordName {

namespace {

Notation g_notation = Notation::English;

// Common spellings guitarists use: C# F# G# as sharps, Eb Bb (and Ab only as bass) as flats.
const char *kPreferred[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"};

struct RootWord { QString word; int pc; };

// Longest words first so "Sol" wins over "So", "صول" before shorter matches.
const QList<RootWord> &rootWords()
{
    static const QList<RootWord> words = {
        {QStringLiteral("Sol"), 7}, {QStringLiteral("Do"), 0}, {QStringLiteral("Ré"), 2},
        {QStringLiteral("Re"), 2},  {QStringLiteral("Mi"), 4}, {QStringLiteral("Fa"), 5},
        {QStringLiteral("La"), 9},  {QStringLiteral("Si"), 11}, {QStringLiteral("Ti"), 11},
        {QStringLiteral("صول"), 7}, {QStringLiteral("سول"), 7}, {QStringLiteral("دو"), 0},
        {QStringLiteral("ري"), 2},  {QStringLiteral("رى"), 2},  {QStringLiteral("مي"), 4},
        {QStringLiteral("مى"), 4},  {QStringLiteral("فا"), 5},  {QStringLiteral("لا"), 9},
        {QStringLiteral("سي"), 11}, {QStringLiteral("سى"), 11},
    };
    return words;
}

// "Do..." and "Fa..." are solfège (C, F) unless they are English chords like "Fadd9" or "Faug".
bool isSolfegeDoFa(const QString &s)
{
    if (s.startsWith(QLatin1String("Do")))
        return true;
    return s.startsWith(QLatin1String("Fa")) && !s.startsWith(QLatin1String("Fadd"))
        && !s.startsWith(QLatin1String("Faug"));
}

// Reads a root at the start of s; returns its length (0 = none).
int readRoot(const QString &s, int *pc)
{
    if (!s.isEmpty() && s[0] >= QLatin1Char('A') && s[0] <= QLatin1Char('G') && !isSolfegeDoFa(s)) {
        static const int base[] = {9, 11, 0, 2, 4, 5, 7};
        *pc = base[s[0].unicode() - 'A'];
        return 1;
    }
    for (const RootWord &w : rootWords()) {
        if (s.startsWith(w.word)) {
            *pc = w.pc;
            return int(w.word.size());
        }
    }
    return 0;
}

// Reads sharp/flat signs after a root.
int readAccidental(const QString &s, int pos, int *shift)
{
    static const QList<QPair<QString, int>> signs = {
        {QStringLiteral(" دييز"), 1}, {QStringLiteral("دييز"), 1}, {QStringLiteral(" بيمول"), -1},
        {QStringLiteral("بيمول"), -1}, {QStringLiteral("#"), 1}, {QStringLiteral("♯"), 1},
        {QStringLiteral("♭"), -1}, {QStringLiteral("b"), -1},
    };
    for (const auto &sg : signs) {
        if (s.mid(pos).startsWith(sg.first)) {
            *shift = sg.second;
            return int(sg.first.size());
        }
    }
    return 0;
}

QString normaliseQuality(QString q)
{
    q = q.trimmed();
    static const QList<QPair<QString, QString>> words = {
        {QStringLiteral("مينور"), QStringLiteral("m")}, {QStringLiteral("ماينر"), QStringLiteral("m")},
        {QStringLiteral("ماجور"), QString()}, {QStringLiteral("ماجير"), QString()}, {QStringLiteral("ميجور"), QString()},
        {QStringLiteral("م"), QStringLiteral("m")}, {QStringLiteral("min"), QStringLiteral("m")},
        {QStringLiteral("mi"), QStringLiteral("m")}, {QStringLiteral("-"), QStringLiteral("m")},
        {QStringLiteral("maj"), QString()}, {QStringLiteral("M"), QString()},
    };
    // Whole-word replacements first ("Am7" stays, "A-7" -> "Am7", "Amin7" -> "Am7").
    static const QList<QPair<QString, QString>> exact = {
        {QStringLiteral("M7"), QStringLiteral("maj7")}, {QStringLiteral("Maj7"), QStringLiteral("maj7")},
        {QStringLiteral("Δ"), QStringLiteral("maj7")}, {QStringLiteral("Δ7"), QStringLiteral("maj7")},
        {QStringLiteral("sus"), QStringLiteral("sus4")}, {QStringLiteral("°"), QStringLiteral("dim")},
        {QStringLiteral("o"), QStringLiteral("dim")}, {QStringLiteral("+"), QStringLiteral("aug")},
        {QStringLiteral("ø"), QStringLiteral("m7b5")}, {QStringLiteral("dom7"), QStringLiteral("7")},
    };
    for (const auto &e : exact)
        if (q == e.first)
            return e.second;
    for (const auto &w : words) {
        if (q.startsWith(w.first)) {
            const QString rest = q.mid(w.first.size()).trimmed();
            // "maj7" must stay "maj7", not become "7".
            if (w.first == QLatin1String("maj") && rest.startsWith(QLatin1Char('7')))
                return QStringLiteral("maj") + rest;
            if (w.first == QLatin1String("M") && !rest.isEmpty() && rest[0].isDigit())
                return QStringLiteral("maj") + rest;
            if (w.first == QLatin1String("mi") && rest.startsWith(QLatin1Char('n')))
                continue;
            return w.second + rest;
        }
    }
    return q;
}

} // namespace

Parsed parse(const QString &raw)
{
    Parsed p;
    QString s = raw.trimmed();
    if (s.isEmpty() || s == QLatin1String("N.C.") || s == QLatin1String("NC"))
        return p;
    // Keep a trailing "(...)" label, e.g. "Em7(open)".
    const int paren = int(s.indexOf(QLatin1Char('(')));
    if (paren > 0) {
        p.suffix = s.mid(paren);
        s = s.left(paren).trimmed();
    }
    // Slash bass.
    const int slash = int(s.lastIndexOf(QLatin1Char('/')));
    if (slash > 0) {
        const QString bassPart = s.mid(slash + 1).trimmed();
        int bpc = -1;
        const int n = readRoot(bassPart, &bpc);
        if (n > 0) {
            int shift = 0;
            const int a = readAccidental(bassPart, n, &shift);
            if (n + a == bassPart.size()) {
                p.bass = ((bpc + shift) % 12 + 12) % 12;
                s = s.left(slash).trimmed();
            }
        }
    }
    int pc = -1;
    const int n = readRoot(s, &pc);
    if (n == 0)
        return p;
    int shift = 0;
    const int a = readAccidental(s, n, &shift);
    p.root = ((pc + shift) % 12 + 12) % 12;
    p.quality = normaliseQuality(s.mid(n + a));
    return p;
}

QStringList joinTokens(const QStringList &tokens)
{
    static const QRegularExpression qualityWord(QStringLiteral(
        "^(م|مينور|ماينر|ماجور|ماجير|ميجور|دييز|بيمول|#|b|m|min|maj|maj7|m7|7|9|6|m6|sus|sus2|sus4|7sus4|dim|dim7|aug|add9|5|m7b5)"
        "(:[0-9.]+)?$"));
    QStringList out;
    for (const QString &t : tokens) {
        if (!out.isEmpty() && qualityWord.match(t).hasMatch()) {
            const QString prev = out.last();
            const bool letter = !prev.isEmpty() && prev[0] >= QLatin1Char('A') && prev[0] <= QLatin1Char('G')
                    && !isSolfegeDoFa(prev);
            // Only spelled-out roots (solfège / Arabic) take a separate quality word.
            if (!letter && parse(prev).root >= 0 && !prev.contains(QLatin1Char(':'))) {
                out.last() = prev + QLatin1Char(' ') + t;
                continue;
            }
        }
        out << t;
    }
    return out;
}

QString toEnglish(const Parsed &p)
{
    if (p.root < 0)
        return QString();
    QString out = QString::fromLatin1(kPreferred[p.root]) + p.quality;
    if (p.bass >= 0)
        out += QLatin1Char('/') + QString::fromLatin1(kPreferred[p.bass]);
    return out + p.suffix;
}

QString normalise(const QString &name)
{
    // Already plain English letters: keep the writer's own sharp/flat spelling.
    if (!name.isEmpty() && name[0] >= QLatin1Char('A') && name[0] <= QLatin1Char('G') && !isSolfegeDoFa(name))
        return name;
    const Parsed p = parse(name);
    return p.root < 0 ? name : toEnglish(p);
}

QString transpose(const QString &name, int semitones)
{
    Parsed p = parse(name);
    if (p.root < 0 || semitones % 12 == 0)
        return name;
    p.root = ((p.root + semitones) % 12 + 12) % 12;
    if (p.bass >= 0)
        p.bass = ((p.bass + semitones) % 12 + 12) % 12;
    p.suffix.clear(); // "(open)" voicing labels don't survive a transposition
    return toEnglish(p);
}

QString simplify(const QString &name)
{
    Parsed p = parse(name);
    if (p.root < 0)
        return name;
    const QString q = p.quality;
    const bool minor = q.startsWith(QLatin1Char('m')) && !q.startsWith(QLatin1String("maj"));
    const bool diminished = q.startsWith(QLatin1String("dim")) || q == QLatin1String("m7b5");
    p.quality = (minor || diminished) ? QStringLiteral("m") : QString();
    p.bass = -1;
    p.suffix.clear();
    return toEnglish(p);
}

void setNotation(Notation n)
{
    g_notation = n;
}

Notation notation()
{
    return g_notation;
}

QString display(const QString &englishName)
{
    if (g_notation == Notation::English)
        return englishName;
    const Parsed p = parse(englishName);
    if (p.root < 0)
        return englishName;
    static const char *solfege[12] = {"Do", "Do#", "Ré", "Mi♭", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "Si♭", "Si"};
    static const QStringList arabic = {QStringLiteral("دو"), QStringLiteral("دو#"), QStringLiteral("ري"),
                                       QStringLiteral("مي♭"), QStringLiteral("مي"), QStringLiteral("فا"),
                                       QStringLiteral("فا#"), QStringLiteral("صول"), QStringLiteral("صول#"),
                                       QStringLiteral("لا"), QStringLiteral("سي♭"), QStringLiteral("سي")};
    auto rootName = [&](int pc) {
        return g_notation == Notation::Solfege ? QString::fromUtf8(solfege[pc]) : arabic[pc];
    };
    QString out = rootName(p.root);
    if (g_notation == Notation::Arabic) {
        // "لا م" reads naturally; minor keeps the Arabic "م", other qualities their usual symbols.
        QString q = p.quality;
        if (q.startsWith(QLatin1Char('m')) && !q.startsWith(QLatin1String("maj")))
            q = QStringLiteral("م") + q.mid(1);
        if (!q.isEmpty())
            out += QLatin1Char(' ') + q;
    } else {
        out += p.quality;
    }
    if (p.bass >= 0)
        out += QLatin1Char('/') + rootName(p.bass);
    return out + p.suffix;
}

} // namespace ChordName
