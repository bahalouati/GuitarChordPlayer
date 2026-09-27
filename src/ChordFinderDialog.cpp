#include "ChordFinderDialog.h"

#include "AudioEngine.h"
#include "ChordDiagramWidget.h"
#include "ChordLibrary.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

static QString fretString(const ChordShape &c)
{
    const bool wide = std::any_of(c.frets.begin(), c.frets.end(), [](int f) { return f > 9; });
    QStringList parts;
    for (int f : c.frets)
        parts << (f < 0 ? QStringLiteral("x") : QString::number(f));
    return parts.join(wide ? QStringLiteral(" ") : QString());
}

ChordFinderDialog::ChordFinderDialog(AudioEngine *engine, QWidget *parent) : QDialog(parent), m_engine(engine)
{
    setWindowTitle(tr("Chord Finder"));
    resize(620, 460);

    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("Type a chord, e.g. F#m7, Bb, Cadd9, D/F#"));
    auto *strum = new QPushButton(tr("Strum it"));
    m_diagram = new ChordDiagramWidget;
    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *list = new QListWidget;
    QStringList names = ChordLibrary::builtIn().keys();
    std::sort(names.begin(), names.end());
    list->addItems(names);
    list->setMaximumWidth(120);

    auto *left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("<b>Built-in</b>")));
    left->addWidget(list, 1);

    auto *right = new QVBoxLayout;
    auto *row = new QHBoxLayout;
    row->addWidget(m_name, 1);
    row->addWidget(strum);
    right->addLayout(row);
    right->addWidget(m_diagram, 1);
    right->addWidget(m_info);

    auto *lay = new QHBoxLayout(this);
    lay->addLayout(left);
    lay->addLayout(right, 1);

    connect(m_name, &QLineEdit::textChanged, this, [this](const QString &t) { showChord(t, false); });
    connect(m_name, &QLineEdit::returnPressed, this, [this] { showChord(m_name->text(), true); });
    connect(strum, &QPushButton::clicked, this, [this] { showChord(m_name->text(), true); });
    connect(list, &QListWidget::currentTextChanged, this, [this](const QString &t) {
        m_name->setText(t);
        showChord(t, true);
    });
    showChord(QString(), false);
}

void ChordFinderDialog::showChord(const QString &name, bool strum)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) {
        m_diagram->setChord(nullptr);
        m_info->setText(tr("Any chord name that works here also works in your song files."));
        return;
    }
    const auto shape = ChordLibrary::lookup(n);
    m_diagram->setChord(shape ? &*shape : nullptr);
    if (!shape) {
        m_info->setText(tr("<span style='color:#d03030'>Unknown chord \"%1\".</span> You can still use it by "
                           "adding its fingering to the song:<br><tt>&lt;chord name=\"%1\" frets=\"x32010\"/&gt;</tt>")
                        .arg(n.toHtmlEscaped()));
        return;
    }
    const bool builtIn = ChordLibrary::builtIn().contains(n);
    m_info->setText(tr("frets=\"%1\" (low E to high e)  -  %2")
                    .arg(fretString(*shape), builtIn ? tr("built-in shape") : tr("worked out automatically")));
    if (strum && m_engine)
        m_engine->previewChord(*shape);
}
