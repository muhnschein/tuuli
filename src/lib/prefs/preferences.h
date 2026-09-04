/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_PREFERENCES_H
#define TUULI_PREFERENCES_H

/*
 * User-facing settings (spec 7.1 Settings view, 9.4 defaults).  Backed by
 * QSettings so the same code runs on host and device; the file lives in the
 * sailjail-permitted config dir.
 */

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

namespace Tuuli {

class Preferences : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString searchEngine READ searchEngine WRITE setSearchEngine NOTIFY searchEngineChanged)
    Q_PROPERTY(QString homePage READ homePage WRITE setHomePage NOTIFY homePageChanged)
    Q_PROPERTY(bool blockThirdPartyCookies READ blockThirdPartyCookies WRITE setBlockThirdPartyCookies NOTIFY privacyChanged)
    Q_PROPERTY(bool sendDoNotTrack READ sendDoNotTrack WRITE setSendDoNotTrack NOTIFY privacyChanged)
    Q_PROPERTY(bool sendGlobalPrivacyControl READ sendGlobalPrivacyControl WRITE setSendGlobalPrivacyControl NOTIFY privacyChanged)
    Q_PROPERTY(QString referrerPolicy READ referrerPolicy WRITE setReferrerPolicy NOTIFY privacyChanged)
    Q_PROPERTY(bool cosmeticFiltering READ cosmeticFiltering WRITE setCosmeticFiltering NOTIFY privacyChanged)
    Q_PROPERTY(bool javascriptEnabled READ javascriptEnabled WRITE setJavascriptEnabled NOTIFY engineChanged)
    Q_PROPERTY(QString downloadDirectory READ downloadDirectory WRITE setDownloadDirectory NOTIFY downloadDirectoryChanged)
    Q_PROPERTY(QString userAgentOverride READ userAgentOverride WRITE setUserAgentOverride NOTIFY engineChanged)
    Q_PROPERTY(bool restoreSession READ restoreSession WRITE setRestoreSession NOTIFY sessionChanged)
    Q_PROPERTY(qreal devicePixelRatioOverride READ devicePixelRatioOverride WRITE setDevicePixelRatioOverride NOTIFY developerChanged)
    Q_PROPERTY(bool showFrameStats READ showFrameStats WRITE setShowFrameStats NOTIFY developerChanged)
    Q_PROPERTY(bool basicRenderLoop READ basicRenderLoop WRITE setBasicRenderLoop NOTIFY developerChanged)
    Q_PROPERTY(bool engineLogging READ engineLogging WRITE setEngineLogging NOTIFY developerChanged)
    Q_PROPERTY(bool perfLogging READ perfLogging WRITE setPerfLogging NOTIFY developerChanged)
    Q_PROPERTY(int maxLiveWebViews READ maxLiveWebViews WRITE setMaxLiveWebViews NOTIFY developerChanged)

public:
    explicit Preferences(const QString& filePath = QString(), QObject* parent = nullptr);

    QString searchEngine() const;
    void setSearchEngine(const QString& id);
    QString homePage() const;
    void setHomePage(const QString& url);

    bool blockThirdPartyCookies() const;
    void setBlockThirdPartyCookies(bool on);
    bool sendDoNotTrack() const;
    void setSendDoNotTrack(bool on);
    bool sendGlobalPrivacyControl() const;
    void setSendGlobalPrivacyControl(bool on);
    QString referrerPolicy() const;
    void setReferrerPolicy(const QString& policy);
    bool cosmeticFiltering() const;
    void setCosmeticFiltering(bool on);

    bool javascriptEnabled() const;
    void setJavascriptEnabled(bool on);
    QString userAgentOverride() const;
    void setUserAgentOverride(const QString& ua);

    QString downloadDirectory() const;
    void setDownloadDirectory(const QString& dir);

    bool restoreSession() const;
    void setRestoreSession(bool on);

    qreal devicePixelRatioOverride() const;
    void setDevicePixelRatioOverride(qreal dpr);
    bool showFrameStats() const;
    void setShowFrameStats(bool on);
    bool basicRenderLoop() const;
    void setBasicRenderLoop(bool on);
    bool engineLogging() const;
    void setEngineLogging(bool on);
    bool perfLogging() const;
    void setPerfLogging(bool on);
    int maxLiveWebViews() const;
    void setMaxLiveWebViews(int n);

    /* Engine preference lines ("name=value") derived from the privacy and
     * engine settings; see servoprefs.h for the name table. */
    QStringList enginePrefs() const;

    Q_INVOKABLE void sync();

signals:
    void searchEngineChanged();
    void homePageChanged();
    void privacyChanged();
    void engineChanged();
    void downloadDirectoryChanged();
    void sessionChanged();
    void developerChanged();

private:
    template <typename T> void setValue(const char* key, const T& value, void (Preferences::*signal)());
    mutable QSettings m_settings;
};

} // namespace Tuuli

#endif
