#include "SongEditor.h"

#include "Song.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QShortcut>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

class XmlHighlighter : public QSyntaxHighlighter
{
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;

protected:
    void highlightBlock(const QString &text) override
    {
        QTextCharFormat tag, attr, value, comment;
        tag.setForeground(QColor(40, 110, 210));
        tag.setFontWeight(QFont::Bold);
        attr.setForeground(QColor(200, 110, 20));
        value.setForeground(QColor(40, 150, 80));
        comment.setForeground(QColor(140, 140, 140));
        comment.setFontItalic(true);

        static const QRegularExpression tagRe(QStringLiteral("</?[A-Za-z?][\\w-]*|/?>|\\?>"));
        static const QRegularExpression attrRe(QStringLiteral("([A-Za-z][\\w-]*)\\s*=\\s*(\"[^\"]*\"|'[^']*')"));
        for (auto it = tagRe.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            setFormat(int(m.capturedStart()), int(m.capturedLength()), tag);
        }
        for (auto it = attrRe.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            setFormat(int(m.capturedStart(1)), int(m.capturedLength(1)), attr);
            setFormat(int(m.capturedStart(2)), int(m.capturedLength(2)), value);
        }

        // Comments, possibly spanning lines (block state 1 = inside a comment).
        int start = previousBlockState() == 1 ? 0 : int(text.indexOf(QLatin1String("<!--")));
        setCurrentBlockState(0);
        while (start >= 0) {
            const int end = int(text.indexOf(QLatin1String("-->"), start));
            if (end < 0) {
                setFormat(start, int(text.length()) - start, comment);
                setCurrentBlockState(1);
                break;
            }
            setFormat(start, end + 3 - start, comment);
            start = int(text.indexOf(QLatin1String("<!--"), end + 3));
        }
    }
};

} // namespace

SongEditor::SongEditor(QWidget *parent) : QWidget(parent)
{
    m_fileLabel = new QLabel;
    auto *saveBtn = new QPushButton(tr("Save && Play (Ctrl+S)"));
    connect(saveBtn, &QPushButton::clicked, this, &SongEditor::save);
    auto *revertBtn = new QPushButton(tr("Revert"));
    connect(revertBtn, &QPushButton::clicked, this, &SongEditor::revert);

    auto *insert = new QToolButton;
    insert->setText(tr("Insert"));
    insert->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(insert);
    menu->addAction(tr("Section"), this, [this] {
        insertSnippet(QStringLiteral("<section name=\"Chorus\" pattern=\"folk\">\n  <bars>C | G | D | Em</bars>\n</section>\n"));
    });
    menu->addAction(tr("Bars"), this, [this] { insertSnippet(QStringLiteral("<bars>G | D | Em | C</bars>\n")); });
    menu->addAction(tr("Play (arrangement)"), this, [this] {
        insertSnippet(QStringLiteral("<play section=\"Chorus\" repeat=\"2\"/>\n"));
    });
    menu->addAction(tr("Own pattern"), this, [this] {
        insertSnippet(QStringLiteral("<pattern name=\"mine\" subdivision=\"2\">D - D U - U D U</pattern>\n"));
    });
    menu->addAction(tr("Own chord fingering"), this, [this] {
        insertSnippet(QStringLiteral("<chord name=\"Cadd9\" frets=\"x32033\" fingers=\"021034\"/>\n"));
    });
    insert->setMenu(menu);

    auto *top = new QHBoxLayout;
    top->addWidget(m_fileLabel, 1);
    top->addWidget(insert);
    top->addWidget(revertBtn);
    top->addWidget(saveBtn);

    m_edit = new QPlainTextEdit;
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_edit->setFont(mono);
    m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_edit->setTabStopDistance(QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')) * 2);
    new XmlHighlighter(m_edit->document());

    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->addLayout(top);
    lay->addWidget(m_edit, 1);
    lay->addWidget(m_status);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(400);
    connect(m_timer, &QTimer::timeout, this, &SongEditor::validate);
    connect(m_edit, &QPlainTextEdit::textChanged, this, [this] { m_timer->start(); });
    connect(m_edit->document(), &QTextDocument::modificationChanged, this, &SongEditor::updateTitle);

    auto *sc = new QShortcut(QKeySequence::Save, this);
    sc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sc, &QShortcut::activated, this, &SongEditor::save);
}

void SongEditor::openFile(const QString &path)
{
    if (path == m_path && isModified())
        return; // don't throw away unsaved edits
    if (isModified() && !m_path.isEmpty()) {
        const auto r = QMessageBox::question(this, tr("Unsaved changes"),
                                             tr("Save your changes to %1 first?").arg(QFileInfo(m_path).fileName()),
                                             QMessageBox::Save | QMessageBox::Discard);
        if (r == QMessageBox::Save)
            save();
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    m_path = path;
    m_edit->setPlainText(QString::fromUtf8(f.readAll()));
    m_edit->document()->setModified(false);
    updateTitle();
    validate();
}

bool SongEditor::isModified() const
{
    return m_edit->document()->isModified();
}

void SongEditor::fileChangedOnDisk(const QString &path)
{
    if (path == m_path && !isModified()) {
        const int pos = m_edit->textCursor().position();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return;
        const QString text = QString::fromUtf8(f.readAll());
        if (text == m_edit->toPlainText())
            return;
        m_edit->setPlainText(text);
        m_edit->document()->setModified(false);
        QTextCursor c = m_edit->textCursor();
        c.setPosition(std::min(pos, int(text.size())));
        m_edit->setTextCursor(c);
        validate();
    }
}

void SongEditor::updateTitle()
{
    m_fileLabel->setText(m_path.isEmpty() ? tr("No song open")
                                          : QStringLiteral("<b>%1</b>%2").arg(QFileInfo(m_path).fileName(),
                                                                              isModified() ? tr("  (not saved)") : QString()));
    m_fileLabel->setToolTip(m_path);
}

void SongEditor::validate()
{
    QString err;
    auto song = loadSongFromData(m_edit->toPlainText().toUtf8(), m_path, &err);
    if (song && buildTimeline(song, &err)) {
        m_status->setStyleSheet(QStringLiteral("color: #2e8b57;"));
        m_status->setText(isModified() ? tr("✓ Looks good - press Ctrl+S to save and hear it") : tr("✓ Looks good"));
    } else {
        m_status->setStyleSheet(QStringLiteral("color: #d03030;"));
        m_status->setText(err);
    }
}

bool SongEditor::save()
{
    if (m_path.isEmpty())
        return false;
    QString path = m_path;
    QSaveFile f(path);
    bool ok = f.open(QIODevice::WriteOnly);
    if (ok) {
        f.write(m_edit->toPlainText().toUtf8());
        ok = f.commit();
    }
    if (!ok) {
        // e.g. the example songs inside a read-only install folder: save a copy instead.
        path = QFileDialog::getSaveFileName(this, tr("Save a copy to My Songs"),
                                            m_userDir + QLatin1Char('/') + QFileInfo(m_path).fileName(),
                                            tr("Song files (*.xml)"));
        if (path.isEmpty())
            return false;
        QSaveFile copy(path);
        if (!copy.open(QIODevice::WriteOnly) || (copy.write(m_edit->toPlainText().toUtf8()), !copy.commit())) {
            QMessageBox::warning(this, tr("Save failed"), tr("Could not save %1").arg(path));
            return false;
        }
        m_path = path;
    }
    m_edit->document()->setModified(false);
    updateTitle();
    validate();
    emit saved(m_path);
    return true;
}

void SongEditor::revert()
{
    if (m_path.isEmpty())
        return;
    m_edit->document()->setModified(false);
    const QString p = m_path;
    m_path.clear();
    openFile(p);
}

void SongEditor::insertSnippet(const QString &text)
{
    QTextCursor c = m_edit->textCursor();
    // Keep the indentation of the current line.
    const QString line = c.block().text();
    const QString indent = line.left(int(line.size() - QString(line).remove(QRegularExpression(QStringLiteral("^\\s+"))).size()));
    QString t = text;
    t.replace(QLatin1Char('\n'), QLatin1Char('\n') + indent);
    if (t.endsWith(indent))
        t.chop(indent.size());
    if (!c.atBlockStart())
        c.insertText(QStringLiteral("\n") + indent);
    c.insertText(t);
    m_edit->setFocus();
}
