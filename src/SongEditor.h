#pragma once

#include <QWidget>

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

public slots:
    bool save();
    void revert();

signals:
    void saved(const QString &path);

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
    QTimer *m_timer;
};
