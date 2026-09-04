/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "clipboardbridge.h"

#include <QClipboard>
#include <QGuiApplication>

namespace Tuuli {

ClipboardBridge::ClipboardBridge(QObject* parent)
    : QObject(parent)
{
    if (QClipboard* cb = QGuiApplication::clipboard())
        connect(cb, &QClipboard::dataChanged, this, &ClipboardBridge::changed);
}

QString ClipboardBridge::text() const
{
    QClipboard* cb = QGuiApplication::clipboard();
    return cb ? cb->text() : QString();
}

void ClipboardBridge::setText(const QString& text)
{
    if (QClipboard* cb = QGuiApplication::clipboard())
        cb->setText(text);
}

bool ClipboardBridge::hasText() const
{
    return !text().isEmpty();
}

} // namespace Tuuli
