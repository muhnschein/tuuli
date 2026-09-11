// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.tuuli 1.0

CoverBackground {
    id: cover

    objectName: "coverPage"

    Column {
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: Theme.paddingLarge
        }
        spacing: Theme.paddingMedium

        Image {
            objectName: "coverFavicon"
            width: Theme.iconSizeLarge
            height: width
            fillMode: Image.PreserveAspectFit
            source: TabModel.activeFavicon
            visible: status === Image.Ready
        }

        Label {
            objectName: "coverTitle"
            width: parent.width
            text: TabModel.activeTitle.length > 0 ? TabModel.activeTitle : TabModel.activeUrl
            wrapMode: Text.Wrap
            maximumLineCount: 3
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.highlightColor
        }

        Label {
            objectName: "coverTabCount"
            width: parent.width
            text: qsTr("%n tab(s)", "", TabModel.count)
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
        }
    }

    CoverActionList {
        CoverAction {
            objectName: "newTabCoverAction"
            iconSource: "image://theme/icon-cover-new"
            onTriggered: {
                TabModel.newTab(Settings.homePage)
                window.activate()
            }
        }
    }
}
