#pragma once

#include <QString>

// The prompt from docs/LLM_PROMPT.md (embedded as a resource), ready to paste into an LLM.
QString llmPrompt();

// Pulls the song XML out of an LLM answer (drops ```xml fences and any text around it).
// Returns an empty string if there is no <song> in it.
QString extractSongXml(const QString &answer);
