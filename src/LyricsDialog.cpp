#include "LyricsDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

LyricsDialog::LyricsDialog(std::shared_ptr<const Timeline> tl, QWidget *parent) : QDialog(parent), m_tl(std::move(tl))
{
    setWindowTitle(tr("Add lyrics"));
    resize(980, 640);

    m_text = new QPlainTextEdit;
    m_text->setPlaceholderText(tr("Paste the song's words here, one sung line per line.\n"
                                  "Leave an empty line between verses.\n\n"
                                  "الصق كلمات الأغنية هنا، سطراً في كل سطر."));
    QFont f = m_text->font();
    f.setPointSizeF(f.pointSizeF() * 1.2);
    m_text->setFont(f);

    const int bars = int(m_tl->bars.size());
    m_start = new QSpinBox;
    m_start->setRange(1, std::max(1, bars));
    m_start->setToolTip(tr("The bar where the first word is sung (skip the intro)"));
    m_perLine = new QSpinBox;
    m_perLine->setRange(1, 16);
    m_perLine->setToolTip(tr("How many bars each lyric line lasts"));
    m_pause = new QSpinBox;
    m_pause->setRange(0, 16);
    m_pause->setToolTip(tr("Bars without singing for each empty line (between verses)"));
    auto *autoBtn = new QPushButton(tr("Auto"));
    autoBtn->setToolTip(tr("Guess the settings from the song and the number of lines"));

    auto *form = new QFormLayout;
    form->addRow(tr("Singing starts at bar"), m_start);
    form->addRow(tr("Bars per lyric line"), m_perLine);
    form->addRow(tr("Pause for an empty line (bars)"), m_pause);
    form->addRow(QString(), autoBtn);

    m_preview = new QTableWidget(0, 3);
    m_preview->setHorizontalHeaderLabels({tr("Bars"), tr("Chords"), tr("Lyrics")});
    m_preview->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_preview->verticalHeader()->hide();
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_preview->setSelectionMode(QAbstractItemView::NoSelection);

    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    auto *hint = new QLabel(tr("After adding them, play the song: if a line comes too early or late, change the "
                               "settings here, or fix single lines in the editor (click a lyric line while it plays)."));
    hint->setWordWrap(true);

    auto *left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("<b>Lyrics</b>")));
    left->addWidget(m_text, 1);
    auto *right = new QVBoxLayout;
    right->addLayout(form);
    right->addWidget(new QLabel(tr("<b>Preview</b>")));
    right->addWidget(m_preview, 1);
    right->addWidget(m_summary);
    auto *cols = new QHBoxLayout;
    cols->addLayout(left, 1);
    cols->addLayout(right, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Add to song"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!m_placements.isEmpty())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addLayout(cols, 1);
    root->addWidget(hint);
    root->addWidget(buttons);

    connect(m_text, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_userTouched)
            autoSettings();
        updatePreview();
    });
    for (QSpinBox *sb : {m_start, m_perLine, m_pause})
        connect(sb, &QSpinBox::valueChanged, this, [this] {
            m_userTouched = true;
            updatePreview();
        });
    connect(autoBtn, &QPushButton::clicked, this, [this] {
        m_userTouched = false;
        autoSettings();
        updatePreview();
    });
    autoSettings();
    updatePreview();
}

void LyricsDialog::autoSettings()
{
    const auto o = LyricsAligner::suggestOptions(*m_tl, LyricsAligner::splitLyrics(m_text->toPlainText()));
    const QSignalBlocker b1(m_start), b2(m_perLine), b3(m_pause);
    m_start->setValue(o.startBar + 1);
    m_perLine->setValue(o.barsPerLine);
    m_pause->setValue(o.pauseBars);
}

void LyricsDialog::updatePreview()
{
    const QStringList lines = LyricsAligner::splitLyrics(m_text->toPlainText());
    LyricsAligner::Options o;
    o.startBar = m_start->value() - 1;
    o.barsPerLine = m_perLine->value();
    o.pauseBars = m_pause->value();
    m_placements = LyricsAligner::place(*m_tl, lines, o);

    m_preview->setRowCount(m_placements.size());
    for (int i = 0; i < m_placements.size(); ++i) {
        const auto &p = m_placements[i];
        QStringList chords;
        for (int b = p.firstBar; b < p.firstBar + p.barCount; ++b)
            chords << LyricsAligner::barChordText(*m_tl, b);
        const QString bars = p.barCount > 1 ? QStringLiteral("%1-%2").arg(p.firstBar + 1).arg(p.firstBar + p.barCount)
                                            : QString::number(p.firstBar + 1);
        m_preview->setItem(i, 0, new QTableWidgetItem(bars));
        m_preview->setItem(i, 1, new QTableWidgetItem(chords.join(QStringLiteral(" | "))));
        auto *lyric = new QTableWidgetItem(p.text);
        if (p.text.isRightToLeft())
            lyric->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_preview->setItem(i, 2, lyric);
    }
    m_preview->resizeColumnToContents(0);
    m_preview->resizeColumnToContents(1);

    int sung = 0;
    for (const QString &l : lines)
        sung += !l.isEmpty();
    const int total = int(m_tl->bars.size());
    const int lastBar = m_placements.isEmpty() ? 0 : m_placements.last().firstBar + m_placements.last().barCount;
    QString msg;
    if (sung == 0)
        msg = tr("Paste some lyrics to start.");
    else if (m_placements.size() < sung)
        msg = tr("<span style='color:#d03030'>Only %1 of %2 lines fit in the song (%3 bars). "
                 "Use fewer bars per line or start earlier.</span>").arg(m_placements.size()).arg(sung).arg(total);
    else
        msg = tr("%1 lines on bars %2-%3 of %4.").arg(sung).arg(m_start->value()).arg(lastBar).arg(total);
    m_summary->setText(msg);
}
