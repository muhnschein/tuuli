// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0

// Spec 8.3: Silica dialog, denied by default, per-origin persistence.
Dialog {
    id: dialog

    property PermissionRequest request
    property bool isPrivate: false
    property bool answered: false

    canAccept: request !== null

    function kindLabel(name) {
        switch (name) {
        //% "your location"
        case "geolocation": return qsTrId("tuuli-la-perm_geolocation")
        //% "show notifications"
        case "notifications": return qsTrId("tuuli-la-perm_notifications")
        //% "use the camera"
        case "camera": return qsTrId("tuuli-la-perm_camera")
        //% "use the microphone"
        case "microphone": return qsTrId("tuuli-la-perm_microphone")
        //% "read the clipboard"
        case "clipboard-read": return qsTrId("tuuli-la-perm_clipboard_read")
        default: return name
        }
    }

    onAccepted: {
        answered = true
        if (remember.checked) Browser.rememberPermission(request.origin, request.kind, true, isPrivate)
        request.allow()
    }
    onRejected: {
        answered = true
        if (remember.checked) Browser.rememberPermission(request.origin, request.kind, false, isPrivate)
        request.deny()
    }
    // A dismissed prompt still answers: denied.
    Component.onDestruction: if (!answered && request) request.deny()

    Column {
        width: parent.width
        spacing: Theme.paddingLarge

        DialogHeader {
            //% "Allow"
            acceptText: qsTrId("tuuli-he-allow")
            //% "Deny"
            cancelText: qsTrId("tuuli-he-deny")
        }

        Label {
            width: parent.width - 2 * Theme.horizontalPageMargin
            x: Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            color: Theme.highlightColor
            //% "%1 wants to %2."
            text: request ? qsTrId("tuuli-la-permission_question").arg(request.origin).arg(kindLabel(request.kindName)) : ""
        }

        TextSwitch {
            id: remember
            enabled: !isPrivate
            //% "Remember for this site"
            text: qsTrId("tuuli-la-remember_permission")
            //% "Not available in private tabs"
            description: isPrivate ? qsTrId("tuuli-la-remember_permission_private") : ""
        }
    }
}
