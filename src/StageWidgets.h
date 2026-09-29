#pragma once

#include "ChordLibrary.h"
#include "Song.h"

#include <QPalette>
#include <QWidget>
#include <memory>

// The live view ("stage"): what you see while a song plays, made to look good on screen and in
// recorded videos.
namespace Stage {

enum class Theme { Dark = 0, Light = 1 };

// Colours for the stage. Chord names and finger dots use QPalette::Link, highlights
// QPalette::Highlight; everything else the usual roles.
QPalette palette(Theme theme, const QPalette &base);

// The orange of whatever is playing right now.
QColor hot();

} // namespace Stage

// Background of the live view: a soft gradient in the stage colours. Children inherit its palette.
class StageWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StageWidget(QWidget *parent = nullptr);
    void setTheme(Stage::Theme theme);
    Stage::Theme theme() const { return m_theme; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Stage::Theme m_theme = Stage::Theme::Dark;
};

// Every chord shape the song uses, side by side; the one playing now is highlighted and the
// next one outlined.
class ChordStripWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ChordStripWidget(QWidget *parent = nullptr);
    void setChords(const QVector<ChordShape> &chords);
    void setActive(const QString &current, const QString &next);

    QSize sizeHint() const override { return {700, 150}; }
    QSize minimumSizeHint() const override { return {200, 90}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<ChordShape> m_chords;
    QString m_current, m_next;
};

// The song as a bar of sections with their names and a playhead. Click a section to jump there.
class SongProgressWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SongProgressWidget(QWidget *parent = nullptr);
    void setTimeline(std::shared_ptr<const Timeline> tl);
    void setPosition(int bar, double barFraction);

    QSize sizeHint() const override { return {700, 40}; }
    QSize minimumSizeHint() const override { return {200, 34}; }

signals:
    void playClicked(int play);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    QRectF segmentRect(int play) const;
    std::shared_ptr<const Timeline> m_tl;
    int m_bar = 0;
    double m_fraction = 0;
};

// The chords a timeline uses, in the order they first come up.
QVector<ChordShape> chordsInSong(const Timeline &tl);
