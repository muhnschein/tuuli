// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0

// Spec 7.1: page title + favicon; cover actions: new tab, reload.
CoverBackground {
    id: cover

    property Tab tab: Browser.tabs.currentTab

    Column {
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: Theme.paddingLarge
        }
        spacing: Theme.paddingMedium

        Image {
            width: Theme.iconSizeLarge
            height: width
            sourceSize: Qt.size(width, height)
            source: tab && tab.hasFavicon ? tab.faviconSource : "image://theme/icon-launcher-tuuli-browser"
            asynchronous: true
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            maximumLineCount: 3
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.primaryColor
            //% "Tuuli"
            text: tab ? (tab.displayTitle.length ? tab.displayTitle : qsTrId("tuuli-la-app_name")) : qsTrId("tuuli-la-app_name")
        }

        Label {
            width: parent.width
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            visible: tab && tab.url.toString().length > 0
            text: tab ? tab.url : ""
        }

        Label {
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryHighlightColor
            //% "%n tabs"
            text: qsTrId("tuuli-la-cover_tab_count", Browser.tabs.count)
        }
    }

    Image {
        anchors {
            bottom: parent.bottom
            right: parent.right
            margins: Theme.paddingMedium
        }
        width: parent.width * 0.6
        fillMode: Image.PreserveAspectFit
        opacity: 0.25
        source: tab && tab.hasThumbnail ? tab.thumbnailSource : ""
        asynchronous: true
        visible: source.toString().length > 0
    }

    CoverActionList {
        CoverAction {
            iconSource: "image://theme/icon-cover-new"
            onTriggered: {
                Browser.tabs.newTab("", false, true)
                app.activate()
            }
        }
        CoverAction {
            iconSource: "image://theme/icon-cover-refresh"
            onTriggered: if (tab) tab.reload()
        }
    }
}
