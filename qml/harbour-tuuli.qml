// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
import QtQuick 2.6
import Sailfish.Silica 1.0
import "pages"

ApplicationWindow {
    id: window

    objectName: "applicationWindow"
    allowedOrientations: Orientation.Portrait
    initialPage: Component {
        BrowserPage {}
    }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
}
