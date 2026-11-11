#include "core/web_language.h"

#include <QCoreApplication>
#include <QLocale>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace whatsie::core {

namespace {

// Official locales supported by WhatsApp Web (from WhatsApp Web client bundles and alternates)
constexpr const char* const kWhatsAppLocales[] = {
    "af_ZA", "ar_AR", "as_IN", "az_AZ", "bg_BG", "bn_IN", "bs_BA", "ca_ES",
    "cs_CZ", "da_DK", "de_DE", "el_GR", "en_GB", "en_US", "es_LA", "es_MX",
    "et_EE", "fa_IR", "fi_FI", "fr_FR", "ga_IE", "gu_IN", "ha_NG", "he_IL",
    "hi_IN", "hr_HR", "hu_HU", "id_ID", "is_IS", "it_IT", "ja_JP", "ka_GE",
    "kk_KZ", "kn_IN", "ko_KR", "lo_LA", "lt_LT", "lv_LV", "mk_MK", "ml_IN",
    "mr_IN", "ms_MY", "my_MM", "nb_NO", "ne_NP", "nl_NL", "nn_NO", "or_IN",
    "pa_IN", "pl_PL", "pt_BR", "pt_PT", "ro_RO", "ru_RU", "sk_SK", "sl_SI",
    "sq_AL", "sr_RS", "sv_SE", "sw_KE", "ta_IN", "te_IN", "th_TH", "tl_PH",
    "tr_TR", "uk_UA", "ur_PK", "uz_UZ", "vi_VN", "zh_CN", "zh_HK", "zh_TW",
    "zu_ZA"
};

} // namespace

QList<WebLanguageEntry> availableWebLanguages()
{
    QList<WebLanguageEntry> entries;
    entries.reserve(static_cast<qsizetype>(std::size(kWhatsAppLocales)) + 1);

    for (const char* localeStr : kWhatsAppLocales) {
        const QString code = QString::fromLatin1(localeStr);
        const QLocale loc(code);
        const QString eng = QLocale::languageToString(loc.language());
        QString native = loc.nativeLanguageName();
        if (!native.isEmpty()) {
            native = native.left(1).toUpper() + native.mid(1);
        }
        const QString terr = loc.nativeTerritoryName();

        QString display;
        if (!terr.isEmpty() && (code.contains(u'_') || loc.language() == QLocale::English)) {
            display = QString(u"%1 — %2 (%3) [%4]"_s).arg(eng, native, terr, code);
        } else {
            display = QString(u"%1 — %2 [%3]"_s).arg(eng, native, code);
        }

        const QString searchKey = QString(u"%1 %2 %3 %4"_s).arg(eng, native, terr, code).toLower();
        entries.append(WebLanguageEntry{code, display, searchKey});
    }

    std::sort(entries.begin(), entries.end(), [](const WebLanguageEntry& a, const WebLanguageEntry& b) {
        return a.displayName.localeAwareCompare(b.displayName) < 0;
    });

    WebLanguageEntry autoEntry;
    autoEntry.code = QString();
    autoEntry.displayName = QCoreApplication::translate("WebLanguage", "Auto-detect (system default)");
    autoEntry.searchKey = u"auto detect default system"_s;
    entries.prepend(autoEntry);

    return entries;
}

QString acceptLanguageHeader(const QString& code)
{
    const QString trimmed = code.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QString hyphenTag = trimmed;
    hyphenTag.replace(u'_', u'-');
    const QString langCode = trimmed.section(u'_', 0, 0).toLower();

    if (langCode == u"en"_s) {
        if (hyphenTag.compare(langCode, Qt::CaseInsensitive) == 0) {
            return u"en;q=0.9"_s;
        }
        return QString(u"%1,en;q=0.9"_s).arg(hyphenTag);
    }

    if (hyphenTag.compare(langCode, Qt::CaseInsensitive) == 0) {
        return QString(u"%1;q=0.9,en-US;q=0.8,en;q=0.7"_s).arg(langCode);
    }

    return QString(u"%1,%2;q=0.9,en-US;q=0.8,en;q=0.7"_s).arg(hyphenTag, langCode);
}

QStringList webClientLanguages(const QString& code)
{
    const QString trimmed = code.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QString hyphenTag = trimmed;
    hyphenTag.replace(u'_', u'-');
    const QString langCode = trimmed.section(u'_', 0, 0).toLower();

    QStringList list;
    list.append(hyphenTag);
    if (hyphenTag.compare(langCode, Qt::CaseInsensitive) != 0) {
        list.append(langCode);
    }
    if (langCode != u"en"_s) {
        list.append(u"en-US"_s);
        list.append(u"en"_s);
    }
    return list;
}

} // namespace whatsie::core
