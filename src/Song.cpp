#include "Song.h"

#include "ChordName.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <cmath>

std::optional<ChordShape> Song::findChord(const QString &name) const
{
    for (const ChordShape &c : chords)
        if (c.name == name)
            return c;
    return ChordLibrary::lookup(name);
}

const QVector<PatternPreset> &patternPresets()
{
    static const QVector<PatternPreset> presets = {
        {"folk", 2, "D - D U - U D U", "The classic strum for almost any song in 4/4"},
        {"pop", 2, "D - D U D U D U", "Busier eighth-note strum"},
        {"rock", 2, "D D D D D D D D", "Driving down-strums on every eighth"},
        {"drive", 2, ">D - D U X U D U", "Folk strum with an accent and a muted chuck on 3"},
        {"ballad", 2, "D - - - D - D U", "Slow and open, for ballads"},
        {"whole", 2, ">D - - - - - - -", "One strum per bar (endings, very slow parts)"},
        {"half", 2, "D - - - D - - -", "Two strums per bar"},
        {"quarters", 2, "D - D - D - D -", "A down-strum on every beat"},
        {"reggae", 2, "- u - u - u - u", "Off-beat skank"},
        {"country", 2, "B - d u A - d u", "Bass note, strum, alternate bass, strum"},
        {"sixteenths", 4, ">D - d u X - u d - u d u X - d u", "Funky sixteenth-note strum"},
        {"arpeggio", 2, "B 3 2 3 1 3 2 3", "Picked chord, bass then up and down the strings"},
        {"arpeggio-slow", 1, "B 3 2 1", "Four picked notes per bar, for beginners"},
        {"travis", 2, "B+1 3 A 2 B 3 A 2", "Travis fingerpicking with alternating bass"},
        {"waltz", 2, "B - d u d u", "3/4: bass then two strums (use beatsPerBar=\"3\")"},
        {"waltz-pick", 2, "B 3 2 1 2 3", "3/4 picked (use beatsPerBar=\"3\")"},
        {"six-eight", 3, "D - U D - U", "6/8 strum (use beatsPerBar=\"2\")"},
        {"six-eight-pick", 3, "B 3 2 1 2 3", "6/8 arpeggio (use beatsPerBar=\"2\")"},
    };
    return presets;
}

const Pattern *Song::findPattern(const QString &name) const
{
    for (const Pattern &p : patterns)
        if (p.name == name)
            return &p;
    // Built-in presets are parsed once and shared.
    static const QVector<Pattern> builtIns = [] {
        QVector<Pattern> out;
        for (const PatternPreset &pr : patternPresets()) {
            Pattern p;
            p.name = QString::fromLatin1(pr.name);
            p.subdivision = pr.subdivision;
            parsePatternSteps(QString::fromLatin1(pr.steps), &p.steps, nullptr);
            out << p;
        }
        return out;
    }();
    for (const Pattern &p : builtIns)
        if (p.name.compare(name, Qt::CaseInsensitive) == 0)
            return &p;
    return nullptr;
}

int Song::sectionIndex(const QString &name) const
{
    for (int i = 0; i < sections.size(); ++i)
        if (sections[i].name == name)
            return i;
    return -1;
}

bool parsePatternSteps(const QString &text, QVector<PatternStep> *out, QString *error)
{
    out->clear();
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("[\\s|]+")), Qt::SkipEmptyParts);
    for (const QString &tok : tokens) {
        PatternStep st;
        st.token = tok;
        QString t = tok;
        if (t.startsWith(QLatin1Char('>'))) {
            st.accent = true;
            t.remove(0, 1);
        }
        if (t == QLatin1String("-") || t == QLatin1String(".")) {
            st.kind = PatternStep::Kind::Rest;
        } else if (t == QLatin1String("D") || t == QLatin1String("d")) {
            st.kind = PatternStep::Kind::Down;
            st.light = t == QLatin1String("d");
        } else if (t == QLatin1String("U") || t == QLatin1String("u")) {
            st.kind = PatternStep::Kind::Up;
            st.light = t == QLatin1String("u");
        } else if (t == QLatin1String("X") || t == QLatin1String("x")) {
            st.kind = PatternStep::Kind::Mute;
        } else {
            st.kind = PatternStep::Kind::Pick;
            for (const QString &p : t.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
                if (p == QLatin1String("B") || p == QLatin1String("b")) {
                    st.strings << 0;
                } else if (p == QLatin1String("A") || p == QLatin1String("a")) {
                    st.strings << -1;
                } else {
                    bool ok = false;
                    const int n = p.toInt(&ok);
                    if (!ok || n < 1 || n > 6) {
                        if (error)
                            *error = QStringLiteral("Unknown pattern token '%1'").arg(tok);
                        return false;
                    }
                    st.strings << n;
                }
            }
            if (st.strings.isEmpty()) {
                if (error)
                    *error = QStringLiteral("Unknown pattern token '%1'").arg(tok);
                return false;
            }
        }
        out->append(st);
    }
    if (out->isEmpty()) {
        if (error)
            *error = QStringLiteral("Pattern has no steps");
        return false;
    }
    return true;
}

namespace {

// "E2", "F#3", "Bb2" -> MIDI note number, or -1.
int noteToMidi(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^([A-Ga-g])([#b]?)(-?\\d)$"));
    const auto m = re.match(s.trimmed());
    if (!m.hasMatch())
        return -1;
    static const int base[] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
    int n = base[m.captured(1).toUpper()[0].unicode() - 'A'];
    if (m.captured(2) == QLatin1String("#"))
        ++n;
    else if (m.captured(2) == QLatin1String("b"))
        --n;
    return (m.captured(3).toInt() + 1) * 12 + n;
}

// Parses bar text: "C", "C G", "C:3 G:1". Multiple bars separated by '|'.
bool parseBars(const QString &text, QVector<BarDef> *out, QString *error)
{
    QStringList barTexts;
    if (text.contains(QLatin1Char('|')))
        barTexts = text.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    else
        barTexts = ChordName::joinTokens(text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts));

    for (const QString &bt : barTexts) {
        const QStringList toks = ChordName::joinTokens(
                bt.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts));
        if (toks.isEmpty())
            continue;
        BarDef bar;
        for (const QString &tok : toks) {
            BarDef::Slot slot;
            const int colon = tok.lastIndexOf(QLatin1Char(':'));
            if (colon > 0) {
                bool ok = false;
                slot.beats = tok.mid(colon + 1).toDouble(&ok);
                if (!ok || slot.beats <= 0) {
                    if (error)
                        *error = QStringLiteral("Bad beat length in '%1'").arg(tok);
                    return false;
                }
                slot.chord = tok.left(colon);
            } else {
                slot.chord = tok;
            }
            bar.parts << slot;
        }
        out->append(bar);
    }
    return true;
}

} // namespace

std::shared_ptr<Song> loadSong(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open %1: %2").arg(path, f.errorString());
        return nullptr;
    }
    return loadSongFromData(f.readAll(), path, error);
}

std::shared_ptr<Song> loadSongFromData(const QByteArray &data, const QString &path, QString *error)
{
    auto song = std::make_shared<Song>();
    song->filePath = path;
    QXmlStreamReader xml(data);
    QString err;
    Section *currentSection = nullptr;

    auto fail = [&](const QString &msg) {
        err = QStringLiteral("Line %1: %2").arg(xml.lineNumber()).arg(msg);
    };

    while (!xml.atEnd() && err.isEmpty()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QLatin1String("section")) {
            currentSection = nullptr;
            continue;
        }
        if (!xml.isStartElement())
            continue;

        const auto a = xml.attributes();
        const auto name = xml.name();

        if (name == QLatin1String("song")) {
            song->title = a.value(QLatin1String("title")).toString();
            song->artist = a.value(QLatin1String("artist")).toString();
            if (a.hasAttribute(QLatin1String("bpm")))
                song->bpm = a.value(QLatin1String("bpm")).toDouble();
            if (a.hasAttribute(QLatin1String("beatsPerBar")))
                song->beatsPerBar = a.value(QLatin1String("beatsPerBar")).toInt();
            if (a.hasAttribute(QLatin1String("capo")))
                song->capo = a.value(QLatin1String("capo")).toInt();
            if (a.hasAttribute(QLatin1String("pattern")))
                song->defaultPattern = a.value(QLatin1String("pattern")).toString();
            if (a.hasAttribute(QLatin1String("tuning"))) {
                const QStringList notes = a.value(QLatin1String("tuning")).toString()
                        .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                if (notes.size() != 6) {
                    fail(QStringLiteral("tuning needs 6 notes, e.g. \"E2 A2 D3 G3 B3 E4\""));
                    break;
                }
                for (int i = 0; i < 6; ++i) {
                    const int m = noteToMidi(notes[i]);
                    if (m < 0) {
                        fail(QStringLiteral("Bad tuning note '%1'").arg(notes[i]));
                        break;
                    }
                    song->tuning[i] = m;
                }
            }
            if (song->bpm <= 0 || song->beatsPerBar <= 0)
                fail(QStringLiteral("bpm and beatsPerBar must be positive"));
        } else if (name == QLatin1String("chord")) {
            ChordShape c;
            c.name = a.value(QLatin1String("name")).toString();
            const auto frets = ChordLibrary::parseFrets(a.value(QLatin1String("frets")).toString());
            if (c.name.isEmpty() || !frets) {
                fail(QStringLiteral("chord needs a name and 6 frets, e.g. frets=\"x32010\""));
                break;
            }
            c.frets = *frets;
            if (a.hasAttribute(QLatin1String("fingers"))) {
                const auto fingers = ChordLibrary::parseFingers(a.value(QLatin1String("fingers")).toString());
                if (!fingers) {
                    fail(QStringLiteral("Bad fingers for chord %1").arg(c.name));
                    break;
                }
                c.fingers = *fingers;
            }
            song->chords << c;
        } else if (name == QLatin1String("pattern")) {
            Pattern p;
            p.name = a.value(QLatin1String("name")).toString();
            p.subdivision = a.hasAttribute(QLatin1String("subdivision"))
                    ? a.value(QLatin1String("subdivision")).toInt() : 2;
            const int line = xml.lineNumber();
            const QString text = xml.readElementText(QXmlStreamReader::SkipChildElements);
            QString perr;
            if (p.name.isEmpty() || p.subdivision < 1 || p.subdivision > 8) {
                fail(QStringLiteral("pattern needs a name and subdivision 1..8"));
                break;
            }
            if (!parsePatternSteps(text, &p.steps, &perr)) {
                err = QStringLiteral("Line %1: pattern %2: %3").arg(line).arg(p.name, perr);
                break;
            }
            song->patterns << p;
        } else if (name == QLatin1String("section")) {
            Section s;
            s.name = a.value(QLatin1String("name")).toString();
            s.pattern = a.value(QLatin1String("pattern")).toString();
            s.bpm = a.value(QLatin1String("bpm")).toDouble();
            s.beatsPerBar = a.value(QLatin1String("beatsPerBar")).toInt();
            if (s.name.isEmpty()) {
                fail(QStringLiteral("section needs a name, e.g. <section name=\"Verse\" pattern=\"folk\">"));
                break;
            }
            song->sections << s;
            currentSection = &song->sections.last();
        } else if (name == QLatin1String("bar") || name == QLatin1String("bars")) {
            if (!currentSection) {
                fail(QStringLiteral("<%1> must be inside a <section>").arg(name.toString()));
                break;
            }
            const int repeat = a.hasAttribute(QLatin1String("repeat"))
                    ? a.value(QLatin1String("repeat")).toInt() : 1;
            const bool single = name == QLatin1String("bar");
            QString text = xml.readElementText(QXmlStreamReader::SkipChildElements);
            if (single)
                text.replace(QLatin1Char('|'), QLatin1Char(' '));
            QVector<BarDef> bars;
            QString berr;
            if (single) {
                QVector<BarDef> tmp;
                // A single <bar> keeps all its chords in one bar.
                if (!parseBars(text + QLatin1Char('|'), &tmp, &berr)) {
                    fail(berr);
                    break;
                }
                bars = tmp;
            } else if (!parseBars(text, &bars, &berr)) {
                fail(berr);
                break;
            }
            if (bars.isEmpty()) {
                fail(QStringLiteral("empty <%1>").arg(name.toString()));
                break;
            }
            for (int r = 0; r < qMax(1, repeat); ++r)
                currentSection->bars += bars;
        } else if (name == QLatin1String("line")) {
            // <line chords="G | D | Em | C">Almost | heaven, West | Vir- | ginia</line>
            if (!currentSection) {
                fail(QStringLiteral("<line> must be inside a <section>"));
                break;
            }
            const int sourceLine = int(xml.lineNumber());
            const QString chords = a.value(QLatin1String("chords")).toString();
            const int repeat = a.hasAttribute(QLatin1String("repeat"))
                    ? a.value(QLatin1String("repeat")).toInt() : 1;
            const QString text = xml.readElementText(QXmlStreamReader::SkipChildElements);
            QVector<BarDef> bars;
            QString berr;
            if (chords.trimmed().isEmpty()) {
                fail(QStringLiteral("<line> needs its chords, e.g. <line chords=\"G | D\">lyrics</line>"));
                break;
            }
            if (!parseBars(chords, &bars, &berr)) {
                fail(berr);
                break;
            }
            QStringList segments = text.split(QLatin1Char('|'));
            for (QString &seg : segments)
                seg = seg.simplified();
            // More lyric pieces than bars: the extra words go with the last bar.
            while (segments.size() > bars.size() && segments.size() > 1) {
                const QString last = segments.takeLast();
                segments.last() += QLatin1Char(' ') + last;
                segments.last() = segments.last().simplified();
            }
            for (int r = 0; r < qMax(1, repeat); ++r) {
                LyricLineDef line;
                line.firstBar = currentSection->bars.size();
                line.barCount = bars.size();
                line.sourceLine = sourceLine;
                line.split = segments.size() > 1;
                for (int b = 0; b < bars.size(); ++b) {
                    BarDef bar = bars[b];
                    bar.lyric = b < segments.size() ? segments[b] : QString();
                    bar.lyricLine = currentSection->lines.size();
                    currentSection->bars << bar;
                }
                currentSection->lines << line;
            }
        } else if (name == QLatin1String("audio")) {
            const QString file = a.value(QLatin1String("file")).toString();
            if (file.isEmpty()) {
                fail(QStringLiteral("<audio> needs a file, e.g. <audio file=\"song.mp3\" offset=\"0.5\"/>"));
                break;
            }
            const QFileInfo fi(file);
            song->audioFile = fi.isAbsolute() ? file
                                              : QFileInfo(path).absoluteDir().absoluteFilePath(file);
            song->audioOffset = a.value(QLatin1String("offset")).toDouble();
        } else if (name == QLatin1String("play")) {
            ArrangementItem it;
            it.section = a.value(QLatin1String("section")).toString();
            it.repeat = a.hasAttribute(QLatin1String("repeat")) ? a.value(QLatin1String("repeat")).toInt() : 1;
            if (it.section.isEmpty() || it.repeat < 1) {
                fail(QStringLiteral("play needs a section and repeat >= 1"));
                break;
            }
            song->arrangement << it;
        } else if (name != QLatin1String("chords") && name != QLatin1String("patterns")
                   && name != QLatin1String("sections") && name != QLatin1String("arrangement")) {
            fail(QStringLiteral("Unknown tag <%1>. Allowed tags: song, chords, chord, patterns, pattern, "
                                "sections, section, bars, bar, line, arrangement, play, audio").arg(name.toString()));
        }
    }

    if (err.isEmpty() && xml.hasError())
        err = QStringLiteral("Line %1: %2").arg(xml.lineNumber()).arg(xml.errorString());
    if (!err.isEmpty()) {
        if (error)
            *error = err;
        return nullptr;
    }

    if (song->title.isEmpty())
        song->title = QFileInfo(path).completeBaseName();
    return song;
}

const PatternStep *Timeline::stepAt(int bar, int step) const
{
    if (bar < 0 || bar >= bars.size())
        return nullptr;
    const Bar &b = bars[bar];
    if (b.pattern < 0)
        return nullptr;
    const Pattern &p = patterns[b.pattern];
    const int idx = (b.barInSection * b.stepCount() + step) % p.steps.size();
    return &p.steps[idx];
}

std::shared_ptr<Timeline> buildTimeline(std::shared_ptr<const Song> song, QString *error)
{
    auto tl = std::make_shared<Timeline>();
    tl->song = song;
    QHash<QString, int> chordIndex;
    QStringList unknown;

    auto resolveChord = [&](const QString &name) -> int {
        if (name == QLatin1String("N.C.") || name == QLatin1String("NC") || name == QLatin1String("-"))
            return -1;
        auto it = chordIndex.constFind(name);
        if (it != chordIndex.constEnd())
            return *it;
        const std::optional<ChordShape> shape = song->findChord(name);
        if (!shape) {
            if (!unknown.contains(name))
                unknown << name;
            return -1;
        }
        tl->chords << *shape;
        chordIndex.insert(name, tl->chords.size() - 1);
        return tl->chords.size() - 1;
    };

    QVector<ArrangementItem> arrangement = song->arrangement;
    if (arrangement.isEmpty())
        for (const Section &s : song->sections)
            arrangement << ArrangementItem{s.name, 1};

    int lastChord = -1;
    for (const ArrangementItem &item : arrangement) {
        const int si = song->sectionIndex(item.section);
        if (si < 0) {
            if (error)
                *error = QStringLiteral("Arrangement refers to unknown section '%1'").arg(item.section);
            return nullptr;
        }
        const Section &sec = song->sections[si];
        const QString patternName = sec.pattern.isEmpty() ? song->defaultPattern : sec.pattern;
        const Pattern *found = song->findPattern(patternName);
        if (!found) {
            QStringList names;
            for (const PatternPreset &pr : patternPresets())
                names << QString::fromLatin1(pr.name);
            if (error)
                *error = QStringLiteral("Section '%1' uses unknown pattern '%2'. Define it with <pattern>, "
                                        "or use a built-in one: %3")
                        .arg(sec.name, patternName, names.join(QStringLiteral(", ")));
            return nullptr;
        }
        int pi = -1;
        for (int i = 0; i < tl->patterns.size(); ++i)
            if (tl->patterns[i].name == found->name)
                pi = i;
        if (pi < 0) {
            tl->patterns << *found;
            pi = tl->patterns.size() - 1;
        }
        if (sec.bars.isEmpty()) {
            if (error)
                *error = QStringLiteral("Section '%1' has no bars").arg(sec.name);
            return nullptr;
        }
        const Pattern &pat = tl->patterns[pi];

        for (int pass = 1; pass <= item.repeat; ++pass) {
            Timeline::Play play;
            play.section = sec.name;
            play.pass = pass;
            play.passes = item.repeat;
            play.firstBar = tl->bars.size();
            play.barCount = sec.bars.size();
            const int playIndex = tl->plays.size();
            tl->plays << play;
            const int lineBase = tl->lyricLines.size();
            for (const LyricLineDef &def : sec.lines)
                tl->lyricLines << Timeline::LyricLine{play.firstBar + def.firstBar, def.barCount, def.sourceLine, def.split};

            for (int b = 0; b < sec.bars.size(); ++b) {
                Timeline::Bar bar;
                bar.play = playIndex;
                bar.barInSection = b;
                bar.bpm = sec.bpm > 0 ? sec.bpm : song->bpm;
                bar.beatsPerBar = sec.beatsPerBar > 0 ? sec.beatsPerBar : song->beatsPerBar;
                bar.subdivision = pat.subdivision;
                bar.pattern = pi;
                if (sec.bars[b].lyricLine >= 0) {
                    bar.lyricLine = lineBase + sec.bars[b].lyricLine;
                    bar.lyric = sec.bars[b].lyric;
                }
                const int steps = bar.stepCount();
                bar.chordAtStep.fill(-1, steps);

                // Work out how many beats each chord slot lasts.
                const BarDef &def = sec.bars[b];
                double fixed = 0;
                int flexible = 0;
                for (const auto &slot : def.parts) {
                    if (slot.beats > 0)
                        fixed += slot.beats;
                    else
                        ++flexible;
                }
                const double flexBeats = flexible ? qMax(0.0, bar.beatsPerBar - fixed) / flexible : 0.0;
                double beat = 0;
                for (const auto &slot : def.parts) {
                    const QString name = slot.chord == QLatin1String("%") && lastChord >= 0
                            ? tl->chords[lastChord].name : slot.chord;
                    const int ci = name == QLatin1String("%") ? -1 : resolveChord(name);
                    const int start = qBound(0, int(std::lround(beat * bar.subdivision)), steps);
                    for (int s = start; s < steps; ++s)
                        bar.chordAtStep[s] = ci;
                    beat += slot.beats > 0 ? slot.beats : flexBeats;
                    lastChord = ci;
                }
                tl->bars << bar;
            }
        }
    }

    if (!unknown.isEmpty()) {
        if (error)
            *error = QStringLiteral("Unknown chord(s): %1. Check the spelling, or define the fingering with "
                                    "<chord name=\"..\" frets=\"x32010\"/> inside <chords>.")
                    .arg(unknown.join(QStringLiteral(", ")));
        return nullptr;
    }
    if (tl->bars.isEmpty()) {
        if (error)
            *error = QStringLiteral("Song has no bars to play");
        return nullptr;
    }
    return tl;
}
