#include "Translator.h"

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>

bool JsonTranslator::loadJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        m_map.insert(it.key(), it.value().toString());
    return !m_map.isEmpty();
}

QString JsonTranslator::translate(const char *, const char *sourceText, const char *, int) const
{
    const auto it = m_map.constFind(QString::fromUtf8(sourceText));
    return it == m_map.constEnd() ? QString() : *it;
}

QString installLanguage(QApplication &app)
{
    QString lang = QSettings().value(QStringLiteral("language"), QStringLiteral("auto")).toString();
    if (lang == QLatin1String("auto"))
        lang = QLocale::system().language() == QLocale::Arabic ? QStringLiteral("ar") : QStringLiteral("en");
    if (lang == QLatin1String("ar")) {
        auto *tr = new JsonTranslator;
        if (tr->loadJson(QStringLiteral(":/resources/i18n/ar.json"))) {
            app.installTranslator(tr);
            QApplication::setLayoutDirection(Qt::RightToLeft);
        } else {
            delete tr;
            lang = QStringLiteral("en");
        }
    }
    return lang;
}
