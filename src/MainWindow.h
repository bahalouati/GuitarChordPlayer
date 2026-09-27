#pragma once

#include "AudioEngine.h"
#include "Song.h"

#include <QMainWindow>
#include <memory>

class ChordDiagramWidget;
class LyricsWidget;
class PatternWidget;
class SongEditor;
class Updater;
class QDockWidget;
class QCheckBox;
class QComboBox;
class QSpinBox;
struct AudioClip;
class QFileSystemWatcher;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool loadSongFile(const QString &path, bool keepPosition = false);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private slots:
    void togglePlay();
    void stop();
    void previousSection();
    void nextSection();
    void openSong();
    void newSong();
    void toggleEditor();
    void showChordFinder();
    void newSongFromAudio();
    void attachRecording();
    void addLyrics();
    void rearrange();
    void reloadSong();
    void exportWav();
    void refreshSongList();
    void updateView();
    void applyTempo();
    void applyLoop();
    void showFormatHelp();

private:
    bool loadSongFileImpl(const QString &path, bool keepPosition);
    void applySong(std::shared_ptr<Song> song, std::shared_ptr<Timeline> tl, bool keepPosition);
    void applyLiveEdit(const QString &path, const QByteArray &xml);
    void updateInfo();
    void buildLanguageMenu(QMenu *menu);
    void buildUi();
    void buildMenus();
    QString examplesDir() const;
    QString userSongsDir() const;
    QString uniqueSongPath(const QString &title) const;
    void saveNewSong(const QString &title, const QString &xml);
    // Recording (MP3) support
    std::shared_ptr<const AudioClip> loadRecording(const QString &path);
    void applyRecording(bool songChanged);
    void saveAudioOffset();
    bool analyseRecording(const AudioClip &clip, struct DetectedSong *out);
    void importSong(const QString &path);
    void watchPaths();
    void showError(const QString &message);
    int currentPlay() const;
    void jumpToPlay(int play);
    void setPlayButton(bool playing);

    AudioEngine *m_engine = nullptr;
    std::shared_ptr<Song> m_song;
    std::shared_ptr<Timeline> m_timeline;          // the song as written in its file
    std::shared_ptr<const Timeline> m_view;        // re-fingered for the player (capo, simplify, names)
    QComboBox *m_capoBox = nullptr;
    QComboBox *m_chordsBox = nullptr;

    QListWidget *m_songList = nullptr;
    QLabel *m_banner = nullptr;
    SongEditor *m_editor = nullptr;
    QDockWidget *m_editorDock = nullptr;
    QTimer *m_dirRefresh = nullptr;
    Updater *m_updater = nullptr;
    QPushButton *m_updateBtn = nullptr;
    QListWidget *m_sectionList = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_info = nullptr;
    QLabel *m_section = nullptr;
    QLabel *m_status = nullptr;
    ChordDiagramWidget *m_current = nullptr;
    ChordDiagramWidget *m_next = nullptr;
    PatternWidget *m_pattern = nullptr;
    LyricsWidget *m_lyrics = nullptr;
    QPushButton *m_playBtn = nullptr;
    QSlider *m_tempo = nullptr;
    QLabel *m_tempoLabel = nullptr;
    QSlider *m_volume = nullptr;
    QCheckBox *m_loop = nullptr;
    QCheckBox *m_metronome = nullptr;
    QCheckBox *m_countIn = nullptr;
    QCheckBox *m_guitarOn = nullptr;
    QCheckBox *m_recordingOn = nullptr;
    QSlider *m_recordingVolume = nullptr;
    QSpinBox *m_syncMs = nullptr;
    QWidget *m_recordingControls = nullptr;
    QTimer *m_offsetSave = nullptr;
    std::shared_ptr<const AudioClip> m_clip;
    QString m_clipKey;   // path + modification time of the loaded recording
    QTimer *m_timer = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;

    int m_viewBar = 0;
    int m_viewStep = 0;
    bool m_audioOk = false;
    bool m_loading = false; // inside loadSongFile
};
