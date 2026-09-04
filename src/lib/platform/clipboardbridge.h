/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_CLIPBOARDBRIDGE_H
#define TUULI_CLIPBOARDBRIDGE_H

#include <QObject>
#include <QString>

namespace Tuuli {

/* Qt clipboard bridged to the engine (spec 8.3, M2).  GUI thread only. */
class ClipboardBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasText READ hasText NOTIFY changed)
public:
    explicit ClipboardBridge(QObject* parent = nullptr);

    Q_INVOKABLE QString text() const;
    Q_INVOKABLE void setText(const QString& text);
    bool hasText() const;

signals:
    void changed();
};

} // namespace Tuuli

#endif
