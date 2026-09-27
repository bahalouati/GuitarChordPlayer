#pragma once

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTimer;

// Lets the user type a simple chord sheet and turns it into a song XML file.
class NewSongDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NewSongDialog(QWidget *parent = nullptr);

    QString title() const;
    QString xml() const { return m_xml; }

private slots:
    void validate();
    void tryAccept();

private:
    bool build(QString *error);

    QLineEdit *m_title;
    QLineEdit *m_artist;
    QDoubleSpinBox *m_bpm;
    QComboBox *m_meter;
    QSpinBox *m_capo;
    QPlainTextEdit *m_sheet;
    QLabel *m_status;
    QTimer *m_timer;
    QString m_xml;
};
