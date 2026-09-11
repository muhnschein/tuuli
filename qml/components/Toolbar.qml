// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
import QtQuick 2.6
import Sailfish.Silica 1.0

Item {
    id: toolbar

    property bool canGoBack: false
    property bool canGoForward: false
    property bool loading: false
    property int tabCount: 0

    signal back()
    signal forward()
    signal reload()
    signal stop()
    signal showTabs()
    signal showMenu()

    height: Theme.itemSizeSmall

    Row {
        anchors.fill: parent

        IconButton {
            objectName: "backButton"
            width: toolbar.width / 5
            height: parent.height
            icon.source: "image://theme/icon-m-back"
            enabled: toolbar.canGoBack
            onClicked: toolbar.back()
        }

        IconButton {
            objectName: "forwardButton"
            width: toolbar.width / 5
            height: parent.height
            icon.source: "image://theme/icon-m-forward"
            enabled: toolbar.canGoForward
            onClicked: toolbar.forward()
        }

        IconButton {
            objectName: "reloadButton"
            width: toolbar.width / 5
            height: parent.height
            icon.source: toolbar.loading ? "image://theme/icon-m-clear"
                                         : "image://theme/icon-m-refresh"
            onClicked: {
                if (toolbar.loading) {
                    toolbar.stop()
                } else {
                    toolbar.reload()
                }
            }
        }

        IconButton {
            objectName: "tabsButton"
            width: toolbar.width / 5
            height: parent.height
            icon.source: "image://theme/icon-m-tabs"
            onClicked: toolbar.showTabs()

            Label {
                objectName: "tabCountLabel"
                anchors.centerIn: parent
                text: toolbar.tabCount
                font.pixelSize: Theme.fontSizeTiny
                font.bold: true
                color: Theme.primaryColor
            }
        }

        IconButton {
            objectName: "menuButton"
            width: toolbar.width / 5
            height: parent.height
            icon.source: "image://theme/icon-m-menu"
            onClicked: toolbar.showMenu()
        }
    }
}
