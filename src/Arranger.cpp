#include "Arranger.h"

#include "ChordLibrary.h"

#include <QHash>
#include <algorithm>
#include <cmath>

namespace Arranger {

double difficulty(const ChordShape &c);

namespace {

// Beginner-friendly replacements for barre chords (Simplify+): shape, fingers, and the name
// of what is really played (the stand-ins for F and B are Fmaj7 and B7).
struct EasyShape { const char *frets, *fingers, *name; };
const QHash<QString, EasyShape> &easyShapes()
{
    static const QHash<QString, EasyShape> shapes = {
        {QStringLiteral("F"), {"xx3210", "003210", "Fmaj7"}},   // sounds like F, no barre
        {QStringLiteral("Bm"), {"xx4432", "003421", "Bm"}},     // small Bm, top four strings
        {QStringLiteral("B"), {"x21202", "021304", "B7"}},
        {QStringLiteral("F#m"), {"xx4222", "003111", "F#m"}},
        {QStringLiteral("C#m"), {"xx2120", "002130", "C#m"}},
        {QStringLiteral("Fm"), {"xx3111", "003111", "Fm"}},
        {QStringLiteral("Gm"), {"xx5333", "003111", "Gm"}},
        {QStringLiteral("Bb"), {"xx3331", "003331", "Bb"}},
        {QStringLiteral("F#"), {"xx4321", "004321", "F#"}},
        {QStringLiteral("G#m"), {"xx6444", "003111", "G#m"}},
        {QStringLiteral("Cm"), {"xx5543", "003421", "Cm"}},
    };
    return shapes;
}

// The sounding chord (what you hear) of a shape played with a capo.
QString sounding(const QString &shapeName, int capo)
{
    return ChordName::transpose(shapeName, capo);
}

std::optional<ChordShape> shapeFor(const QString &english, ChordName::Level level)
{
    if (level == ChordName::Level::SimplifyPlus) {
        auto it = easyShapes().constFind(english);
        if (it != easyShapes().constEnd()) {
            ChordShape c;
            c.name = QString::fromLatin1(it->name);
            c.frets = *ChordLibrary::parseFrets(QString::fromLatin1(it->frets));
            c.fingers = *ChordLibrary::parseFingers(QString::fromLatin1(it->fingers));
            return c;
        }
    }
    auto c = ChordLibrary::lookup(english);
    if (c)
        c->name = english;
    return c;
}

// The chord to play at this level and its shape. Simplifying never makes a chord harder:
// an easy B7 stays B7 instead of becoming a barre B.
std::optional<ChordShape> chooseShape(const QString &english, ChordName::Level level)
{
    if (level == ChordName::Level::AsWritten)
        return shapeFor(english, level);
    const QString simple = ChordName::simplify(english);
    auto simplified = shapeFor(simple, level);
    if (simple == english)
        return simplified;
    auto original = shapeFor(english, level);
    if (original && (!simplified || difficulty(*original) + 0.25 < difficulty(*simplified)))
        return original;
    return simplified;
}

} // namespace

double difficulty(const ChordShape &c)
{
    int lo = 99, hi = 0, fretted = 0, muted = 0;
    for (int f : c.frets) {
        if (f < 0)
            ++muted;
        else if (f > 0) {
            lo = std::min(lo, f);
            hi = std::max(hi, f);
            ++fretted;
        }
    }
    double d = fretted * 0.3;
    // A barre: the same finger on several strings.
    for (int finger = 1; finger <= 4; ++finger) {
        int n = 0;
        for (int i = 0; i < 6; ++i)
            n += c.fingers[size_t(i)] == finger && c.frets[size_t(i)] > 0;
        if (n >= 2)
            d += n >= 4 ? 3.0 : 1.5;
    }
    if (hi > 0) {
        d += std::max(0, hi - lo - 2) * 0.8;  // stretch
        d += hi > 5 ? 1.0 : 0.0;              // high on the neck
    }
    // Muted strings in the middle of a strum are awkward.
    if (muted > 0 && c.frets[0] >= 0)
        d += 0.5;
    return d;
}

int easiestCapo(const Timeline &tl, ChordName::Level level)
{
    const int songCapo = tl.song ? tl.song->capo : 0;
    // Count how long each chord is played, so rare chords matter less.
    QHash<int, int> weight;
    for (const Timeline::Bar &b : tl.bars)
        for (int c : b.chordAtStep)
            if (c >= 0)
                ++weight[c];
    int best = songCapo;
    double bestCost = 1e30;
    int total = 0;
    for (int w : weight)
        total += w;
    for (int capo = 0; capo <= 7; ++capo) {
        // Prefer no or low capos: high ones shorten the neck and sound thin.
        double cost = total * (0.1 * capo + (capo > 5 ? 0.5 : 0.0));
        for (auto it = weight.constBegin(); it != weight.constEnd(); ++it) {
            const QString snd = sounding(ChordName::normalise(tl.chords[it.key()].name), songCapo);
            const auto shape = chooseShape(ChordName::transpose(snd, -capo), level);
            const double d = shape ? difficulty(*shape) : 20.0;
            cost += d * it.value();
        }
        if (cost < bestCost - 1e-9) {
            bestCost = cost;
            best = capo;
        }
    }
    return best;
}

std::shared_ptr<const Timeline> arrange(std::shared_ptr<const Timeline> tl, const Settings &s)
{
    if (!tl || !tl->song)
        return tl;
    const int songCapo = tl->song->capo;
    // Simplify+ moves the capo to where the chords are easiest, unless a capo was chosen.
    const bool autoCapo = s.capo == kCapoAuto || (s.capo == kCapoAsSong && s.level == ChordName::Level::SimplifyPlus);
    int capo = autoCapo ? easiestCapo(*tl, s.level) : s.capo == kCapoAsSong ? songCapo : s.capo;
    capo = std::clamp(capo, 0, 11);
    const bool english = ChordName::notation() == ChordName::Notation::English;
    if (capo == songCapo && s.level == ChordName::Level::AsWritten && (english || !s.displayNames))
        return tl;

    auto out = std::make_shared<Timeline>(*tl);
    auto song = std::make_shared<Song>(*tl->song);
    song->capo = capo;
    out->song = song;

    for (int i = 0; i < out->chords.size(); ++i) {
        const ChordShape &orig = tl->chords[i];
        ChordShape shape = orig;
        const QString englishName = ChordName::normalise(orig.name);
        const bool parsable = ChordName::parse(englishName).root >= 0;
        if (parsable && (capo != songCapo || s.level != ChordName::Level::AsWritten)) {
            const QString snd = sounding(englishName, songCapo);
            const QString name = ChordName::transpose(snd, -capo);
            if (auto sh = chooseShape(name, s.level)) {
                // Keep the song's own fingering when the chord didn't change, unless the
                // simplified voicing is easier to play.
                const bool same = capo == songCapo && sh->name == englishName;
                if (!same || difficulty(*sh) + 0.25 < difficulty(orig))
                    shape = *sh;
                shape.name = sh->name;
            } else if (capo != songCapo) {
                // Unknown chord: move the song's own shape if it fits on the neck.
                const int diff = songCapo - capo;
                bool ok = true;
                for (int f : orig.frets)
                    ok = ok && (f < 0 || (f + diff >= 0 && (f > 0 || diff == 0)));
                if (ok) {
                    for (int k = 0; k < 6; ++k)
                        if (shape.frets[size_t(k)] > 0)
                            shape.frets[size_t(k)] += diff;
                }
                shape.name = name;
            }
        }
        if (s.displayNames && !english)
            shape.name = ChordName::display(ChordName::normalise(shape.name));
        out->chords[i] = shape;
    }
    return out;
}

} // namespace Arranger
