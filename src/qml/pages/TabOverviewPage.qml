// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0
import "../components"

// Tab overview (spec 7.1): grid of thumbnails, swipe-to-close, long-press
// to reorder.  Top pulley: new tab, new private tab, close all (spec 7.2).
Page {
    id: page

    allowedOrientations: Orientation.Portrait | Orientation.Landscape | Orientation.LandscapeInverted

    function activate(index) {
        Browser.tabs.activate(index)
        pageStack.pop()
    }

    SilicaGridView {
        id: grid
        anchors.fill: parent
        model: Browser.tabs
        cellWidth: width / (isPortrait ? 2 : 3)
        cellHeight: cellWidth * 1.4
        currentIndex: Browser.tabs.currentIndex

        header: PageHeader {
            //% "Tabs"
            title: qsTrId("tuuli-he-tabs")
            //% "%n open"
            description: qsTrId("tuuli-la-tabs_open", Browser.tabs.count)
        }

        PullDownMenu {
            MenuItem {
                //% "Close all"
                text: qsTrId("tuuli-me-close_all")
                enabled: Browser.tabs.count > 0
                onClicked: remorse.execute(qsTrId("tuuli-me-close_all"), function() {
                    Browser.tabs.closeAll()
                    Browser.tabs.newTab("", false, true)
                    pageStack.pop()
                })
            }
            MenuItem {
                //% "New private tab"
                text: qsTrId("tuuli-me-new_private_tab")
                onClicked: {
                    Browser.tabs.newTab("", true, true)
                    pageStack.pop()
                }
            }
            MenuItem {
                //% "New tab"
                text: qsTrId("tuuli-me-new_tab")
                onClicked: {
                    Browser.tabs.newTab("", false, true)
                    pageStack.pop()
                }
            }
        }

        ViewPlaceholder {
            enabled: Browser.tabs.count === 0
            //% "No open tabs"
            text: qsTrId("tuuli-la-no_tabs")
        }

        delegate: Item {
            id: cellWrapper
            width: grid.cellWidth
            height: grid.cellHeight

            property int visualIndex: index
            property bool dragging: false

            TabThumbnail {
                id: cell
                anchors.fill: parent
                title: model.title
                thumbnail: model.thumbnail
                isPrivate: model.isPrivate
                active: model.active
                loading: model.loading
                scale: dragging ? 1.05 : 1.0
                z: dragging ? 10 : 0
                onClicked: page.activate(index)
                onCloseRequested: Browser.tabs.closeTab(index)

                // Long-press to reorder: drag over another cell and drop.
                onPressAndHold: {
                    dragging = true
                    dragArea.enabled = true
                }
                Behavior on scale { NumberAnimation { duration: 100 } }
            }

            MouseArea {
                id: dragArea
                anchors.fill: parent
                enabled: false
                drag.target: cell
                drag.axis: Drag.XAndYAxis
                onReleased: {
                    var dropIndex = grid.indexAt(cell.x + cellWrapper.x + cell.width / 2,
                                                 cell.y + cellWrapper.y + cell.height / 2 + grid.contentY)
                    if (dropIndex >= 0 && dropIndex !== index)
                        Browser.tabs.moveTab(index, dropIndex)
                    cell.x = 0
                    cell.y = 0
                    dragging = false
                    enabled = false
                }
            }

            // Swipe-to-close: horizontal flick beyond a third of the width.
            MouseArea {
                anchors.fill: parent
                enabled: !dragging
                propagateComposedEvents: true
                property real startX: 0
                onPressed: { startX = mouse.x; mouse.accepted = false }
                onPositionChanged: {
                    if (Math.abs(mouse.x - startX) > width / 3) {
                        Browser.tabs.closeTab(index)
                    }
                }
            }
        }

        VerticalScrollDecorator {}
    }

    RemorsePopup { id: remorse }
}
