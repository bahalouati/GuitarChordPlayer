#include "MainWindow.h"

#include "ChordDiagramWidget.h"
#include "PatternWidget.h"
#include "WavWriter.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("Guitar Chord Player"));
    m_engine = new AudioEngine(this);
    QString err;
    m_audioOk = m_engine->start(&err);

    buildUi();
    buildMenus();

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        // Editors often save by replace; re-add the path and reload after a short delay.
        QTimer::singleShot(200, this, [this, path] {
            if (m_song && m_song->filePath == path)
                loadSongFile(path, true);
        });
    });

    QSettings s;
    m_tempo->setValue(s.value(QStringLiteral("tempo"), 100).toInt());
    m_volume->setValue(s.value(QStringLiteral("volume"), 80).toInt());
    m_metronome->setChecked(s.value(QStringLiteral("metronome"), false).toBool());
    m_countIn->setChecked(s.value(QStringLiteral("countIn"), true).toBool());
    restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray());

    refreshSongList();
    const QString last = s.value(QStringLiteral("lastSong")).toString();
    if (!last.isEmpty() && QFileInfo::exists(last))
        loadSongFile(last);
    else if (m_songList->count() > 0)
        loadSongFile(m_songList->item(0)->data(Qt::UserRole).toString());

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::updateView);
    m_timer->start(16);

    if (!m_audioOk)
        QTimer::singleShot(0, this, [this, err] {
            QMessageBox::warning(this, tr("Audio"), tr("Audio output is not available:\n%1").arg(err));
        });
}

MainWindow::~MainWindow()
{
    QSettings s;
    s.setValue(QStringLiteral("tempo"), m_tempo->value());
    s.setValue(QStringLiteral("volume"), m_volume->value());
    s.setValue(QStringLiteral("metronome"), m_metronome->isChecked());
    s.setValue(QStringLiteral("countIn"), m_countIn->isChecked());
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    if (m_song)
        s.setValue(QStringLiteral("lastSong"), m_song->filePath);
}

void MainWindow::buildUi()
{
    auto *splitter = new QSplitter(this);

    // Left: song library and arrangement
    auto *left = new QWidget;
    auto *ll = new QVBoxLayout(left);
    ll->addWidget(new QLabel(tr("<b>Songs</b>")));
    m_songList = new QListWidget;
    ll->addWidget(m_songList, 2);
    ll->addWidget(new QLabel(tr("<b>Arrangement</b> (click to jump)")));
    m_sectionList = new QListWidget;
    ll->addWidget(m_sectionList, 3);
    splitter->addWidget(left);

    connect(m_songList, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) {
        loadSongFile(it->data(Qt::UserRole).toString());
    });
    connect(m_songList, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        const QString path = it->data(Qt::UserRole).toString();
        if (!m_song || m_song->filePath != path)
            loadSongFile(path);
    });
    connect(m_sectionList, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        jumpToPlay(m_sectionList->row(it));
    });

    // Right: live view
    auto *right = new QWidget;
    auto *rl = new QVBoxLayout(right);
    m_title = new QLabel;
    QFont tf = m_title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.8);
    tf.setBold(true);
    m_title->setFont(tf);
    m_info = new QLabel;
    m_section = new QLabel;
    QFont sf = m_section->font();
    sf.setPointSizeF(sf.pointSizeF() * 1.5);
    m_section->setFont(sf);
    m_section->setStyleSheet(QStringLiteral("color: rgb(255,140,40);"));
    rl->addWidget(m_title);
    rl->addWidget(m_info);
    rl->addWidget(m_section);

    auto *chords = new QHBoxLayout;
    m_current = new ChordDiagramWidget;
    m_current->setCaption(tr("Now"));
    m_next = new ChordDiagramWidget;
    m_next->setCaption(tr("Next"));
    m_next->setDimmed(true);
    chords->addStretch(1);
    chords->addWidget(m_current, 3);
    chords->addSpacing(20);
    chords->addWidget(m_next, 2);
    chords->addStretch(1);
    rl->addLayout(chords, 5);

    m_pattern = new PatternWidget;
    rl->addWidget(m_pattern, 2);

    // Transport
    auto *tr1 = new QHBoxLayout;
    m_playBtn = new QPushButton;
    m_playBtn->setFocusPolicy(Qt::NoFocus);
    m_playBtn->setMinimumWidth(90);
    setPlayButton(false);
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::togglePlay);
    auto *stopBtn = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop), tr("Stop"));
    stopBtn->setFocusPolicy(Qt::NoFocus);
    connect(stopBtn, &QPushButton::clicked, this, &MainWindow::stop);
    auto *prevBtn = new QPushButton(style()->standardIcon(QStyle::SP_MediaSkipBackward), QString());
    prevBtn->setToolTip(tr("Previous section (Left)"));
    prevBtn->setFocusPolicy(Qt::NoFocus);
    connect(prevBtn, &QPushButton::clicked, this, &MainWindow::previousSection);
    auto *nextBtn = new QPushButton(style()->standardIcon(QStyle::SP_MediaSkipForward), QString());
    nextBtn->setToolTip(tr("Next section (Right)"));
    nextBtn->setFocusPolicy(Qt::NoFocus);
    connect(nextBtn, &QPushButton::clicked, this, &MainWindow::nextSection);
    tr1->addWidget(prevBtn);
    tr1->addWidget(m_playBtn);
    tr1->addWidget(stopBtn);
    tr1->addWidget(nextBtn);
    tr1->addSpacing(16);

    tr1->addWidget(new QLabel(tr("Tempo")));
    m_tempo = new QSlider(Qt::Horizontal);
    m_tempo->setRange(25, 150);
    m_tempo->setValue(100);
    m_tempo->setFocusPolicy(Qt::NoFocus);
    m_tempo->setMinimumWidth(140);
    connect(m_tempo, &QSlider::valueChanged, this, &MainWindow::applyTempo);
    tr1->addWidget(m_tempo, 1);
    m_tempoLabel = new QLabel;
    m_tempoLabel->setMinimumWidth(110);
    tr1->addWidget(m_tempoLabel);

    m_loop = new QCheckBox(tr("Loop section"));
    m_loop->setFocusPolicy(Qt::NoFocus);
    connect(m_loop, &QCheckBox::toggled, this, &MainWindow::applyLoop);
    m_metronome = new QCheckBox(tr("Metronome"));
    m_metronome->setFocusPolicy(Qt::NoFocus);
    connect(m_metronome, &QCheckBox::toggled, m_engine, &AudioEngine::setMetronome);
    m_countIn = new QCheckBox(tr("Count-in"));
    m_countIn->setFocusPolicy(Qt::NoFocus);
    connect(m_countIn, &QCheckBox::toggled, m_engine, &AudioEngine::setCountIn);
    auto *tr2 = new QHBoxLayout;
    tr2->addWidget(m_loop);
    tr2->addWidget(m_metronome);
    tr2->addWidget(m_countIn);
    tr2->addStretch(1);
    tr2->addWidget(new QLabel(tr("Volume")));
    m_volume = new QSlider(Qt::Horizontal);
    m_volume->setRange(0, 100);
    m_volume->setFocusPolicy(Qt::NoFocus);
    m_volume->setFixedWidth(140);
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) { m_engine->setVolume(v / 100.f); });
    tr2->addWidget(m_volume);
    rl->addLayout(tr1);
    rl->addLayout(tr2);

    splitter->addWidget(right);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);
    splitter->setSizes({220, 900});
    setCentralWidget(splitter);

    m_status = new QLabel;
    statusBar()->addWidget(m_status, 1);
    resize(1180, 760);
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Open song..."), QKeySequence::Open, this, &MainWindow::openSong);
    file->addAction(tr("&Reload song"), QKeySequence(Qt::Key_F5), this, &MainWindow::reloadSong);
    file->addAction(tr("Refresh song &list"), this, &MainWindow::refreshSongList);
    file->addAction(tr("Open songs &folder"), this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(songsDir()));
    });
    file->addSeparator();
    file->addAction(tr("&Export as WAV..."), this, &MainWindow::exportWav);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, qApp, &QApplication::quit);

    QMenu *play = menuBar()->addMenu(tr("&Playback"));
    play->addAction(tr("Play / Pause"), QKeySequence(Qt::Key_Space), this, &MainWindow::togglePlay);
    play->addAction(tr("Stop"), QKeySequence(Qt::Key_Escape), this, &MainWindow::stop);
    play->addAction(tr("Previous section"), QKeySequence(Qt::Key_Left), this, &MainWindow::previousSection);
    play->addAction(tr("Next section"), QKeySequence(Qt::Key_Right), this, &MainWindow::nextSection);
    play->addSeparator();
    play->addAction(tr("Faster (+5%)"), QKeySequence(Qt::Key_Up), this, [this] { m_tempo->setValue(m_tempo->value() + 5); });
    play->addAction(tr("Slower (-5%)"), QKeySequence(Qt::Key_Down), this, [this] { m_tempo->setValue(m_tempo->value() - 5); });
    play->addAction(tr("Reset tempo"), QKeySequence(Qt::Key_0), this, [this] { m_tempo->setValue(100); });
    play->addSeparator();
    play->addAction(tr("Toggle loop section"), QKeySequence(Qt::Key_L), m_loop, &QCheckBox::toggle);
    play->addAction(tr("Toggle metronome"), QKeySequence(Qt::Key_M), m_metronome, &QCheckBox::toggle);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Song XML format..."), this, &MainWindow::showFormatHelp);
    help->addAction(tr("About"), this, [this] {
        QMessageBox::about(this, tr("About Guitar Chord Player"),
                           tr("<b>Guitar Chord Player</b><br>Plays chord charts from XML files with a "
                              "synthesized (Karplus-Strong) guitar so you can see and hear how songs "
                              "are strummed and picked."));
    });
}

QString MainWindow::songsDir() const
{
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates = {app + QStringLiteral("/songs"), app + QStringLiteral("/../songs"),
                                    app + QStringLiteral("/../../songs"), QDir::currentPath() + QStringLiteral("/songs")};
    for (const QString &c : candidates)
        if (QDir(c).exists())
            return QDir(c).absolutePath();
    return app + QStringLiteral("/songs");
}

void MainWindow::refreshSongList()
{
    m_songList->clear();
    QDir dir(songsDir());
    const QStringList files = dir.entryList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        const QString path = dir.absoluteFilePath(f);
        QString err;
        auto song = loadSong(path, &err);
        auto *it = new QListWidgetItem(song ? song->title : f + tr(" (error)"));
        it->setData(Qt::UserRole, path);
        if (song && !song->artist.isEmpty())
            it->setToolTip(song->artist);
        else if (!song)
            it->setToolTip(err);
        m_songList->addItem(it);
    }
}

bool MainWindow::loadSongFile(const QString &path, bool keepPosition)
{
    QString err;
    auto song = loadSong(path, &err);
    std::shared_ptr<Timeline> tl;
    if (song)
        tl = buildTimeline(song, &err);
    if (!tl) {
        if (keepPosition) {
            // Keep playing the old version while the file is being edited.
            m_status->setText(tr("Reload failed: %1").arg(err));
        } else {
            QMessageBox::warning(this, tr("Cannot load song"), QFileInfo(path).fileName() + QStringLiteral("\n\n") + err);
        }
        if (!m_watcher->files().contains(path) && QFileInfo::exists(path))
            m_watcher->addPath(path);
        return false;
    }

    const int oldBar = m_viewBar;
    const bool wasPlaying = m_engine->isPlaying();
    m_song = song;
    m_timeline = tl;
    m_engine->setTimeline(tl);
    m_pattern->setTimeline(tl);
    applyTempo();

    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    m_watcher->addPath(path);

    m_title->setText(song->title);
    QString info = song->artist;
    if (!info.isEmpty())
        info += QStringLiteral("  ·  ");
    info += tr("%1 BPM  ·  %2 beats per bar").arg(song->bpm).arg(song->beatsPerBar);
    if (song->capo > 0)
        info += tr("  ·  Capo %1").arg(song->capo);
    m_info->setText(info);
    setWindowTitle(tr("%1 - Guitar Chord Player").arg(song->title));

    m_sectionList->clear();
    for (const Timeline::Play &p : tl->plays) {
        QString label = p.section;
        if (p.passes > 1)
            label += QStringLiteral("  (%1/%2)").arg(p.pass).arg(p.passes);
        label += tr("  - %n bar(s)", nullptr, p.barCount);
        m_sectionList->addItem(label);
    }
    for (int i = 0; i < m_songList->count(); ++i)
        if (m_songList->item(i)->data(Qt::UserRole).toString() == path)
            m_songList->setCurrentRow(i);

    m_viewBar = 0;
    if (keepPosition && oldBar < tl->bars.size()) {
        m_engine->seekToBar(oldBar);
        if (wasPlaying)
            m_engine->play();
        m_status->setText(tr("Reloaded %1").arg(QFileInfo(path).fileName()));
    } else {
        m_status->setText(tr("Loaded %1  ·  %n bar(s)", nullptr, tl->bars.size()).arg(QFileInfo(path).fileName()));
        setPlayButton(false);
    }
    applyLoop();
    return true;
}

void MainWindow::setPlayButton(bool playing)
{
    m_playBtn->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    m_playBtn->setText(playing ? tr("Pause") : tr("Play"));
}

void MainWindow::togglePlay()
{
    if (!m_timeline)
        return;
    if (m_engine->isPlaying()) {
        m_engine->pause();
        setPlayButton(false);
    } else {
        m_engine->play();
        setPlayButton(true);
    }
}

void MainWindow::stop()
{
    m_engine->stop();
    setPlayButton(false);
}

int MainWindow::currentPlay() const
{
    if (!m_timeline || m_viewBar < 0 || m_viewBar >= m_timeline->bars.size())
        return 0;
    return m_timeline->bars[m_viewBar].play;
}

void MainWindow::jumpToPlay(int play)
{
    if (!m_timeline || play < 0 || play >= m_timeline->plays.size())
        return;
    m_viewBar = m_timeline->plays[play].firstBar;
    m_viewStep = 0;
    applyLoop();
    m_engine->seekToBar(m_viewBar);
}

void MainWindow::previousSection()
{
    const int cur = currentPlay();
    // Like a CD player: go to the start of this section, or the previous one if near the start.
    if (m_timeline && m_viewBar == m_timeline->plays[cur].firstBar)
        jumpToPlay(cur - 1);
    else
        jumpToPlay(cur);
}

void MainWindow::nextSection()
{
    jumpToPlay(currentPlay() + 1);
}

void MainWindow::applyTempo()
{
    const double scale = m_tempo->value() / 100.0;
    m_engine->setTempoScale(scale);
    double bpm = m_song ? m_song->bpm : 0;
    if (m_timeline && m_viewBar >= 0 && m_viewBar < m_timeline->bars.size())
        bpm = m_timeline->bars[m_viewBar].bpm;
    m_tempoLabel->setText(tr("%1 BPM (%2%)").arg(qRound(bpm * scale)).arg(m_tempo->value()));
}

void MainWindow::applyLoop()
{
    if (!m_timeline || !m_loop->isChecked()) {
        m_engine->setLoop(-1, -1);
        return;
    }
    const Timeline::Play &p = m_timeline->plays[currentPlay()];
    m_engine->setLoop(p.firstBar, p.firstBar + p.barCount - 1);
}

void MainWindow::updateView()
{
    if (!m_timeline)
        return;
    const Sequencer::Snapshot s = m_engine->snapshot();
    const auto &bars = m_timeline->bars;
    const int bar = std::clamp(s.bar, 0, int(bars.size()) - 1);
    const int prevPlay = currentPlay();
    const int prevBar = m_viewBar;
    m_viewBar = bar;
    m_viewStep = s.step;
    const Timeline::Bar &b = bars[bar];
    const Timeline::Play &play = m_timeline->plays[b.play];

    if (s.finished)
        setPlayButton(false);
    if (bar != prevBar)
        applyTempo();

    // Section header
    QString header = play.section;
    if (play.passes > 1)
        header += QStringLiteral(" (%1/%2)").arg(play.pass).arg(play.passes);
    header += tr("   ·   bar %1 of %2").arg(b.barInSection + 1).arg(play.barCount);
    if (s.countIn)
        header = tr("Get ready...  %1").arg(s.countInBeat + 1);
    else if (s.finished)
        header = tr("Finished - press Play to start again");
    m_section->setText(header);

    if (b.play != prevPlay) {
        m_sectionList->setCurrentRow(b.play);
        if (m_loop->isChecked() && !s.playing)
            applyLoop();
    }
    if (m_sectionList->currentRow() != b.play)
        m_sectionList->setCurrentRow(b.play);

    // Chord diagrams
    const int step = s.countIn ? 0 : std::clamp(s.step, 0, b.stepCount() - 1);
    const int chord = b.chordAtStep.value(step, -1);
    m_current->setChord(chord >= 0 ? &m_timeline->chords[chord] : nullptr);
    m_current->setGlow(s.stringGlow);

    // Find the next different chord and how many beats away it is.
    int nextChord = -1;
    double beats = 0;
    for (int bi = bar, st = step + 1, guard = 0; bi < bars.size() && guard < 4; ++guard) {
        const Timeline::Bar &nb = bars[bi];
        for (; st < nb.stepCount(); ++st) {
            if (nb.chordAtStep[st] != chord) {
                nextChord = nb.chordAtStep[st];
                beats += double(st - (bi == bar ? step : 0)) / nb.subdivision;
                break;
            }
        }
        if (st < nb.stepCount())
            break;
        beats += double(nb.stepCount() - (bi == bar ? step : 0)) / nb.subdivision;
        ++bi;
        st = 0;
    }
    if (nextChord >= 0 || beats > 0) {
        m_next->setChord(nextChord >= 0 ? &m_timeline->chords[nextChord] : nullptr);
        const int whole = int(std::ceil(beats - s.stepFraction / b.subdivision - 1e-6));
        m_next->setCaption(tr("Next - in %n beat(s)", nullptr, std::max(1, whole)));
    } else {
        m_next->setChord(nullptr);
        m_next->setCaption(tr("Next"));
    }

    m_pattern->setPosition(bar, step, s.stepFraction, s.playing && !s.countIn);
}

void MainWindow::openSong()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open song"), songsDir(), tr("Song files (*.xml)"));
    if (!path.isEmpty())
        loadSongFile(path);
}

void MainWindow::reloadSong()
{
    if (m_song)
        loadSongFile(m_song->filePath, true);
    refreshSongList();
}

void MainWindow::exportWav()
{
    if (!m_timeline)
        return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Export WAV"),
                                                      QDir::homePath() + QLatin1Char('/') + m_song->title + QStringLiteral(".wav"),
                                                      tr("WAV audio (*.wav)"));
    if (path.isEmpty())
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = exportSongToWav(m_timeline, path, m_tempo->value() / 100.0, &err);
    QApplication::restoreOverrideCursor();
    if (ok)
        m_status->setText(tr("Exported %1").arg(path));
    else
        QMessageBox::warning(this, tr("Export failed"), err);
}

void MainWindow::showFormatHelp()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Song XML format"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr(
        "<h3>Song file cheat sheet</h3>"
        "<pre>&lt;song title=\"..\" artist=\"..\" bpm=\"90\" beatsPerBar=\"4\" capo=\"0\"&gt;\n"
        "  &lt;chords&gt;  &lt;!-- optional, overrides built-ins --&gt;\n"
        "    &lt;chord name=\"Cadd9\" frets=\"x32033\" fingers=\"021034\"/&gt;\n"
        "  &lt;/chords&gt;\n"
        "  &lt;patterns&gt;\n"
        "    &lt;pattern name=\"folk\" subdivision=\"2\"&gt;D - D U - U D U&lt;/pattern&gt;\n"
        "  &lt;/patterns&gt;\n"
        "  &lt;sections&gt;\n"
        "    &lt;section name=\"Verse\" pattern=\"folk\" bpm=\"90\"&gt;\n"
        "      &lt;bars repeat=\"2\"&gt;G | D | Em C | C&lt;/bars&gt;\n"
        "    &lt;/section&gt;\n"
        "  &lt;/sections&gt;\n"
        "  &lt;arrangement&gt;&lt;play section=\"Verse\" repeat=\"2\"/&gt;&lt;/arrangement&gt;\n"
        "&lt;/song&gt;</pre>"
        "<b>Pattern tokens</b> (one per step, <i>subdivision</i> steps per beat):<br>"
        "<tt>D</tt>/<tt>U</tt> full down/up strum, <tt>d</tt>/<tt>u</tt> light strum, "
        "<tt>X</tt> muted chuck, <tt>-</tt> rest, <tt>B</tt> bass note, <tt>A</tt> alternate bass, "
        "<tt>1</tt>-<tt>6</tt> pick a string (1 = high e), <tt>B+1</tt> pinch, <tt>&gt;D</tt> accent.<br><br>"
        "<b>Bars:</b> chords separated by <tt>|</tt>; several chords in one bar share it evenly, "
        "or give lengths in beats: <tt>C:3 G:1</tt>. <tt>%</tt> repeats the previous chord, "
        "<tt>N.C.</tt> is silence. See README.md for details."));
    box.exec();
}
