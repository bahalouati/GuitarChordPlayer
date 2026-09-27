#pragma once

#include <QHash>
#include <QTranslator>

class QApplication;

// Translations from a JSON file ({"English text": "translation", ...}) embedded in the app.
// Keeps the build free of extra Qt translation tools; the key is the text passed to tr().
class JsonTranslator : public QTranslator
{
public:
    bool loadJson(const QString &path);
    bool isEmpty() const override { return m_map.isEmpty(); }
    QString translate(const char *context, const char *sourceText, const char *disambiguation = nullptr,
                      int n = -1) const override;

private:
    QHash<QString, QString> m_map;
};

// Installs the user's language ("auto", "en" or "ar"); returns the code in use.
QString installLanguage(QApplication &app);
