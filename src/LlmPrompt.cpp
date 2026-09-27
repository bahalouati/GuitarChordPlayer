#include "LlmPrompt.h"

#include <QFile>

QString llmPrompt()
{
    QFile f(QStringLiteral(":/docs/LLM_PROMPT.md"));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QString md = QString::fromUtf8(f.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    // The prompt is the ````text ... ```` block of the document.
    const QString open = QStringLiteral("````text\n");
    const int start = int(md.indexOf(open));
    const int end = int(md.lastIndexOf(QStringLiteral("\n````")));
    if (start < 0 || end <= start)
        return md;
    return md.mid(start + open.size(), end - start - open.size()) + QLatin1Char('\n');
}

QString extractSongXml(const QString &answer)
{
    int start = int(answer.indexOf(QLatin1String("<?xml")));
    if (start < 0)
        start = int(answer.indexOf(QLatin1String("<song")));
    const int end = int(answer.lastIndexOf(QLatin1String("</song>")));
    if (start < 0 || end < start)
        return QString();
    return answer.mid(start, end + 7 - start) + QLatin1Char('\n');
}
