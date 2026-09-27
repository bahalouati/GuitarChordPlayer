#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QTimer;

// Built-in XML editor for the current song: checks as you type, Ctrl+S saves and replays.
class SongEditor : public QWidget
{
    Q_OBJECT
public:
    explicit SongEditor(QWidget *parent = nullptr);

    void setUserSongsDir(const QString &dir) { m_userDir = dir; }
    void openFile(const QString &path);
    QString filePath() const { return m_path; }
    bool isModified() const;
    // Called when the file changed on disk (e.g. edited in another program).
    void fileChangedOnDisk(const QString &path);
    // Puts the cursor on a line of the file, selecting the lyrics if it is a <line>.
    void goToLine(int line);

public slots:
    bool save();
    void revert();

signals:
    void saved(const QString &path);
    // Live mode: the text changed and is valid, so it can be played without saving.
    void liveEdit(const QString &path, const QByteArray &xml);

private slots:
    void validate();

private:
    void insertSnippet(const QString &text);
    void updateTitle();

    QString m_path;
    QString m_userDir;
    QPlainTextEdit *m_edit;
    QLabel *m_fileLabel;
    QLabel *m_status;
    QCheckBox *m_live;
    QTimer *m_timer;
};
