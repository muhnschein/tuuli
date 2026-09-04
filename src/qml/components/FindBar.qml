// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0

// Find in page (spec 7.2, bottom pulley).
Rectangle {
    id: findBar

    property Tab tab
    property bool active: false

    function show() {
        active = true
        field.forceActiveFocus()
    }
    function hide() {
        active = false
        field.text = ""
        if (tab) tab.clearFind()
        field.focus = false
    }

    width: parent.width
    height: active ? Theme.itemSizeMedium : 0
    visible: height > 0
    clip: true
    color: Theme.rgba(Theme.highlightBackgroundColor, Theme.highlightBackgroundOpacity)
    Behavior on height { NumberAnimation { duration: 150 } }

    Row {
        anchors.fill: parent

        SearchField {
            id: field
            width: parent.width - 3 * Theme.itemSizeSmall
            anchors.verticalCenter: parent.verticalCenter
            //% "Find in page"
            placeholderText: qsTrId("tuuli-ph-find_in_page")
            EnterKey.iconSource: "image://theme/icon-m-enter-next"
            EnterKey.onClicked: if (tab) tab.findNext()
            onTextChanged: if (tab) tab.findInPage(text, false)
        }
        IconButton {
            width: Theme.itemSizeSmall
            height: parent.height
            icon.source: "image://theme/icon-m-up"
            onClicked: if (tab) tab.findPrevious()
        }
        IconButton {
            width: Theme.itemSizeSmall
            height: parent.height
            icon.source: "image://theme/icon-m-down"
            onClicked: if (tab) tab.findNext()
        }
        IconButton {
            width: Theme.itemSizeSmall
            height: parent.height
            icon.source: "image://theme/icon-m-clear"
            onClicked: findBar.hide()
        }
    }
}
