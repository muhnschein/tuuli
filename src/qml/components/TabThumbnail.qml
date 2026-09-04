// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0

// One cell of the tab overview grid: thumbnail, title, close button.
BackgroundItem {
    id: cell

    property alias title: titleLabel.text
    property alias thumbnail: image.source
    property bool isPrivate: false
    property bool active: false
    property bool loading: false

    signal closeRequested()

    Rectangle {
        anchors.fill: parent
        anchors.margins: Theme.paddingSmall
        radius: Theme.paddingSmall
        color: isPrivate ? Theme.rgba(Theme.highlightDimmerColor, 0.9)
                         : Theme.rgba(Theme.highlightBackgroundColor, active ? 0.4 : 0.15)
        border.width: active ? 2 : 0
        border.color: Theme.highlightColor

        Image {
            id: image
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                bottom: footer.top
                margins: Theme.paddingSmall
            }
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            clip: true
            opacity: status === Image.Ready ? 1.0 : 0.0
            Behavior on opacity { FadeAnimation {} }
        }

        Row {
            id: footer
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                margins: Theme.paddingSmall
            }
            height: Theme.itemSizeExtraSmall

            Label {
                id: titleLabel
                width: parent.width - closeButton.width
                anchors.verticalCenter: parent.verticalCenter
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeSmall
                color: cell.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
            IconButton {
                id: closeButton
                width: Theme.itemSizeExtraSmall
                height: parent.height
                icon.source: "image://theme/icon-m-clear"
                icon.width: Theme.iconSizeSmall
                icon.height: Theme.iconSizeSmall
                onClicked: cell.closeRequested()
            }
        }

        BusyIndicator {
            anchors.centerIn: image
            size: BusyIndicatorSize.Medium
            running: loading
        }

        Label {
            anchors.centerIn: image
            visible: isPrivate
            //% "Private"
            text: qsTrId("tuuli-la-private_label")
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryHighlightColor
        }
    }
}
