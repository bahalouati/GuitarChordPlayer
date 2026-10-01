#pragma once

#include <QHash>
#include <QFont>
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

// Installs the user's language ("auto", "en" or "ar"); returns the code in use. In Arabic the
// whole interface uses the app's Arabic font.
QString installLanguage(QApplication &app);

// Loads the fonts that come with the app (IBM Plex Sans Arabic, SIL Open Font License).
// Call once after the application object exists.
void loadAppFonts();

// The font for Arabic text and for the stage and printed sheets: IBM Plex Sans Arabic (it has
// Latin letters too), falling back to the system font if it couldn't be loaded.
QFont appTextFont(const QFont &base);
