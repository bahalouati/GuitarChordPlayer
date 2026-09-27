#include "MainWindow.h"

#include "ChordDiagramWidget.h"
#include "Arranger.h"
#include "AudioTrack.h"
#include "ChordDetector.h"
#include "ChordFinderDialog.h"
#include "NewSongDialog.h"
#include "LlmPrompt.h"
#include "LyricsDialog.h"
#include "LyricsWidget.h"
#include "PatternWidget.h"
#include "SongEditor.h"
#include "Updater.h"
#include "WavWriter.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QProcess>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDateTime>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QStandardPaths>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QInputDialog>
#include <QProgressDialog>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QThread>
#include <atomic>
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

    m_updater = new Updater(this);
    buildUi();
    buildMenus();

    QDir().mkpath(userSongsDir());
    m_editor->setUserSongsDir(userSongsDir());
    setAcceptDrops(true);

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        // Editors often save by replacing the file; reload after a short delay and watch it again.
        QTimer::singleShot(200, this, [this, path] {
            m_editor->fileChangedOnDisk(path);
            if (m_song && m_song->filePath == path)
                loadSongFile(path, true);
            watchPaths();
        });
    });
    // New, renamed or deleted files in the song folders show up in the list by themselves.
    m_dirRefresh = new QTimer(this);
    m_dirRefresh->setSingleShot(true);
    m_dirRefresh->setInterval(300);
    connect(m_dirRefresh, &QTimer::timeout, this, &MainWindow::refreshSongList);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, m_dirRefresh, qOverload<>(&QTimer::start));
    watchPaths();

    QSettings s;
    m_tempo->setValue(s.value(QStringLiteral("tempo"), 100).toInt());
    {
        const QSignalBlocker block(m_chordsBox);
        const int idx = m_chordsBox->findData(s.value(QStringLiteral("chordLevel"), 0).toInt());
        m_chordsBox->setCurrentIndex(std::max(0, idx));
    }
    m_volume->setValue(s.value(QStringLiteral("volume"), 80).toInt());
    m_recordingVolume->setValue(s.value(QStringLiteral("recordingVolume"), 80).toInt());
    m_metronome->setChecked(s.value(QStringLiteral("metronome"), false).toBool());
    m_countIn->setChecked(s.value(QStringLiteral("countIn"), true).toBool());
    restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray());
    if (!restoreState(s.value(QStringLiteral("windowState")).toByteArray()))
        resizeDocks({m_editorDock}, {460}, Qt::Horizontal);

    refreshSongList();
    const QString last = s.value(QStringLiteral("lastSong")).toString();
    if (!last.isEmpty() && QFileInfo::exists(last)) {
        loadSongFile(last);
    } else {
        for (int i = 0; i < m_songList->count(); ++i) {
            const QString path = m_songList->item(i)->data(Qt::UserRole).toString();
            if (path.endsWith(QLatin1String(".xml"))) {
                loadSongFile(path);
                break;
            }
        }
    }

    // Look for a newer build now and then (at most every 12 hours), without getting in the way.
    m_updateBtn = new QPushButton;
    m_updateBtn->setStyleSheet(QStringLiteral("color: white; background: #2e8b57; padding: 2px 10px;"));
    m_updateBtn->hide();
    statusBar()->addPermanentWidget(m_updateBtn);
    connect(m_updateBtn, &QPushButton::clicked, m_updater, [this] { m_updater->check(true); });
    connect(m_updater, &Updater::updateAvailable, this, [this](int build, const QString &) {
        m_updateBtn->setText(tr("Update available (build %1) - install").arg(build));
        m_updateBtn->show();
    });
    const QDateTime lastCheck = s.value(QStringLiteral("lastUpdateCheck")).toDateTime();
    if (s.value(QStringLiteral("autoUpdateCheck"), true).toBool()
        && (!lastCheck.isValid() || lastCheck.secsTo(QDateTime::currentDateTime()) > 12 * 3600)) {
        s.setValue(QStringLiteral("lastUpdateCheck"), QDateTime::currentDateTime());
        QTimer::singleShot(3000, m_updater, [this] { m_updater->check(false); });
    }

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
    s.setValue(QStringLiteral("recordingVolume"), m_recordingVolume->value());
    s.setValue(QStringLiteral("metronome"), m_metronome->isChecked());
    s.setValue(QStringLiteral("countIn"), m_countIn->isChecked());
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    s.setValue(QStringLiteral("windowState"), saveState());
    if (m_song)
        s.setValue(QStringLiteral("lastSong"), m_song->filePath);
}

void MainWindow::buildUi()
{
    auto *splitter = new QSplitter(this);

    // Left: song library and arrangement
    auto *left = new QWidget;
    auto *ll = new QVBoxLayout(left);
    auto *newBtn = new QPushButton(tr("+ New song"));
    newBtn->setFocusPolicy(Qt::NoFocus);
    connect(newBtn, &QPushButton::clicked, this, &MainWindow::newSong);
    auto *editBtn = new QPushButton(tr("Edit song"));
    editBtn->setFocusPolicy(Qt::NoFocus);
    connect(editBtn, &QPushButton::clicked, this, &MainWindow::toggleEditor);
    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(newBtn);
    btnRow->addWidget(editBtn);
    ll->addLayout(btnRow);
    ll->addWidget(new QLabel(tr("<b>Songs</b>")));
    m_songList = new QListWidget;
    ll->addWidget(m_songList, 2);
    ll->addWidget(new QLabel(tr("<b>Arrangement</b> (click to jump)")));
    m_sectionList = new QListWidget;
    ll->addWidget(m_sectionList, 3);
    splitter->addWidget(left);

    connect(m_songList, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        const QString path = it->data(Qt::UserRole).toString();
        if (path == QLatin1String("new"))
            newSong();
        else if (!path.isEmpty() && (!m_song || m_song->filePath != path))
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
    m_banner = new QLabel;
    m_banner->setWordWrap(true);
    m_banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_banner->setStyleSheet(QStringLiteral("background: #d03030; color: white; padding: 8px; border-radius: 4px;"));
    m_banner->hide();
    rl->addWidget(m_banner);
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

    m_lyrics = new LyricsWidget;
    m_lyrics->hide();
    connect(m_lyrics, &LyricsWidget::lineClicked, this, [this](int line) {
        if (m_song && m_editor->filePath() != m_song->filePath)
            m_editor->openFile(m_song->filePath);
        m_editorDock->show();
        m_editorDock->raise();
        m_editor->goToLine(line);
    });
    rl->addWidget(m_lyrics, 2);

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

    // Capo and chord simplification: change what you play, not what you hear.
    tr1->addSpacing(12);
    tr1->addWidget(new QLabel(tr("Capo")));
    m_capoBox = new QComboBox;
    m_capoBox->setFocusPolicy(Qt::NoFocus);
    m_capoBox->setToolTip(tr("Play the song with a different capo. It sounds the same; the chord shapes change.\n"
                             "Auto picks the capo with the easiest shapes."));
    m_capoBox->addItem(tr("As in song"), Arranger::kCapoAsSong);
    m_capoBox->addItem(tr("Auto (easiest)"), Arranger::kCapoAuto);
    m_capoBox->addItem(tr("No capo"), 0);
    for (int c = 1; c <= 9; ++c)
        m_capoBox->addItem(tr("Fret %1").arg(c), c);
    tr1->addWidget(m_capoBox);
    tr1->addWidget(new QLabel(tr("Chords")));
    m_chordsBox = new QComboBox;
    m_chordsBox->setFocusPolicy(Qt::NoFocus);
    m_chordsBox->addItem(tr("As written"), int(ChordName::Level::AsWritten));
    m_chordsBox->addItem(tr("Simplify"), int(ChordName::Level::Simplify));
    m_chordsBox->addItem(tr("Simplify+"), int(ChordName::Level::SimplifyPlus));
    m_chordsBox->setItemData(0, tr("Play the chords exactly as the song has them"), Qt::ToolTipRole);
    m_chordsBox->setItemData(1, tr("Plain major and minor chords: Cmaj7 → C, Am7 → Am, Dsus4 → D, G/B → G"), Qt::ToolTipRole);
    m_chordsBox->setItemData(2, tr("Simplify, pick the easiest capo, and replace barre chords with easy shapes "
                                   "(F → Fmaj7 shape, Bm → small Bm, B → B7...)"), Qt::ToolTipRole);
    tr1->addWidget(m_chordsBox);
    connect(m_capoBox, &QComboBox::currentIndexChanged, this, [this] {
        if (m_song)
            QSettings().setValue(QStringLiteral("capo/") + m_song->filePath, m_capoBox->currentData());
        rearrange();
    });
    connect(m_chordsBox, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(QStringLiteral("chordLevel"), m_chordsBox->currentData());
        rearrange();
    });

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
    // Mixer: guitar and the song's recording (if it has one).
    m_guitarOn = new QCheckBox(tr("Guitar"));
    m_guitarOn->setChecked(true);
    m_guitarOn->setFocusPolicy(Qt::NoFocus);
    connect(m_guitarOn, &QCheckBox::toggled, m_engine, &AudioEngine::setGuitarEnabled);
    tr2->addWidget(m_guitarOn);
    m_volume = new QSlider(Qt::Horizontal);
    m_volume->setRange(0, 100);
    m_volume->setFocusPolicy(Qt::NoFocus);
    m_volume->setFixedWidth(110);
    m_volume->setToolTip(tr("Guitar volume"));
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) { m_engine->setVolume(v / 100.f); });
    tr2->addWidget(m_volume);

    m_recordingControls = new QWidget;
    auto *rec = new QHBoxLayout(m_recordingControls);
    rec->setContentsMargins(12, 0, 0, 0);
    m_recordingOn = new QCheckBox(tr("Recording"));
    m_recordingOn->setChecked(true);
    m_recordingOn->setFocusPolicy(Qt::NoFocus);
    connect(m_recordingOn, &QCheckBox::toggled, m_engine, &AudioEngine::setAudioEnabled);
    rec->addWidget(m_recordingOn);
    m_recordingVolume = new QSlider(Qt::Horizontal);
    m_recordingVolume->setRange(0, 100);
    m_recordingVolume->setValue(80);
    m_recordingVolume->setFocusPolicy(Qt::NoFocus);
    m_recordingVolume->setFixedWidth(110);
    m_recordingVolume->setToolTip(tr("Recording volume"));
    connect(m_recordingVolume, &QSlider::valueChanged, this, [this](int v) { m_engine->setAudioVolume(v / 100.f); });
    rec->addWidget(m_recordingVolume);
    rec->addWidget(new QLabel(tr("Sync")));
    m_syncMs = new QSpinBox;
    m_syncMs->setRange(-60000, 600000);
    m_syncMs->setSingleStep(10);
    m_syncMs->setSuffix(tr(" ms"));
    m_syncMs->setToolTip(tr("Where bar 1 starts in the recording. Nudge it until the guitar and the recording line up; "
                            "it is saved in the song file."));
    rec->addWidget(m_syncMs);
    tr2->addWidget(m_recordingControls);
    m_recordingControls->setEnabled(false);

    m_offsetSave = new QTimer(this);
    m_offsetSave->setSingleShot(true);
    m_offsetSave->setInterval(800);
    connect(m_offsetSave, &QTimer::timeout, this, &MainWindow::saveAudioOffset);
    connect(m_syncMs, &QSpinBox::valueChanged, this, [this](int ms) {
        if (!m_song || m_song->audioFile.isEmpty() || std::abs(ms - int(std::lround(m_song->audioOffset * 1000))) < 1)
            return;
        m_engine->setAudioOffset(ms / 1000.0);
        m_song->audioOffset = ms / 1000.0;
        m_offsetSave->start();
    });
    rl->addLayout(tr1);
    rl->addLayout(tr2);

    splitter->addWidget(right);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);
    splitter->setSizes({220, 900});
    setCentralWidget(splitter);

    m_editor = new SongEditor;
    m_editorDock = new QDockWidget(tr("Song editor"), this);
    m_editorDock->setObjectName(QStringLiteral("editorDock"));
    m_editorDock->setWidget(m_editor);
    addDockWidget(Qt::RightDockWidgetArea, m_editorDock);
    m_editorDock->hide();
    connect(m_editor, &SongEditor::liveEdit, this, &MainWindow::applyLiveEdit);
    connect(m_editor, &SongEditor::saved, this, [this](const QString &path) {
        // While switching songs the editor may save the previous one; don't load it back.
        if (!m_loading)
            loadSongFile(path, m_song && m_song->filePath == path);
        m_dirRefresh->start();
    });

    m_status = new QLabel;
    statusBar()->addWidget(m_status, 1);
    resize(1180, 760);
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New song..."), QKeySequence::New, this, &MainWindow::newSong);
    file->addAction(tr("New song from &MP3 (detect chords)..."), QKeySequence(Qt::CTRL | Qt::Key_I), this,
                    &MainWindow::newSongFromAudio);
    file->addAction(tr("Attach &recording to this song..."), this, &MainWindow::attachRecording);
    file->addAction(tr("&Edit song"), QKeySequence(Qt::CTRL | Qt::Key_E), this, &MainWindow::toggleEditor);
    file->addAction(tr("New song from LLM &answer (clipboard)"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), this, [this] {
        const QString xml = extractSongXml(QApplication::clipboard()->text());
        if (xml.isEmpty()) {
            QMessageBox::information(this, tr("New song from LLM answer"),
                                     tr("The clipboard has no song in it. Copy the LLM's whole answer and try again.\n\n"
                                        "To get one: Help > Copy LLM prompt, paste it into an LLM and fill in the song."));
            return;
        }
        QString err;
        auto song = loadSongFromData(xml.toUtf8(), QString(), &err);
        saveNewSong(song && !song->title.isEmpty() ? song->title : tr("LLM Song"), xml);
    });
    file->addAction(tr("&Open song..."), QKeySequence::Open, this, &MainWindow::openSong);
    file->addAction(tr("&Reload song"), QKeySequence(Qt::Key_F5), this, &MainWindow::reloadSong);
    file->addAction(tr("Refresh song &list"), this, &MainWindow::refreshSongList);
    file->addAction(tr("Open &My Songs folder"), this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(userSongsDir()));
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

    QMenu *tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(tr("&Chord finder..."), QKeySequence(Qt::CTRL | Qt::Key_K), this, &MainWindow::showChordFinder);
    tools->addAction(tr("Add &lyrics to this song..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L), this,
                     &MainWindow::addLyrics);

    QMenu *settings = menuBar()->addMenu(tr("&Settings"));
    buildLanguageMenu(settings->addMenu(tr("&Language")));
    QAction *detailed = settings->addAction(tr("Recognise 7th, sus, dim and aug chords in recordings"));
    detailed->setCheckable(true);
    detailed->setChecked(QSettings().value(QStringLiteral("detailedDetection"), true).toBool());
    detailed->setToolTip(tr("Off: only major and minor chords are detected"));
    connect(detailed, &QAction::toggled, this, [](bool on) { QSettings().setValue(QStringLiteral("detailedDetection"), on); });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Song XML format..."), this, &MainWindow::showFormatHelp);
    help->addAction(tr("Copy LLM prompt"), this, [this] {
        QApplication::clipboard()->setText(llmPrompt());
        m_status->setText(tr("LLM prompt copied - paste it into ChatGPT, Claude, Gemini..., then use "
                             "File > New song from LLM answer"));
    });
    help->addSeparator();
    help->addAction(tr("Check for &updates..."), m_updater, [this] { m_updater->check(true); });
    QAction *autoCheck = help->addAction(tr("Check for updates automatically"));
    autoCheck->setCheckable(true);
    autoCheck->setChecked(QSettings().value(QStringLiteral("autoUpdateCheck"), true).toBool());
    connect(autoCheck, &QAction::toggled, this, [](bool on) { QSettings().setValue(QStringLiteral("autoUpdateCheck"), on); });
    help->addSeparator();
    help->addAction(tr("About"), this, [this] {
        const QString build = QStringLiteral("%1 %2").arg(QApplication::applicationVersion(),
            Updater::currentBuild() > 0 ? tr("(build %1)").arg(Updater::currentBuild()) : tr("(development build)"));
        QMessageBox::about(this, tr("About Guitar Chord Player"),
                           tr("<b>Guitar Chord Player</b> - %1<br>Plays chord charts from XML files with a "
                              "synthesized (Karplus-Strong) guitar so you can see and hear how songs "
                              "are strummed and picked.<br><br>Updates come from "
                              "<a href=\"https://github.com/%2/releases\">github.com/%2</a>.")
                           .arg(build, Updater::repository()));
    });
}

QString MainWindow::userSongsDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/Guitar Chord Player/My Songs");
}

QString MainWindow::examplesDir() const
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
    const QString current = m_song ? m_song->filePath : QString();
    m_songList->clear();

    auto addHeader = [this](const QString &text) {
        auto *it = new QListWidgetItem(text);
        QFont f = it->font();
        f.setBold(true);
        it->setFont(f);
        it->setFlags(Qt::ItemIsEnabled); // a heading: not selectable
        m_songList->addItem(it);
    };
    auto addFolder = [this, &current](const QString &dirPath) {
        QDir dir(dirPath);
        const QStringList files = dir.entryList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name);
        for (const QString &f : files) {
            const QString path = dir.absoluteFilePath(f);
            QString err;
            auto song = loadSong(path, &err);
            std::shared_ptr<Timeline> tl = song ? buildTimeline(song, &err) : nullptr;
            auto *it = new QListWidgetItem(QStringLiteral("   ") + (song ? song->title : QFileInfo(f).completeBaseName()));
            it->setData(Qt::UserRole, path);
            if (!tl) {
                it->setForeground(QColor(208, 48, 48));
                it->setText(it->text() + tr("  (needs fixing)"));
                it->setToolTip(err);
            } else if (!song->artist.isEmpty()) {
                it->setToolTip(song->artist);
            }
            m_songList->addItem(it);
            if (path == current)
                m_songList->setCurrentItem(it);
        }
    };

    // Each sub-folder (e.g. songs/spacetoon) gets its own heading.
    auto addSubFolders = [&](const QString &dirPath) {
        const QDir dir(dirPath);
        for (const QString &sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            QString name = sub;
            name.replace(QLatin1Char('_'), QLatin1Char(' '));
            if (!name.isEmpty())
                name[0] = name[0].toUpper();
            addHeader(name);
            addFolder(dir.absoluteFilePath(sub));
        }
    };

    addHeader(tr("My Songs"));
    auto *add = new QListWidgetItem(tr("   + Create a new song..."));
    add->setData(Qt::UserRole, QStringLiteral("new"));
    add->setForeground(QColor(40, 120, 220));
    m_songList->addItem(add);
    addFolder(userSongsDir());
    addSubFolders(userSongsDir());
    const QString examples = examplesDir();
    if (QDir(examples).absolutePath() != QDir(userSongsDir()).absolutePath()) {
        addHeader(tr("Examples"));
        addFolder(examples);
        addSubFolders(examples);
    }
}

void MainWindow::watchPaths()
{
    QStringList wanted = {userSongsDir(), examplesDir()};
    if (m_song && !m_song->filePath.isEmpty())
        wanted << m_song->filePath;
    if (!m_editor->filePath().isEmpty())
        wanted << m_editor->filePath();
    for (const QString &p : wanted)
        if (QFileInfo::exists(p) && !m_watcher->files().contains(p) && !m_watcher->directories().contains(p))
            m_watcher->addPath(p);
}

void MainWindow::showError(const QString &message)
{
    m_banner->setText(message);
    m_banner->setVisible(!message.isEmpty());
}

QString MainWindow::uniqueSongPath(const QString &title) const
{
    QString base = title;
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    base = base.trimmed();
    if (base.isEmpty())
        base = QStringLiteral("My Song");
    QString path = userSongsDir() + QLatin1Char('/') + base + QStringLiteral(".xml");
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = userSongsDir() + QLatin1Char('/') + base + QStringLiteral(" (%1).xml").arg(n);
    return path;
}

void MainWindow::newSong()
{
    NewSongDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    saveNewSong(dlg.title(), dlg.xml());
}

void MainWindow::saveNewSong(const QString &title, const QString &xml)
{
    const QString path = uniqueSongPath(title);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(xml.toUtf8()) < 0) {
        QMessageBox::warning(this, tr("New song"), tr("Could not save %1").arg(path));
        return;
    }
    f.close();
    refreshSongList();
    if (loadSongFile(path)) {
        m_editorDock->show();
        m_engine->play();
        setPlayButton(true);
        m_status->setText(tr("Saved to %1 - use Edit song to change strums, chords or tempo").arg(path));
    }
}

void MainWindow::toggleEditor()
{
    if (m_editorDock->isVisible()) {
        m_editorDock->hide();
        return;
    }
    if (m_song && m_editor->filePath().isEmpty())
        m_editor->openFile(m_song->filePath);
    m_editorDock->show();
    m_editorDock->raise();
}

void MainWindow::showChordFinder()
{
    auto *dlg = new ChordFinderDialog(m_engine, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::importSong(const QString &path)
{
    QString target = path;
    if (QFileInfo(path).absolutePath() != QDir(userSongsDir()).absolutePath()) {
        target = userSongsDir() + QLatin1Char('/') + QFileInfo(path).fileName();
        for (int n = 2; QFileInfo::exists(target); ++n)
            target = userSongsDir() + QLatin1Char('/') + QFileInfo(path).completeBaseName()
                     + QStringLiteral(" (%1).xml").arg(n);
        if (!QFile::copy(path, target)) {
            QMessageBox::warning(this, tr("Add song"), tr("Could not copy %1 to %2").arg(path, target));
            return;
        }
    }
    refreshSongList();
    loadSongFile(target);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    for (const QUrl &u : e->mimeData()->urls()) {
        if (u.isLocalFile() && u.toLocalFile().endsWith(QLatin1String(".xml"), Qt::CaseInsensitive)) {
            e->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent *e)
{
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile() && u.toLocalFile().endsWith(QLatin1String(".xml"), Qt::CaseInsensitive))
            importSong(u.toLocalFile());
    m_status->setText(tr("Added to My Songs (%1)").arg(userSongsDir()));
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_editor->isModified()) {
        const auto r = QMessageBox::question(this, tr("Unsaved changes"),
                                             tr("Save your changes to %1?").arg(QFileInfo(m_editor->filePath()).fileName()),
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel || (r == QMessageBox::Save && !m_editor->save())) {
            e->ignore();
            return;
        }
    }
    e->accept();
}

bool MainWindow::loadSongFile(const QString &path, bool keepPosition)
{
    if (m_loading)
        return false;
    m_loading = true;
    const bool ok = loadSongFileImpl(path, keepPosition);
    m_loading = false;
    return ok;
}

bool MainWindow::loadSongFileImpl(const QString &path, bool keepPosition)
{
    QString err;
    auto song = loadSong(path, &err);
    std::shared_ptr<Timeline> tl;
    if (song)
        tl = buildTimeline(song, &err);
    if (!tl) {
        // Show what is wrong and open the file in the editor so it can be fixed right away.
        // If a song was already playing, it keeps playing the last good version.
        showError(tr("<b>%1 has a problem:</b> %2<br>Fix it in the song editor and press Ctrl+S.")
                  .arg(QFileInfo(path).fileName().toHtmlEscaped(), err.toHtmlEscaped()));
        if (QFileInfo::exists(path)) {
            m_editor->openFile(path);
            m_editorDock->show();
        }
        watchPaths();
        return false;
    }
    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    if (m_editor->filePath() != path)
        m_editor->openFile(path);
    applySong(song, tl, keepPosition);
    watchPaths();
    m_status->setText(keepPosition ? tr("Reloaded %1").arg(QFileInfo(path).fileName())
                                   : tr("Loaded %1  ·  %n bar(s)", nullptr, tl->bars.size()).arg(QFileInfo(path).fileName()));
    return true;
}

void MainWindow::applyLiveEdit(const QString &path, const QByteArray &xml)
{
    if (m_loading)
        return;
    QString err;
    auto song = loadSongFromData(xml, path, &err);
    auto tl = song ? buildTimeline(song, &err) : nullptr;
    if (!tl)
        return; // the editor already shows the problem
    applySong(song, tl, m_song && m_song->filePath == path);
    m_status->setText(tr("Playing your unsaved changes - Ctrl+S saves them"));
}

void MainWindow::applySong(std::shared_ptr<Song> song, std::shared_ptr<Timeline> tl, bool keepPosition)
{
    const QString path = song->filePath;
    showError(QString());
    const bool keep = keepPosition && m_timeline;
    m_song = song;
    m_timeline = tl;
    {
        // The capo chosen for this song earlier (or "as in song").
        const QSignalBlocker block(m_capoBox);
        const int capo = QSettings().value(QStringLiteral("capo/") + song->filePath, Arranger::kCapoAsSong).toInt();
        const int idx = m_capoBox->findData(capo);
        m_capoBox->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    Arranger::Settings as;
    as.capo = m_capoBox->currentData().toInt();
    as.level = ChordName::Level(m_chordsBox->currentData().toInt());
    m_view = Arranger::arrange(tl, as);
    if (keep)
        m_engine->replaceTimeline(m_view);
    else
        m_engine->setTimeline(m_view);
    m_pattern->setTimeline(m_view);
    applyRecording(!keep);
    m_lyrics->setTimeline(m_view);
    m_lyrics->setVisible(!tl->lyricLines.isEmpty());
    applyTempo();

    m_title->setText(song->title);
    updateInfo();
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

    if (keep) {
        m_viewBar = std::min(m_viewBar, int(tl->bars.size()) - 1);
    } else {
        m_viewBar = 0;
        setPlayButton(false);
    }
    applyLoop();
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
    m_current->setChord(chord >= 0 ? &m_view->chords[chord] : nullptr);
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
        m_next->setChord(nextChord >= 0 ? &m_view->chords[nextChord] : nullptr);
        const int whole = int(std::ceil(beats - s.stepFraction / b.subdivision - 1e-6));
        m_next->setCaption(tr("Next - in %n beat(s)", nullptr, std::max(1, whole)));
    } else {
        m_next->setChord(nullptr);
        m_next->setCaption(tr("Next"));
    }

    m_pattern->setPosition(bar, step, s.stepFraction, s.playing && !s.countIn);
    m_lyrics->setPosition(bar, (step + s.stepFraction) / std::max(1, b.stepCount()), s.playing && !s.countIn);
}

void MainWindow::openSong()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open song"), userSongsDir(), tr("Song files (*.xml)"));
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
    const bool ok = exportSongToWav(m_view, path, m_tempo->value() / 100.0, &err, m_clip, m_song->audioOffset);
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
        "<b>Recording:</b> <tt>&lt;audio file=\"song.mp3\" offset=\"0.35\"/&gt;</tt> plays along "
        "(offset = where bar 1 starts, in seconds).<br><br>"
        "<b>Built-in patterns</b> (no &lt;pattern&gt; needed): folk, pop, rock, drive, ballad, whole, half, "
        "quarters, reggae, country, sixteenths, arpeggio, arpeggio-slow, travis, waltz, waltz-pick, six-eight, "
        "six-eight-pick. A section without <tt>pattern</tt> uses folk.<br><br>"
        "<b>Lyrics:</b> <tt>&lt;line chords=\"G | D\"&gt;words in bar one | bar two&lt;/line&gt;</tt> instead of "
        "&lt;bars&gt; - one lyric piece per bar.<br><br>"
        "<b>Bars:</b> chords separated by <tt>|</tt>; several chords in one bar share it evenly, "
        "or give lengths in beats: <tt>C:3 G:1</tt>. <tt>%</tt> repeats the previous chord, "
        "<tt>N.C.</tt> is silence. Any chord name works (Tools &gt; Chord finder). See README.md for details.<br><br>"
        "<b>Easiest:</b> File &gt; New song lets you type a plain chord sheet instead."));
    box.exec();
}

// ---------------------------------------------------------------- Recordings

std::shared_ptr<const AudioClip> MainWindow::loadRecording(const QString &path)
{
    QProgressDialog progress(tr("Loading %1...").arg(QFileInfo(path).fileName()), QString(), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);
    QString err;
    auto clip = decodeAudioFile(path, m_engine->sampleRate(), &err,
                                [&](double f) { progress.setValue(int(f * 100)); });
    progress.close();
    if (!clip)
        showError(tr("<b>Can't play the recording:</b> %1").arg(err.toHtmlEscaped()));
    return clip;
}

void MainWindow::applyRecording(bool songChanged)
{
    const QString file = m_song ? m_song->audioFile : QString();
    const QString key = file.isEmpty() ? QString()
                                       : file + QLatin1Char('@') + QString::number(QFileInfo(file).lastModified().toMSecsSinceEpoch());
    if (key != m_clipKey) {
        m_clipKey = key;
        m_clip = key.isEmpty() ? nullptr : loadRecording(file);
        m_engine->setAudio(m_clip, m_song ? m_song->audioOffset : 0.0);
    } else if (m_clip) {
        m_engine->setAudioOffset(m_song->audioOffset);
    }
    Q_UNUSED(songChanged);
    m_recordingControls->setEnabled(bool(m_clip));
    const QSignalBlocker block(m_syncMs);
    m_syncMs->setValue(m_song ? int(std::lround(m_song->audioOffset * 1000)) : 0);
}

// Replaces (or adds) the <audio .../> element of a song file's text.
static QString withAudioElement(QString xml, const QString &file, double offset)
{
    const QString element = QStringLiteral("<audio file=\"%1\" offset=\"%2\"/>")
            .arg(file.toHtmlEscaped(), QString::number(offset, 'f', 3));
    static const QRegularExpression existing(QStringLiteral("<audio\\b[^>]*/>"));
    if (xml.contains(existing))
        return xml.replace(existing, element);
    static const QRegularExpression songTag(QStringLiteral("<song\\b[^>]*>"));
    const auto m = songTag.match(xml);
    if (!m.hasMatch())
        return xml;
    return xml.insert(int(m.capturedEnd()), QStringLiteral("\n  ") + element);
}

void MainWindow::saveAudioOffset()
{
    if (!m_song || m_song->audioFile.isEmpty())
        return;
    QFile f(m_song->filePath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    QString xml = QString::fromUtf8(f.readAll());
    f.close();
    static const QRegularExpression fileAttr(QStringLiteral("<audio\\b[^>]*file=\"([^\"]*)\""));
    const auto m = fileAttr.match(xml);
    if (!m.hasMatch())
        return;
    const QString updated = withAudioElement(xml, m.captured(1), m_song->audioOffset);
    if (updated == xml || !f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(updated.toUtf8());
    f.close();
    m_status->setText(tr("Sync saved (%1 ms)").arg(int(std::lround(m_song->audioOffset * 1000))));
}

bool MainWindow::analyseRecording(const AudioClip &clip, DetectedSong *out)
{
    QProgressDialog progress(tr("Listening to the recording: finding the beat and the chords..."), tr("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    std::atomic<bool> cancel{false};
    std::atomic<int> percent{0};
    const bool detailed = QSettings().value(QStringLiteral("detailedDetection"), true).toBool();
    bool ok = false;
    QString err;
    QThread *worker = QThread::create([&] {
        ok = detectChords(clip, out, &err, [&](double f) { percent = int(f * 100); }, &cancel, detailed);
    });
    QEventLoop loop;
    connect(worker, &QThread::finished, &loop, &QEventLoop::quit);
    QTimer poll;
    connect(&poll, &QTimer::timeout, this, [&] {
        progress.setValue(percent.load());
        if (progress.wasCanceled())
            cancel = true;
    });
    poll.start(50);
    worker->start();
    loop.exec();
    worker->wait();
    delete worker;
    progress.close();
    if (!ok && !cancel)
        QMessageBox::warning(this, tr("Chord detection"), err.isEmpty() ? tr("Could not analyse the recording.") : err);
    return ok && !cancel;
}

void MainWindow::newSongFromAudio()
{
    const QString src = QFileDialog::getOpenFileName(this, tr("New song from a recording"), QDir::homePath(),
                                                     tr("Audio (*.mp3 *.m4a *.aac *.ogg *.opus *.flac *.wav *.wma)"));
    if (src.isEmpty())
        return;
    bool okTitle = false;
    const QString title = QInputDialog::getText(this, tr("New song from a recording"), tr("Song title:"),
                                                QLineEdit::Normal, QFileInfo(src).completeBaseName(), &okTitle).trimmed();
    if (!okTitle || title.isEmpty())
        return;

    auto clip = loadRecording(src);
    if (!clip)
        return;
    DetectedSong detected;
    if (!analyseRecording(*clip, &detected))
        return;

    // Keep the recording next to the song so the song folder is self-contained.
    const QString xmlPath = uniqueSongPath(title);
    const QString audioName = QFileInfo(xmlPath).completeBaseName() + QLatin1Char('.') + QFileInfo(src).suffix().toLower();
    const QString audioPath = QFileInfo(xmlPath).absoluteDir().absoluteFilePath(audioName);
    if (QFileInfo(src).absoluteFilePath() != audioPath && !QFile::copy(src, audioPath)) {
        QMessageBox::warning(this, tr("New song"), tr("Could not copy the recording to %1").arg(audioPath));
        return;
    }
    QFile f(xmlPath);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("New song"), tr("Could not save %1").arg(xmlPath));
        return;
    }
    f.write(detectedSongToXml(detected, title, QString(), audioName).toUtf8());
    f.close();
    refreshSongList();
    if (loadSongFile(xmlPath)) {
        m_status->setText(tr("%1 bars at %2 BPM%3 - the chords were detected automatically, fix any wrong ones "
                             "in the editor while it plays")
                          .arg(detected.bars.size()).arg(qRound(detected.bpm))
                          .arg(detected.capo ? tr(", capo %1").arg(detected.capo) : QString()));
        m_editorDock->show();
    }
}

void MainWindow::attachRecording()
{
    if (!m_song) {
        QMessageBox::information(this, tr("Attach recording"), tr("Open a song first."));
        return;
    }
    QString songPath = m_song->filePath;
    const bool inMySongs = QFileInfo(songPath).absolutePath().startsWith(QDir(userSongsDir()).absolutePath());
    if (!inMySongs) {
        if (QMessageBox::question(this, tr("Attach recording"),
                                  tr("This is an example song. Make a copy in My Songs and attach the recording to the copy?"))
            != QMessageBox::Yes)
            return;
        const QString copy = uniqueSongPath(m_song->title);
        if (!QFile::copy(songPath, copy)) {
            QMessageBox::warning(this, tr("Attach recording"), tr("Could not copy the song to %1").arg(copy));
            return;
        }
        QFile::setPermissions(copy, QFile::permissions(copy) | QFileDevice::WriteOwner);
        songPath = copy;
    }
    const QString src = QFileDialog::getOpenFileName(this, tr("Attach a recording"), QDir::homePath(),
                                                     tr("Audio (*.mp3 *.m4a *.aac *.ogg *.opus *.flac *.wav *.wma)"));
    if (src.isEmpty())
        return;
    auto clip = loadRecording(src);
    if (!clip)
        return;
    DetectedSong detected;
    if (!analyseRecording(*clip, &detected))
        return;

    const QDir dir = QFileInfo(songPath).absoluteDir();
    QString audioName = QFileInfo(songPath).completeBaseName() + QLatin1Char('.') + QFileInfo(src).suffix().toLower();
    if (QFileInfo(src).absoluteDir() == dir)
        audioName = QFileInfo(src).fileName();
    else if (!QFile::exists(dir.absoluteFilePath(audioName)) && !QFile::copy(src, dir.absoluteFilePath(audioName))) {
        QMessageBox::warning(this, tr("Attach recording"), tr("Could not copy the recording next to the song."));
        return;
    }

    QFile f(songPath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    QString xml = QString::fromUtf8(f.readAll());
    f.close();
    xml = withAudioElement(xml, audioName, detected.offset);
    // Offer the recording's tempo if the song's differs noticeably.
    if (std::abs(detected.bpm - m_song->bpm) / m_song->bpm > 0.03) {
        const auto r = QMessageBox::question(this, tr("Attach recording"),
                                             tr("The recording is about %1 BPM, the song says %2 BPM.\n"
                                                "Use the recording's tempo so they stay together?")
                                             .arg(qRound(detected.bpm)).arg(m_song->bpm));
        if (r == QMessageBox::Yes) {
            static const QRegularExpression bpmAttr(QStringLiteral("(<song\\b[^>]*\\bbpm=\")[^\"]*(\")"));
            const auto bm = bpmAttr.match(xml);
            if (bm.hasMatch())
                xml.replace(int(bm.capturedStart()), int(bm.capturedLength()),
                            bm.captured(1) + QString::number(detected.bpm, 'f', 2) + bm.captured(2));
        }
    }
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Attach recording"), tr("Could not save %1").arg(songPath));
        return;
    }
    f.write(xml.toUtf8());
    f.close();
    refreshSongList();
    loadSongFile(songPath);
    m_status->setText(tr("Recording attached. If the guitar and the recording drift apart, nudge Sync until they line up."));
}

void MainWindow::addLyrics()
{
    if (!m_song || !m_timeline) {
        QMessageBox::information(this, tr("Add lyrics"), tr("Open a song first."));
        return;
    }
    if (m_editor->isModified() && m_editor->filePath() == m_song->filePath) {
        QMessageBox::information(this, tr("Add lyrics"),
                                 tr("The song has unsaved changes in the editor. Save them (Ctrl+S) or revert them first."));
        return;
    }
    LyricsDialog dlg(m_timeline, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // Songs outside My Songs (the examples) get a copy, so updates never overwrite your lyrics.
    const bool inMySongs = QFileInfo(m_song->filePath).absolutePath().startsWith(QDir(userSongsDir()).absolutePath());
    const QString target = inMySongs ? m_song->filePath : uniqueSongPath(m_song->title);
    const QString audio = m_song->audioFile.isEmpty()
            ? QString() : QFileInfo(target).absoluteDir().relativeFilePath(m_song->audioFile);
    const QString xml = LyricsAligner::songXmlWithLyrics(*m_song, *m_timeline, dlg.placements(), audio);

    QString err;
    auto check = loadSongFromData(xml.toUtf8(), target, &err);
    if (!check || !buildTimeline(check, &err)) {
        QMessageBox::warning(this, tr("Add lyrics"), tr("Could not build the song with lyrics:\n%1").arg(err));
        return;
    }
    if (inMySongs) {
        // Keep the previous version, just in case.
        QFile::remove(target + QStringLiteral(".bak"));
        QFile::copy(target, target + QStringLiteral(".bak"));
    }
    QSaveFile f(target);
    if (!f.open(QIODevice::WriteOnly) || f.write(xml.toUtf8()) < 0 || !f.commit()) {
        QMessageBox::warning(this, tr("Add lyrics"), tr("Could not save %1").arg(target));
        return;
    }
    refreshSongList();
    loadSongFile(target, target == m_song->filePath);
    m_status->setText(inMySongs ? tr("Lyrics added (the previous version is kept as %1.bak)").arg(QFileInfo(target).fileName())
                                : tr("Lyrics added to a copy in My Songs: %1").arg(QFileInfo(target).fileName()));
}

void MainWindow::updateInfo()
{
    if (!m_song)
        return;
    QString info = m_song->artist;
    if (!info.isEmpty())
        info += QStringLiteral("  ·  ");
    info += tr("%1 BPM  ·  %2 beats per bar").arg(m_song->bpm).arg(m_song->beatsPerBar);
    const int capo = m_view ? Arranger::capoOf(*m_view) : m_song->capo;
    if (capo != m_song->capo)
        info += capo > 0 ? tr("  ·  Capo %1 (song: %2)").arg(capo).arg(m_song->capo)
                         : tr("  ·  No capo (song: %1)").arg(m_song->capo);
    else if (capo > 0)
        info += tr("  ·  Capo %1").arg(capo);
    const auto level = ChordName::Level(m_chordsBox->currentData().toInt());
    if (level == ChordName::Level::Simplify)
        info += tr("  ·  Simplified chords");
    else if (level == ChordName::Level::SimplifyPlus)
        info += tr("  ·  Simplified chords (easy shapes)");
    if (!m_song->audioFile.isEmpty())
        info += tr("  ·  ♪ %1").arg(QFileInfo(m_song->audioFile).fileName());
    m_info->setText(info);
}

void MainWindow::rearrange()
{
    if (!m_timeline)
        return;
    Arranger::Settings as;
    as.capo = m_capoBox->currentData().toInt();
    as.level = ChordName::Level(m_chordsBox->currentData().toInt());
    m_view = Arranger::arrange(m_timeline, as);
    m_engine->replaceTimeline(m_view);
    m_pattern->setTimeline(m_view);
    m_lyrics->setTimeline(m_view);
    updateInfo();
    updateView();
}

void MainWindow::buildLanguageMenu(QMenu *menu)
{
    auto *group = new QActionGroup(this);
    const QString current = QSettings().value(QStringLiteral("language"), QStringLiteral("auto")).toString();
    const QList<QPair<QString, QString>> langs = {
        {tr("Automatic (system language)"), QStringLiteral("auto")},
        {QStringLiteral("English"), QStringLiteral("en")},
        {QStringLiteral("العربية"), QStringLiteral("ar")},
    };
    for (const auto &l : langs) {
        QAction *a = menu->addAction(l.first);
        a->setCheckable(true);
        a->setChecked(current == l.second);
        group->addAction(a);
        const QString code = l.second;
        connect(a, &QAction::triggered, this, [this, code] {
            QSettings().setValue(QStringLiteral("language"), code);
            const auto r = QMessageBox::question(this, tr("Language"),
                                                 tr("The new language is used after a restart. Restart now?"));
            if (r == QMessageBox::Yes && close()) {
                QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
                QCoreApplication::quit();
            }
        });
    }
}
