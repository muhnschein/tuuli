// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
import QtQuick 2.6
import Sailfish.Silica 1.0

ListItem {
    id: delegate

    signal openInNewTabRequested()
    signal editRequested()
    signal removeRequested()

    objectName: "bookmarkDelegate"
    contentHeight: Theme.itemSizeMedium
    menu: ContextMenu {
        MenuItem {
            objectName: "openInNewTabMenu"
            text: qsTr("Open in new tab")
            onClicked: delegate.openInNewTabRequested()
        }
        MenuItem {
            objectName: "editBookmarkMenu"
            text: qsTr("Edit")
            onClicked: delegate.editRequested()
        }
        MenuItem {
            objectName: "removeBookmarkMenu"
            text: qsTr("Remove")
            onClicked: delegate.remorseAction(qsTr("Removing bookmark"), function () {
                delegate.removeRequested()
            })
        }
    }

    Image {
        id: favicon

        objectName: "bookmarkFavicon"
        anchors {
            left: parent.left
            leftMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        width: Theme.iconSizeSmall
        height: width
        fillMode: Image.PreserveAspectFit
        source: model.favicon
    }

    Column {
        anchors {
            left: favicon.right
            leftMargin: Theme.paddingMedium
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }

        Label {
            objectName: "bookmarkTitle"
            width: parent.width
            text: model.title
            truncationMode: TruncationMode.Fade
            color: delegate.highlighted ? Theme.highlightColor : Theme.primaryColor
        }

        Label {
            objectName: "bookmarkUrl"
            width: parent.width
            text: model.url
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeExtraSmall
            color: delegate.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
        }
    }
}
