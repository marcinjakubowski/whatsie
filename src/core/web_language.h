#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace whatsie::core {

struct WebLanguageEntry
{
    QString code;        ///< Locale code (e.g. "pl_PL", "de_DE"), or empty for auto-detect
    QString displayName; ///< Human-readable label for UI display
    QString searchKey;   ///< Lower-case tokens for filter matching
};

/// Curated list of WhatsApp Web supported languages with Auto-detect at index 0.
[[nodiscard]] QList<WebLanguageEntry> availableWebLanguages();

/// Converts a language code to an HTTP Accept-Language header value.
/// Returns empty if code is empty (Auto-detect), indicating no header override.
[[nodiscard]] QString acceptLanguageHeader(const QString& code);

/// Returns the language array for client JavaScript navigator.languages.
/// Returns empty if code is empty (Auto-detect).
[[nodiscard]] QStringList webClientLanguages(const QString& code);

} // namespace whatsie::core
