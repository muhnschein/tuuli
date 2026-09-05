// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0

// Start page (spec 7.1): recent history, bookmarks grid, search field
// focused on cold start.
SilicaFlickable {
    id: startView

    property bool focusSearchOnShow: false
    signal inputEntered(string input)
    signal urlActivated(url url)

    contentHeight: column.height + Theme.paddingLarge

    onVisibleChanged: if (visible && focusSearchOnShow) searchField.forceActiveFocus()

    Column {
        id: column
        width: parent.width

        PageHeader {
            //% "Tuuli"
            title: qsTrId("tuuli-la-app_name")
            description: Browser.tabs.currentTab && Browser.tabs.currentTab.isPrivate
                         //% "Private tab"
                         ? qsTrId("tuuli-la-private_tab") : ""
        }

        SearchField {
            id: searchField
            width: parent.width
            //% "Search or enter address"
            placeholderText: qsTrId("tuuli-ph-url_field")
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            EnterKey.enabled: text.length > 0
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: {
                startView.inputEntered(text)
                text = ""
                focus = false
            }
        }

        SectionHeader {
            //% "Bookmarks"
            text: qsTrId("tuuli-he-bookmarks")
            visible: bookmarkGrid.count > 0
        }

        Grid {
            id: bookmarkGrid
            property int count: Browser.bookmarks.count
            width: parent.width - 2 * Theme.horizontalPageMargin
            x: Theme.horizontalPageMargin
            columns: Math.max(2, Math.floor(width / (Theme.itemSizeHuge * 1.2)))
            spacing: Theme.paddingMedium
            visible: count > 0

            Repeater {
                model: Browser.bookmarks
                delegate: BackgroundItem {
                    width: (bookmarkGrid.width - (bookmarkGrid.columns - 1) * bookmarkGrid.spacing) / bookmarkGrid.columns
                    height: Theme.itemSizeLarge
                    onClicked: startView.urlActivated(model.url)
                    onPressAndHold: bookmarkMenu.open(this)

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - Theme.paddingMedium
                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeSmall
                            text: model.title
                        }
                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeTiny
                            color: Theme.secondaryColor
                            text: model.url
                        }
                    }
                    ContextMenu {
                        id: bookmarkMenu
                        MenuItem {
                            //% "Open in new tab"
                            text: qsTrId("tuuli-me-open_new_tab")
                            onClicked: Browser.openUrl(model.url, false, true)
                        }
                        MenuItem {
                            //% "Remove"
                            text: qsTrId("tuuli-me-remove")
                            onClicked: Browser.bookmarks.remove(model.url)
                        }
                    }
                }
            }
        }

        SectionHeader {
            //% "Recent"
            text: qsTrId("tuuli-he-recent")
            visible: recentRepeater.count > 0
        }

        Repeater {
            id: recentRepeater
            model: Browser.history
            delegate: ListItem {
                width: startView.width
                contentHeight: Theme.itemSizeSmall
                onClicked: startView.urlActivated(model.url)
                menu: ContextMenu {
                    MenuItem {
                        //% "Open in new tab"
                        text: qsTrId("tuuli-me-open_new_tab")
                        onClicked: Browser.openUrl(model.url, false, true)
                    }
                    MenuItem {
                        //% "Remove from history"
                        text: qsTrId("tuuli-me-remove_from_history")
                        onClicked: Browser.history.remove(model.url)
                    }
                }
                Column {
                    anchors {
                        left: parent.left
                        right: parent.right
                        margins: Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }
                    Label {
                        width: parent.width
                        truncationMode: TruncationMode.Fade
                        text: model.title
                        color: highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                    Label {
                        width: parent.width
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                        text: model.url
                    }
                }
            }
        }

        Item {
            width: parent.width
            height: Theme.paddingLarge
            visible: recentRepeater.count === 0 && bookmarkGrid.count === 0
        }

        Label {
            visible: recentRepeater.count === 0 && bookmarkGrid.count === 0
            width: parent.width - 2 * Theme.horizontalPageMargin
            x: Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            color: Theme.secondaryHighlightColor
            font.pixelSize: Theme.fontSizeSmall
            //% "Tuuli is an experimental second browser. Web content is not sandboxed from the app; see Settings → About."
            text: qsTrId("tuuli-la-start_disclaimer")
        }
    }

    Component.onDestruction: Browser.history.filter = ""
    Component.onCompleted: {
        Browser.history.limit = 12
        if (visible && focusSearchOnShow) searchField.forceActiveFocus()
    }

    VerticalScrollDecorator {}
}
