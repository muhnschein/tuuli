// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "EngineMessages.h"

#include <QUrl>

namespace Tuuli {

namespace {

bool isWebScheme(const QString &scheme)
{
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

} // namespace

EngineMessages::EngineMessages(QObject *parent)
    : QObject(parent)
{
}

QString EngineMessages::clearPrivateDataTopic() const
{
    return QStringLiteral("clear-private-data");
}

QString EngineMessages::cookiesAndSiteDataPayload() const
{
    return QStringLiteral("cookies-and-site-data");
}

QString EngineMessages::cachePayload() const
{
    return QStringLiteral("cache");
}

QString EngineMessages::faviconScript() const
{
    return QStringLiteral("(function () {"
                          " var link = document.querySelector('link[rel~=\"icon\"]');"
                          " return link && link.href ? String(link.href) : '';"
                          " })()");
}

QString EngineMessages::defaultFavicon(const QString &pageUrl) const
{
    const QUrl page(pageUrl, QUrl::TolerantMode);
    if (!page.isValid() || !isWebScheme(page.scheme()) || page.host().isEmpty()) {
        return {};
    }
    QUrl icon;
    icon.setScheme(page.scheme());
    icon.setHost(page.host());
    icon.setPort(page.port());
    icon.setPath(QStringLiteral("/favicon.ico"));
    return icon.toString();
}

QString EngineMessages::resolveFavicon(const QString &pageUrl, const QString &href) const
{
    const QUrl page(pageUrl, QUrl::TolerantMode);
    const QUrl candidate = page.resolved(QUrl(href.trimmed(), QUrl::TolerantMode));
    if (!href.trimmed().isEmpty() && candidate.isValid() &&
        (isWebScheme(candidate.scheme()) || candidate.scheme() == QLatin1String("data"))) {
        return candidate.toString();
    }
    return defaultFavicon(pageUrl);
}

} // namespace Tuuli
