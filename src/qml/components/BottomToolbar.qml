// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0

// Auto-hiding bottom toolbar: URL, tab count, back, overflow (spec 7.1).
DockedPanel {
    id: toolbar

    property Tab tab
    property bool editing: urlField.activeFocus

    signal urlEntered(string input)
    signal tabsRequested()
    signal overflowRequested()

    function edit() {
        open = true
        urlField.text = tab && tab.url.toString().length ? tab.url : ""
        urlField.forceActiveFocus()
        urlField.selectAll()
    }

    dock: Dock.Bottom
    width: parent.width
    height: column.height + Theme.paddingMedium
    open: true
    modal: false

    Rectangle {
        anchors.fill: parent
        color: Theme.rgba(Theme.highlightBackgroundColor, Theme.highlightBackgroundOpacity)
    }

    Column {
        id: column
        width: parent.width
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.paddingSmall

        Rectangle {
            // Load indicator: thin highlight bar while the page is loading.
            width: tab && tab.loading ? parent.width : 0
            height: Theme.paddingSmall / 2
            color: Theme.highlightColor
            Behavior on width { NumberAnimation { duration: 400 } }
        }

        Row {
            width: parent.width
            height: Theme.itemSizeSmall

            IconButton {
                id: backButton
                width: Theme.itemSizeSmall
                height: parent.height
                icon.source: "image://theme/icon-m-back"
                enabled: tab && tab.canGoBack
                onClicked: tab.goBack()
            }

            TextField {
                id: urlField
                width: parent.width - backButton.width - tabsButton.width - overflowButton.width
                anchors.verticalCenter: parent.verticalCenter
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                //% "Search or enter address"
                placeholderText: qsTrId("tuuli-ph-url_field")
                label: tab && tab.isPrivate
                       //% "Private"
                       ? qsTrId("tuuli-la-private_label")
                       : (tab && tab.url.toString().length ? tab.url : "")
                textLeftMargin: Theme.paddingMedium
                textRightMargin: Theme.paddingMedium
                EnterKey.enabled: text.length > 0
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: {
                    toolbar.urlEntered(text)
                    focus = false
                }
                onActiveFocusChanged: if (activeFocus) selectAll()

                // The tab's title while idle; the user's text while editing.
                // A Binding rather than `text: activeFocus ? text : ...`,
                // which binds text to itself and loops.
                Binding {
                    target: urlField
                    property: "text"
                    value: tab ? tab.displayTitle : ""
                    when: !urlField.activeFocus
                }
            }

            IconButton {
                id: tabsButton
                width: Theme.itemSizeSmall
                height: parent.height
                icon.source: "image://theme/icon-m-tabs"
                onClicked: toolbar.tabsRequested()

                Label {
                    anchors.centerIn: parent
                    text: Browser.tabs.count
                    font.pixelSize: Theme.fontSizeTiny
                    font.bold: true
                    color: tabsButton.down ? Theme.highlightColor : Theme.primaryColor
                }
            }

            IconButton {
                id: overflowButton
                width: Theme.itemSizeSmall
                height: parent.height
                icon.source: "image://theme/icon-m-menu"
                onClicked: toolbar.overflowRequested()
            }
        }
    }
}
