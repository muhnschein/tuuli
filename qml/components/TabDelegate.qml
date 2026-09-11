// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
import QtQuick 2.6
import Sailfish.Silica 1.0

ListItem {
    id: delegate

    signal closeRequested()

    objectName: "tabDelegate"
    contentHeight: Theme.itemSizeMedium
    highlighted: down || model.activeTab
    menu: ContextMenu {
        MenuItem {
            objectName: "closeTabMenu"
            text: qsTr("Close")
            onClicked: delegate.closeRequested()
        }
    }

    Image {
        id: favicon

        objectName: "tabFavicon"
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
            right: closeButton.left
            rightMargin: Theme.paddingMedium
            verticalCenter: parent.verticalCenter
        }

        Label {
            objectName: "tabTitle"
            width: parent.width
            text: model.title.length > 0 ? model.title : model.url
            truncationMode: TruncationMode.Fade
            color: delegate.highlighted ? Theme.highlightColor : Theme.primaryColor
        }

        Label {
            objectName: "tabUrl"
            width: parent.width
            text: model.privateTab ? qsTr("Private tab") : model.url
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeExtraSmall
            color: delegate.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
        }
    }

    IconButton {
        id: closeButton

        objectName: "closeTabButton"
        anchors {
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        icon.source: "image://theme/icon-m-clear"
        onClicked: delegate.closeRequested()
    }
}
