// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

namespace Tuuli {

// User preferences, stored in the Sailjail-approved config location. Also owns the
// address-bar heuristics because "what does typed text mean" depends on the search engine.
class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString homePage READ homePage WRITE setHomePage NOTIFY homePageChanged)
    Q_PROPERTY(
        QString searchEngine READ searchEngine WRITE setSearchEngine NOTIFY searchEngineChanged)
    Q_PROPERTY(int searchEngineIndex READ searchEngineIndex WRITE setSearchEngineIndex NOTIFY
                   searchEngineChanged)
    Q_PROPERTY(QStringList searchEngineNames READ searchEngineNames CONSTANT)
    Q_PROPERTY(bool desktopMode READ desktopMode WRITE setDesktopMode NOTIFY desktopModeChanged)

public:
    explicit Settings(const QString &filePath, QObject *parent = nullptr);

    QString homePage() const;
    void setHomePage(const QString &url);

    QString searchEngine() const;
    void setSearchEngine(const QString &key);
    int searchEngineIndex() const;
    void setSearchEngineIndex(int index);
    QStringList searchEngineNames() const;
    QStringList searchEngineKeys() const;

    bool desktopMode() const;
    void setDesktopMode(bool desktopMode);

    Q_INVOKABLE QString searchUrl(const QString &query) const;
    // Typed address-bar text: a URL as-is, a host with a scheme added, or a search.
    Q_INVOKABLE QString urlForInput(const QString &input) const;

    static QString defaultHomePage();
    static QString defaultSearchEngine();

signals:
    void homePageChanged();
    void searchEngineChanged();
    void desktopModeChanged();

private:
    QSettings m_settings;
};

} // namespace Tuuli
