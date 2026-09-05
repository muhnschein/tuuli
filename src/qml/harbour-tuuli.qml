// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Sailfish.Share 1.0
import Tuuli 1.0
import "components"
import "pages"

ApplicationWindow {
    id: app

    // Portrait-first (spec 7); landscape is basic orientation support only.
    allowedOrientations: Orientation.Portrait | Orientation.Landscape | Orientation.LandscapeInverted
    _defaultPageOrientations: Orientation.Portrait

    initialPage: Component { BrowserPage { id: browserPage } }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")

    function showNotice(text) {
        notice.show(text)
    }

    // Share via the system share UI (spec 8.3, M2).  Reached from pages as
    // Browser.share(): the C++ singleton relays to this handler so page
    // files need no cross-file ids.
    ShareAction {
        id: shareAction
    }

    function share(url, title) {
        shareAction.resources = [{ "type": "text/x-url", "status": url.toString(), "linkTitle": title }]
        shareAction.mimeType = "text/x-url"
        shareAction.trigger()
    }

    Notice {
        id: notice
        anchors {
            horizontalCenter: parent.horizontalCenter
            top: parent.top
            topMargin: Theme.paddingLarge * 2
        }
    }

    Connections {
        target: Browser

        // Spec 8.3: every permission prompt is a Silica dialog, denied by default.
        onPermissionPrompt: {
            var dialog = pageStack.push(Qt.resolvedUrl("components/PermissionDialog.qml"),
                                        { request: request, isPrivate: isPrivate })
        }
        onDialogPrompt: {
            pageStack.push(Qt.resolvedUrl("components/SimpleDialog.qml"), { request: request })
        }
        onNotificationRequested: showNotice(title.length ? title + " — " + body : body)
        onShareRequested: share(url, title)
        onDownloadStarted: {
            //% "Downloading %1"
            showNotice(qsTrId("tuuli-la-downloading").arg(fileName))
        }
        onEngineCrashed: {
            //% "The web engine stopped: %1. Your tabs were saved."
            showNotice(qsTrId("tuuli-la-engine_crashed").arg(reason))
        }
    }

    Component.onCompleted: {
        if (Browser.restoredAfterCrash) {
            //% "Session restored after an unexpected exit"
            showNotice(qsTrId("tuuli-la-restored_after_crash"))
        }
    }
}
