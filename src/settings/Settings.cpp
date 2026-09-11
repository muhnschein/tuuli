// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "Settings.h"

#include <QHostAddress>
#include <QRegularExpression>
#include <QUrl>
#include <QVector>

namespace Tuuli {

namespace {

const char *const HomePageKey = "homePage";
const char *const SearchEngineKey = "searchEngine";
const char *const DesktopModeKey = "desktopMode";

struct SearchEngine
{
    const char *key;
    const char *name;
    const char *urlTemplate;
};

const QVector<SearchEngine> &searchEngines()
{
    static const QVector<SearchEngine> engines{
        {"duckduckgo", "DuckDuckGo", "https://duckduckgo.com/?q=%1"},
        {"google", "Google", "https://www.google.com/search?q=%1"},
        {"bing", "Bing", "https://www.bing.com/search?q=%1"},
        {"startpage", "Startpage", "https://www.startpage.com/do/search?q=%1"},
        {"wikipedia", "Wikipedia", "https://en.wikipedia.org/w/index.php?search=%1"},
    };
    return engines;
}

int engineIndex(const QString &key)
{
    const QVector<SearchEngine> &engines = searchEngines();
    for (int i = 0; i < engines.count(); ++i) {
        if (QLatin1String(engines.at(i).key) == key) {
            return i;
        }
    }
    return -1;
}

bool isNavigableScheme(const QString &scheme)
{
    return scheme == QLatin1String("http") || scheme == QLatin1String("https") ||
           scheme == QLatin1String("about") || scheme == QLatin1String("file") ||
           scheme == QLatin1String("data");
}

bool isLocalHost(const QString &host)
{
    return host == QLatin1String("localhost") || !QHostAddress(host).isNull();
}

} // namespace

Settings::Settings(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_settings(filePath, QSettings::IniFormat)
{
}

QString Settings::defaultHomePage()
{
    return QStringLiteral("https://duckduckgo.com/");
}

QString Settings::defaultSearchEngine()
{
    return QLatin1String(searchEngines().first().key);
}

QString Settings::homePage() const
{
    return m_settings.value(QLatin1String(HomePageKey), defaultHomePage()).toString();
}

void Settings::setHomePage(const QString &url)
{
    const QString value = url.trimmed().isEmpty() ? defaultHomePage() : url.trimmed();
    if (value == homePage()) {
        return;
    }
    m_settings.setValue(QLatin1String(HomePageKey), value);
    emit homePageChanged();
}

QString Settings::searchEngine() const
{
    const QString key = m_settings.value(QLatin1String(SearchEngineKey)).toString();
    return engineIndex(key) >= 0 ? key : defaultSearchEngine();
}

void Settings::setSearchEngine(const QString &key)
{
    if (engineIndex(key) < 0 || key == searchEngine()) {
        return;
    }
    m_settings.setValue(QLatin1String(SearchEngineKey), key);
    emit searchEngineChanged();
}

int Settings::searchEngineIndex() const
{
    return engineIndex(searchEngine());
}

void Settings::setSearchEngineIndex(int index)
{
    if (index < 0 || index >= searchEngines().count()) {
        return;
    }
    setSearchEngine(QLatin1String(searchEngines().at(index).key));
}

QStringList Settings::searchEngineNames() const
{
    QStringList names;
    for (const SearchEngine &engine : searchEngines()) {
        names.append(QLatin1String(engine.name));
    }
    return names;
}

QStringList Settings::searchEngineKeys() const
{
    QStringList keys;
    for (const SearchEngine &engine : searchEngines()) {
        keys.append(QLatin1String(engine.key));
    }
    return keys;
}

bool Settings::desktopMode() const
{
    return m_settings.value(QLatin1String(DesktopModeKey), false).toBool();
}

void Settings::setDesktopMode(bool desktopMode)
{
    if (desktopMode == this->desktopMode()) {
        return;
    }
    m_settings.setValue(QLatin1String(DesktopModeKey), desktopMode);
    emit desktopModeChanged();
}

QString Settings::searchUrl(const QString &query) const
{
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(query.trimmed()));
    return QString::fromLatin1(searchEngines().at(searchEngineIndex()).urlTemplate).arg(encoded);
}

QString Settings::urlForInput(const QString &input) const
{
    const QString text = input.trimmed();
    if (text.isEmpty()) {
        return {};
    }

    const QUrl typed(text, QUrl::TolerantMode);
    if (typed.isValid() && isNavigableScheme(typed.scheme())) {
        return typed.toString();
    }

    // "host", "host/path", "host:port" without a scheme; a space means a search.
    static const QRegularExpression hostLike(
        QStringLiteral("^[^\\s/?#:]+(:[0-9]{1,5})?(/[^\\s]*)?$"));
    if (hostLike.match(text).hasMatch()) {
        const QString host = text.section(QLatin1Char('/'), 0, 0).section(QLatin1Char(':'), 0, 0);
        const bool looksLikeHost = host.contains(QLatin1Char('.')) || isLocalHost(host);
        if (looksLikeHost) {
            const QString scheme =
                isLocalHost(host) ? QStringLiteral("http://") : QStringLiteral("https://");
            return QUrl(scheme + text, QUrl::TolerantMode).toString();
        }
    }

    return searchUrl(text);
}

} // namespace Tuuli
