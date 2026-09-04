/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "preferences.h"
#include "searchengines.h"
#include "servoprefs.h"

#include <QStandardPaths>

namespace Tuuli {

namespace Key {
constexpr const char* SearchEngine = "general/searchEngine";
constexpr const char* HomePage = "general/homePage";
constexpr const char* BlockThirdPartyCookies = "privacy/blockThirdPartyCookies";
constexpr const char* SendDnt = "privacy/sendDoNotTrack";
constexpr const char* SendGpc = "privacy/sendGlobalPrivacyControl";
constexpr const char* ReferrerPolicy = "privacy/referrerPolicy";
constexpr const char* CosmeticFiltering = "privacy/cosmeticFiltering";
constexpr const char* JavascriptEnabled = "engine/javascriptEnabled";
constexpr const char* UserAgentOverride = "engine/userAgentOverride";
constexpr const char* DownloadDirectory = "downloads/directory";
constexpr const char* RestoreSession = "session/restore";
constexpr const char* DprOverride = "developer/devicePixelRatioOverride";
constexpr const char* ShowFrameStats = "developer/showFrameStats";
constexpr const char* BasicRenderLoop = "developer/basicRenderLoop";
constexpr const char* EngineLogging = "developer/engineLogging";
constexpr const char* PerfLogging = "developer/perfLogging";
constexpr const char* MaxLiveWebViews = "developer/maxLiveWebViews";
}

static QString defaultSettingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/tuuli.conf");
}

Preferences::Preferences(const QString& filePath, QObject* parent)
    : QObject(parent)
    , m_settings(filePath.isEmpty() ? defaultSettingsPath() : filePath, QSettings::IniFormat)
{
}

template <typename T>
void Preferences::setValue(const char* key, const T& value, void (Preferences::*signal)())
{
    if (m_settings.value(QLatin1String(key)).template value<T>() == value
        && m_settings.contains(QLatin1String(key)))
        return;
    m_settings.setValue(QLatin1String(key), value);
    emit (this->*signal)();
}

QString Preferences::searchEngine() const
{
    const QString id = m_settings.value(QLatin1String(Key::SearchEngine), SearchEngines::defaultId()).toString();
    return SearchEngines::byId(id) ? id : SearchEngines::defaultId();
}
void Preferences::setSearchEngine(const QString& id) { setValue(Key::SearchEngine, id, &Preferences::searchEngineChanged); }

QString Preferences::homePage() const { return m_settings.value(QLatin1String(Key::HomePage), QString()).toString(); }
void Preferences::setHomePage(const QString& url) { setValue(Key::HomePage, url, &Preferences::homePageChanged); }

bool Preferences::blockThirdPartyCookies() const { return m_settings.value(QLatin1String(Key::BlockThirdPartyCookies), true).toBool(); }
void Preferences::setBlockThirdPartyCookies(bool on) { setValue(Key::BlockThirdPartyCookies, on, &Preferences::privacyChanged); }
bool Preferences::sendDoNotTrack() const { return m_settings.value(QLatin1String(Key::SendDnt), true).toBool(); }
void Preferences::setSendDoNotTrack(bool on) { setValue(Key::SendDnt, on, &Preferences::privacyChanged); }
bool Preferences::sendGlobalPrivacyControl() const { return m_settings.value(QLatin1String(Key::SendGpc), true).toBool(); }
void Preferences::setSendGlobalPrivacyControl(bool on) { setValue(Key::SendGpc, on, &Preferences::privacyChanged); }
QString Preferences::referrerPolicy() const
{
    return m_settings.value(QLatin1String(Key::ReferrerPolicy), QStringLiteral("strict-origin-when-cross-origin")).toString();
}
void Preferences::setReferrerPolicy(const QString& policy) { setValue(Key::ReferrerPolicy, policy, &Preferences::privacyChanged); }
bool Preferences::cosmeticFiltering() const { return m_settings.value(QLatin1String(Key::CosmeticFiltering), true).toBool(); }
void Preferences::setCosmeticFiltering(bool on) { setValue(Key::CosmeticFiltering, on, &Preferences::privacyChanged); }

bool Preferences::javascriptEnabled() const { return m_settings.value(QLatin1String(Key::JavascriptEnabled), true).toBool(); }
void Preferences::setJavascriptEnabled(bool on) { setValue(Key::JavascriptEnabled, on, &Preferences::engineChanged); }
QString Preferences::userAgentOverride() const { return m_settings.value(QLatin1String(Key::UserAgentOverride), QString()).toString(); }
void Preferences::setUserAgentOverride(const QString& ua) { setValue(Key::UserAgentOverride, ua, &Preferences::engineChanged); }

QString Preferences::downloadDirectory() const
{
    return m_settings.value(QLatin1String(Key::DownloadDirectory),
                            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString();
}
void Preferences::setDownloadDirectory(const QString& dir) { setValue(Key::DownloadDirectory, dir, &Preferences::downloadDirectoryChanged); }

bool Preferences::restoreSession() const { return m_settings.value(QLatin1String(Key::RestoreSession), true).toBool(); }
void Preferences::setRestoreSession(bool on) { setValue(Key::RestoreSession, on, &Preferences::sessionChanged); }

qreal Preferences::devicePixelRatioOverride() const { return m_settings.value(QLatin1String(Key::DprOverride), 0.0).toReal(); }
void Preferences::setDevicePixelRatioOverride(qreal dpr) { setValue(Key::DprOverride, dpr, &Preferences::developerChanged); }
bool Preferences::showFrameStats() const { return m_settings.value(QLatin1String(Key::ShowFrameStats), false).toBool(); }
void Preferences::setShowFrameStats(bool on) { setValue(Key::ShowFrameStats, on, &Preferences::developerChanged); }
bool Preferences::basicRenderLoop() const { return m_settings.value(QLatin1String(Key::BasicRenderLoop), false).toBool(); }
void Preferences::setBasicRenderLoop(bool on) { setValue(Key::BasicRenderLoop, on, &Preferences::developerChanged); }
bool Preferences::engineLogging() const { return m_settings.value(QLatin1String(Key::EngineLogging), false).toBool(); }
void Preferences::setEngineLogging(bool on) { setValue(Key::EngineLogging, on, &Preferences::developerChanged); }
bool Preferences::perfLogging() const { return m_settings.value(QLatin1String(Key::PerfLogging), false).toBool(); }
void Preferences::setPerfLogging(bool on) { setValue(Key::PerfLogging, on, &Preferences::developerChanged); }
int Preferences::maxLiveWebViews() const { return m_settings.value(QLatin1String(Key::MaxLiveWebViews), 8).toInt(); }
void Preferences::setMaxLiveWebViews(int n) { setValue(Key::MaxLiveWebViews, qMax(1, n), &Preferences::developerChanged); }

static QString boolPref(const char* name, bool on)
{
    return QString::fromLatin1(name) + QLatin1Char('=') + (on ? QStringLiteral("true") : QStringLiteral("false"));
}

QStringList Preferences::enginePrefs() const
{
    QStringList prefs;
    prefs << boolPref(ServoPref::NetworkBlockThirdPartyCookies, blockThirdPartyCookies());
    prefs << boolPref(ServoPref::NetworkSendDnt, sendDoNotTrack());
    prefs << boolPref(ServoPref::NetworkSendGpc, sendGlobalPrivacyControl());
    prefs << QString::fromLatin1(ServoPref::NetworkReferrerPolicy) + QLatin1Char('=') + referrerPolicy();
    prefs << boolPref(ServoPref::JsEnabled, javascriptEnabled());
    prefs << boolPref(ServoPref::DomTouchEnabled, true);
    prefs << boolPref(ServoPref::MediaGlVideo, true);
    return prefs;
}

void Preferences::sync()
{
    m_settings.sync();
}

} // namespace Tuuli
