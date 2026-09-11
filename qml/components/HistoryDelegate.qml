// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
import QtQuick 2.6
import Sailfish.Silica 1.0

ListItem {
    id: delegate

    signal openInNewTabRequested()
    signal removeRequested()

    objectName: "historyDelegate"
    contentHeight: Theme.itemSizeMedium
    menu: ContextMenu {
        MenuItem {
            objectName: "openInNewTabMenu"
            text: qsTr("Open in new tab")
            onClicked: delegate.openInNewTabRequested()
        }
        MenuItem {
            objectName: "removeHistoryMenu"
            text: qsTr("Remove")
            onClicked: delegate.removeRequested()
        }
    }

    Column {
        anchors {
            left: parent.left
            right: dateLabel.left
            margins: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }

        Label {
            objectName: "historyTitle"
            width: parent.width
            text: model.title
            truncationMode: TruncationMode.Fade
            color: delegate.highlighted ? Theme.highlightColor : Theme.primaryColor
        }

        Label {
            objectName: "historyUrl"
            width: parent.width
            text: model.url
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeExtraSmall
            color: delegate.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
        }
    }

    Label {
        id: dateLabel

        objectName: "historyDate"
        anchors {
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        text: Qt.formatDate(model.date, Qt.DefaultLocaleShortDate)
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.secondaryColor
    }
}
