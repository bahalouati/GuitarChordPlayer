#include "NewSongDialog.h"

#include "ChordSheet.h"
#include "LlmPrompt.h"
#include "Song.h"

#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QRegularExpression>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

NewSongDialog::NewSongDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("New Song"));
    resize(900, 640);

    m_title = new QLineEdit;
    m_title->setPlaceholderText(tr("My Song"));
    m_artist = new QLineEdit;
    m_bpm = new QDoubleSpinBox;
    m_bpm->setRange(30, 260);
    m_bpm->setDecimals(0);
    m_bpm->setValue(90);
    m_meter = new QComboBox;
    m_meter->addItem(tr("4/4 (most songs)"), 4);
    m_meter->addItem(tr("3/4 (waltz)"), 3);
    m_meter->addItem(tr("6/8 (two beats, each split in three)"), 2);
    m_capo = new QSpinBox;
    m_capo->setRange(0, 12);
    m_capo->setSpecialValueText(tr("No capo"));

    auto *form = new QFormLayout;
    form->addRow(tr("Title"), m_title);
    form->addRow(tr("Artist"), m_artist);
    form->addRow(tr("Tempo (BPM)"), m_bpm);
    form->addRow(tr("Time signature"), m_meter);
    form->addRow(tr("Capo"), m_capo);

    m_sheet = new QPlainTextEdit;
    m_sheet->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_sheet->setPlainText(exampleChordSheet());
    m_sheet->setTabChangesFocus(true);

    // Pattern reference: double-click inserts the name at the cursor.
    auto *patterns = new QListWidget;
    for (const PatternPreset &p : patternPresets()) {
        auto *it = new QListWidgetItem(QStringLiteral("%1   %2").arg(QString::fromLatin1(p.name), QString::fromLatin1(p.steps)));
        it->setToolTip(QString::fromLatin1(p.description));
        it->setData(Qt::UserRole, QString::fromLatin1(p.name));
        patterns->addItem(it);
    }
    patterns->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    connect(patterns, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        m_sheet->insertPlainText(it->data(Qt::UserRole).toString());
        m_sheet->setFocus();
    });

    auto *side = new QVBoxLayout;
    side->addLayout(form);
    side->addWidget(new QLabel(tr("<b>Strum patterns</b> (double-click to insert,<br>hover for a description)")));
    side->addWidget(patterns, 1);
    auto *hint = new QLabel(tr("<b>Pattern symbols:</b> D/U strum down/up, d/u light, X muted chuck, "
                               "- rest, B bass note, A alternate bass, 1-6 pick a string (1 = high e)."));
    hint->setWordWrap(true);
    side->addWidget(hint);

    auto *main = new QVBoxLayout;
    main->addWidget(new QLabel(tr("<b>Chords</b> - one [Section] per part of the song, bars separated by |")));
    main->addWidget(m_sheet, 1);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    main->addWidget(m_status);

    auto *cols = new QHBoxLayout;
    cols->addLayout(side, 2);
    cols->addLayout(main, 3);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create && Play"));
    connect(buttons, &QDialogButtonBox::accepted, this, &NewSongDialog::tryAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Let an LLM write the song: copy the prompt, then paste its answer back.
    auto *copyBtn = new QPushButton(tr("Copy LLM prompt"));
    copyBtn->setToolTip(tr("Copies a prompt for ChatGPT, Claude, Gemini... that makes them write a song file"));
    connect(copyBtn, &QPushButton::clicked, this, &NewSongDialog::copyPrompt);
    auto *pasteBtn = new QPushButton(tr("Paste LLM answer"));
    pasteBtn->setToolTip(tr("Creates the song from the XML the LLM wrote (copy its answer first)"));
    connect(pasteBtn, &QPushButton::clicked, this, &NewSongDialog::pasteAnswer);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(copyBtn);
    bottom->addWidget(pasteBtn);
    bottom->addStretch(1);
    bottom->addWidget(buttons);

    auto *root = new QVBoxLayout(this);
    root->addLayout(cols, 1);
    root->addLayout(bottom);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(300);
    connect(m_timer, &QTimer::timeout, this, &NewSongDialog::validate);
    auto kick = [this] { m_timer->start(); };
    connect(m_sheet, &QPlainTextEdit::textChanged, this, kick);
    connect(m_title, &QLineEdit::textChanged, this, kick);
    connect(m_meter, &QComboBox::currentIndexChanged, this, kick);
    validate();
    m_title->setFocus();
}

QString NewSongDialog::title() const
{
    if (!m_titleOverride.isEmpty())
        return m_titleOverride;
    const QString t = m_title->text().trimmed();
    return t.isEmpty() ? tr("My Song") : t;
}

bool NewSongDialog::build(QString *error)
{
    ChordSheetInfo info;
    info.title = title();
    info.artist = m_artist->text().trimmed();
    info.bpm = m_bpm->value();
    info.beatsPerBar = m_meter->currentData().toInt();
    info.capo = m_capo->value();
    info.defaultPattern = info.beatsPerBar == 3 ? QStringLiteral("waltz")
                        : info.beatsPerBar == 2 ? QStringLiteral("six-eight")
                                                : QStringLiteral("folk");
    QString xml;
    if (!chordSheetToXml(info, m_sheet->toPlainText(), &xml, error))
        return false;
    // Run it through the real loader so every problem is caught here.
    auto song = loadSongFromData(xml.toUtf8(), QString(), error);
    if (!song || !buildTimeline(song, error))
        return false;
    m_xml = xml;
    return true;
}

void NewSongDialog::validate()
{
    QString err;
    if (build(&err)) {
        m_status->setStyleSheet(QStringLiteral("color: #2e8b57;"));
        m_status->setText(tr("✓ Looks good"));
    } else {
        m_status->setStyleSheet(QStringLiteral("color: #d03030;"));
        m_status->setText(err);
    }
}

void NewSongDialog::tryAccept()
{
    QString err;
    if (build(&err))
        accept();
    else
        validate();
}

void NewSongDialog::copyPrompt()
{
    QString prompt = llmPrompt();
    // Fill in what is already typed in the dialog.
    if (!m_title->text().trimmed().isEmpty()) {
        QString song = m_title->text().trimmed();
        if (!m_artist->text().trimmed().isEmpty())
            song += tr(" by ") + m_artist->text().trimmed();
        prompt.replace(QStringLiteral("<SONG TITLE> by <ARTIST>"), song);
    }
    QApplication::clipboard()->setText(prompt);
    m_status->setStyleSheet(QString());
    m_status->setText(tr("Prompt copied. Paste it into ChatGPT, Claude, Gemini..., fill in the song at the end "
                         "(and lyrics if you have them), then copy the answer and press \"Paste LLM answer\"."));
}

void NewSongDialog::pasteAnswer()
{
    const QString xml = extractSongXml(QApplication::clipboard()->text());
    if (xml.isEmpty()) {
        QMessageBox::information(this, tr("Paste LLM answer"),
                                 tr("The clipboard has no song in it. Copy the LLM's whole answer "
                                    "(the part starting with <?xml or <song) and try again."));
        return;
    }
    // Keep it even if it has mistakes: the editor will show them so they can be fixed.
    QString err;
    auto song = loadSongFromData(xml.toUtf8(), QString(), &err);
    if (song && !song->title.isEmpty()) {
        m_titleOverride = song->title;
    } else {
        const auto m = QRegularExpression(QStringLiteral("title=\"([^\"]*)\"")).match(xml);
        m_titleOverride = m.hasMatch() ? m.captured(1) : tr("LLM Song");
    }
    m_xml = xml;
    accept();
}
