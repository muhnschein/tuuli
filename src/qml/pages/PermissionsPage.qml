// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0

// Stored per-origin permission decisions (spec 8.3).
Page {
    id: page

    property var entries: Browser.permissions.entries()

    Connections {
        target: Browser.permissions
        onChanged: page.entries = Browser.permissions.entries()
    }

    SilicaListView {
        anchors.fill: parent
        model: entries

        header: PageHeader {
            //% "Site permissions"
            title: qsTrId("tuuli-la-site_permissions")
        }

        PullDownMenu {
            MenuItem {
                //% "Forget all"
                text: qsTrId("tuuli-me-forget_all")
                enabled: entries.length > 0
                onClicked: Browser.permissions.clearAll()
            }
        }

        ViewPlaceholder {
            enabled: entries.length === 0
            //% "No stored decisions. Everything is denied unless you allow it when asked."
            text: qsTrId("tuuli-la-no_permissions")
        }

        delegate: ListItem {
            width: ListView.view.width
            contentHeight: Theme.itemSizeSmall
            menu: ContextMenu {
                MenuItem {
                    //% "Forget"
                    text: qsTrId("tuuli-me-forget")
                    onClicked: Browser.permissions.setDecision(modelData.origin, modelData.kind, 0)
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
                    text: modelData.origin
                }
                Label {
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: modelData.kindName + ": " + (modelData.decision === 1
                          //% "allowed"
                          ? qsTrId("tuuli-la-allowed")
                          //% "denied"
                          : qsTrId("tuuli-la-denied"))
                }
            }
        }
        VerticalScrollDecorator {}
    }
}
