/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "imageprovider.h"

#include <QMutexLocker>
#include <QStringList>

namespace Tuuli {

TuuliImageProvider::TuuliImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage TuuliImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    QImage img;
    if (parts.size() >= 2) {
        const int tabId = parts.at(1).toInt();
        QMutexLocker lock(&m_mutex);
        if (parts.at(0) == QLatin1String("favicon"))
            img = m_favicons.value(tabId);
        else if (parts.at(0) == QLatin1String("thumbnail"))
            img = m_thumbnails.value(tabId);
    }
    if (img.isNull()) {
        img = QImage(1, 1, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
    }
    if (requestedSize.isValid() && !requestedSize.isEmpty())
        img = img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = img.size();
    return img;
}

void TuuliImageProvider::setFavicon(int tabId, const QImage& image)
{
    QMutexLocker lock(&m_mutex);
    if (image.isNull())
        m_favicons.remove(tabId);
    else
        m_favicons.insert(tabId, image);
}

void TuuliImageProvider::setThumbnail(int tabId, const QImage& image)
{
    QMutexLocker lock(&m_mutex);
    if (image.isNull())
        m_thumbnails.remove(tabId);
    else
        m_thumbnails.insert(tabId, image);
}

void TuuliImageProvider::removeTab(int tabId)
{
    QMutexLocker lock(&m_mutex);
    m_favicons.remove(tabId);
    m_thumbnails.remove(tabId);
}

} // namespace Tuuli
