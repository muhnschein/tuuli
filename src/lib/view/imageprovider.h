/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_IMAGEPROVIDER_H
#define TUULI_IMAGEPROVIDER_H

/*
 * image://tuuli/favicon/<tabId>/<rev> and image://tuuli/thumbnail/<tabId>/<rev>.
 * Qt calls requestImage() from a worker thread, so this keeps its own
 * mutex-guarded copies, fed from the GUI thread by BrowserContext.
 */

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

namespace Tuuli {

class TuuliImageProvider : public QQuickImageProvider
{
public:
    TuuliImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    /* GUI thread. */
    void setFavicon(int tabId, const QImage& image);
    void setThumbnail(int tabId, const QImage& image);
    void removeTab(int tabId);

    static const char* providerId() { return "tuuli"; }

private:
    QMutex m_mutex;
    QHash<int, QImage> m_favicons;
    QHash<int, QImage> m_thumbnails;
};

} // namespace Tuuli

#endif
