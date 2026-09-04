/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "engine.h"

namespace Tuuli {

QString PermissionRequest::kindName() const
{
    return kindName(m_kind);
}

QString PermissionRequest::kindName(PermissionKind kind)
{
    switch (kind) {
    case PermissionKind::Geolocation: return QStringLiteral("geolocation");
    case PermissionKind::Notifications: return QStringLiteral("notifications");
    case PermissionKind::Camera: return QStringLiteral("camera");
    case PermissionKind::Microphone: return QStringLiteral("microphone");
    case PermissionKind::PersistentStorage: return QStringLiteral("persistent-storage");
    case PermissionKind::Midi: return QStringLiteral("midi");
    case PermissionKind::Bluetooth: return QStringLiteral("bluetooth");
    case PermissionKind::ClipboardRead: return QStringLiteral("clipboard-read");
    case PermissionKind::ClipboardWrite: return QStringLiteral("clipboard-write");
    }
    return QStringLiteral("unknown");
}

} // namespace Tuuli
