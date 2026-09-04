// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0

Page {
    id: page

    Component.onCompleted: { Browser.history.limit = 200; Browser.history.filter = "" }
    Component.onDestruction: { Browser.history.limit = 12; Browser.history.filter = "" }

    SilicaListView {
        anchors.fill: parent
        model: Browser.history
        currentIndex: -1

        header: Column {
            width: parent.width
            PageHeader {
                //% "History"
                title: qsTrId("tuuli-he-history")
            }
            SearchField {
                width: parent.width
                //% "Search history"
                placeholderText: qsTrId("tuuli-ph-search_history")
                onTextChanged: Browser.history.filter = text
            }
        }

        PullDownMenu {
            MenuItem {
                //% "Clear history"
                text: qsTrId("tuuli-me-clear_history")
                enabled: Browser.history.count > 0
                onClicked: remorse.execute(qsTrId("tuuli-me-clear_history"), function() { Browser.history.clear() })
            }
        }

        ViewPlaceholder {
            enabled: Browser.history.count === 0
            //% "No history"
            text: qsTrId("tuuli-la-no_history")
        }

        delegate: ListItem {
            width: ListView.view.width
            contentHeight: Theme.itemSizeSmall
            onClicked: {
                Browser.openUrl(model.url, false, false)
                pageStack.pop()
            }
            menu: ContextMenu {
                MenuItem {
                    //% "Open in new tab"
                    text: qsTrId("tuuli-me-open_new_tab")
                    onClicked: { Browser.openUrl(model.url, false, true); pageStack.pop() }
                }
                MenuItem {
                    //% "Remove"
                    text: qsTrId("tuuli-me-remove")
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
        VerticalScrollDecorator {}
    }

    RemorsePopup { id: remorse }
}
