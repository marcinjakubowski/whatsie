#include "web/web_profile.h"

#include "core/settings/settings.h"
#include "core/spellcheck.h"
#include "core/theme/theme_service.h"
#include "core/web_language.h"
#include "web/bridge.h"
#include "web/logging.h"
#include "web/proxy.h"
#include "web/script_bundle.h"
#include "web/user_agent.h"

#include <QDir>
#include <QJsonArray>
#include <QNetworkCookie>
#include <QStandardPaths>
#include <QWebEngineCookieStore>
#include <QWebEnginePermission>
#include <QWebEngineSettings>
#include <QtEnvironmentVariables>

using namespace Qt::StringLiterals;

namespace whatsie::web {

namespace {
constexpr auto kProfileName = "whatsie";
} // namespace

WebProfile::WebProfile(core::Settings& settings, core::ThemeService& theme, QObject* parent)
    : QWebEngineProfile(QString::fromLatin1(kProfileName), parent)
    , m_settings(settings)
    , m_theme(theme)
    , m_bridge(std::make_unique<Bridge>())
    , m_scripts(std::make_unique<ScriptBundle>(*this))
{
    configureStorage();
    configureUserAgent();
    configureAttributes();
    configureAcceptLanguage();
    installBootstrap();
    configureSpellCheck();

    connect(&m_settings, &core::Settings::smoothScrollingChanged, this, [this](bool enabled) {
        this->settings()->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, enabled);
    });
    connect(&m_settings, &core::Settings::webLanguageChanged, this, [this](const QString&) {
        configureAcceptLanguage();
        installBootstrap();
    });
    // Rebake the config so newly loaded pages start at the current blur level;
    // WebView applies it live to the already-loaded page.
    connect(&m_settings, &core::Settings::messageBlurLevelChanged, this, [this](int) { installBootstrap(); });
    connect(&m_settings, &core::Settings::themeChanged, this, [this](core::Theme) { installBootstrap(); });
    // Keep the seeded effective theme current when the desktop scheme changes in
    // System mode, so a freshly loaded page starts at the right theme (#367, #374).
    connect(&m_theme, &core::ThemeService::effectiveSchemeChanged, this,
            [this](Qt::ColorScheme) { installBootstrap(); });

    // FEATURES P3: proxy is process-wide; set it before the first request and
    // re-apply whenever it changes (a page reload then picks it up).
    applyProxy(m_settings.proxyConfig());
    connect(&m_settings, &core::Settings::proxyChanged, this,
            [this] { applyProxy(m_settings.proxyConfig()); });
    connect(&m_settings, &core::Settings::webrtcPublicInterfacesOnlyChanged, this, [this](bool on) {
        this->settings()->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, on);
    });
    connect(&m_settings, &core::Settings::spellCheckEnabledChanged, this,
            [this](bool on) { setSpellCheckEnabled(on); });
    connect(&m_settings, &core::Settings::spellCheckLanguagesChanged, this,
            [this](const QStringList& langs) { setSpellCheckLanguages(langs); });
    qCInfo(lcWeb) << "profile ready, storage at" << persistentStoragePath();
}

WebProfile::~WebProfile() = default;

void WebProfile::configureStorage()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString storage = base + u"/profile"_s;
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + u"/profile"_s;
    QDir().mkpath(storage);
    QDir().mkpath(cache);

    setPersistentStoragePath(storage);
    setCachePath(cache);
    setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    setPersistentPermissionsPolicy(QWebEngineProfile::PersistentPermissionsPolicy::StoreOnDisk);

    // Pre-grant Notifications so WhatsApp never shows its "notifications are off"
    // banner (W#307); it persists reliably. Camera/microphone do NOT pre-grant
    // reliably before a request, so PermissionController grants those on first
    // request instead (respecting an explicit deny). ADR-022.
    QWebEnginePermission notifications = queryPermission(QUrl(u"https://web.whatsapp.com"_s),
                                                         QWebEnginePermission::PermissionType::Notifications);
    if (notifications.state() == QWebEnginePermission::State::Ask) {
        notifications.grant();
    }
}

QStringList WebProfile::storageRoots() const
{
    return {QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation)};
}

void WebProfile::configureUserAgent()
{
    // The override setting lands with the "custom user agent" feature; until
    // then only the sanitized engine default is used.
    setHttpUserAgent(effectiveUserAgent(httpUserAgent(), QString()));
    qCDebug(lcWeb) << "user agent:" << httpUserAgent();
}

void WebProfile::configureAttributes()
{
    QWebEngineSettings* s = settings();
    s->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);
    s->setAttribute(QWebEngineSettings::JavascriptCanPaste, true);
    s->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    s->setAttribute(QWebEngineSettings::ScreenCaptureEnabled, true);
    s->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    s->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, true);
    s->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    s->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, false);
    // We render our own branded error page on load failure (WebView), so turn
    // off Chromium's stock one — it looks out of place inside Whatsie.
    s->setAttribute(QWebEngineSettings::ErrorPageEnabled, false);
    // FEATURES A14: user option, off by default.
    s->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, m_settings.smoothScrolling());
    // FEATURES P2: keep WebRTC off private interfaces when the user asks.
    s->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, m_settings.webrtcPublicInterfacesOnly());
}

QString WebProfile::themeName(core::Theme theme)
{
    switch (theme) {
    case core::Theme::Light:
        return u"light"_s;
    case core::Theme::Dark:
        return u"dark"_s;
    case core::Theme::System:
        break;
    }
    return u"system"_s;
}

void WebProfile::configureSpellCheck()
{
    // FEATURES L1: resolve each configured language to a dictionary that is
    // actually installed (QTWEBENGINE_DICTIONARIES_PATH, set by Application) —
    // e.g. en-IN, which has no dictionary, falls back to en-GB. Feeding the
    // checker an absent language just silently disables it (owner report: the
    // UI claimed "en-IN" while spell check did nothing).
    const QStringList available =
        core::availableDictionaries(qEnvironmentVariable("QTWEBENGINE_DICTIONARIES_PATH"));
    QStringList effective;
    for (const QString& want : m_settings.spellCheckLanguages()) {
        const QString dict = core::resolveDictionary(want, available);
        if (!dict.isEmpty() && !effective.contains(dict)) {
            effective << dict;
        }
    }
    setSpellCheckEnabled(m_settings.spellCheckEnabled());
    setSpellCheckLanguages(effective);
    qCInfo(lcWeb) << "spell check" << (m_settings.spellCheckEnabled() ? "on" : "off") << "requested"
                  << m_settings.spellCheckLanguages() << "effective" << effective;
}

void WebProfile::configureAcceptLanguage()
{
    const QString code = m_settings.webLanguage().trimmed();
    const QString header = core::acceptLanguageHeader(code);
    setHttpAcceptLanguage(header);
    qCInfo(lcWeb) << "HTTP Accept-Language:" << (header.isEmpty() ? u"<default system>"_s : header);

    auto* store = cookieStore();
    if (store == nullptr) {
        return;
    }

    const QUrl whatsappUrl(u"https://web.whatsapp.com"_s);
    if (!code.isEmpty()) {
        const QString langCode = code.section(u'_', 0, 0).toLower();

        QNetworkCookie c1(QByteArrayLiteral("wa_lang_pref"), langCode.toUtf8());
        c1.setDomain(u".whatsapp.com"_s);
        c1.setPath(u"/"_s);
        c1.setSecure(true);
        store->setCookie(c1, whatsappUrl);

        QNetworkCookie c2(QByteArrayLiteral("wa_web_lang_pref"), code.toUtf8());
        c2.setDomain(u".web.whatsapp.com"_s);
        c2.setPath(u"/"_s);
        c2.setSecure(true);
        store->setCookie(c2, whatsappUrl);
    } else {
        QNetworkCookie c1(QByteArrayLiteral("wa_lang_pref"), QByteArray());
        c1.setDomain(u".whatsapp.com"_s);
        c1.setPath(u"/"_s);
        store->deleteCookie(c1, whatsappUrl);

        QNetworkCookie c2(QByteArrayLiteral("wa_web_lang_pref"), QByteArray());
        c2.setDomain(u".web.whatsapp.com"_s);
        c2.setPath(u"/"_s);
        store->deleteCookie(c2, whatsappUrl);
    }
}

void WebProfile::installBootstrap()
{
    // Config passed to injected scripts as window.__whatsie.config. theme-control.js
    // forces an explicit colorScheme at the page level; "system" lets WhatsApp
    // follow the OS prefers-color-scheme (ADR-020, revised by ADR-026).
    const QString webLang = m_settings.webLanguage().trimmed();
    QJsonObject config{
        {u"blurLevel"_s, m_settings.messageBlurLevel()},
        {u"colorScheme"_s, themeName(m_settings.theme())},
        {u"effectiveTheme"_s, m_theme.isDark() ? u"dark"_s : u"light"_s},
        {u"webLanguage"_s, webLang},
    };
    if (!webLang.isEmpty()) {
        QJsonArray langsArray;
        for (const QString& lang : core::webClientLanguages(webLang)) {
            langsArray.append(lang);
        }
        config[u"webLanguages"_s] = langsArray;
    }
    m_scripts->installBootstrap(config);
}

} // namespace whatsie::web
