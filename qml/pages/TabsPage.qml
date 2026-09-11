// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.tuuli 1.0
import "../components"

Page {
    id: tabsPage

    objectName: "tabsPage"
    allowedOrientations: Orientation.Portrait

    SilicaListView {
        id: tabList

        objectName: "tabList"
        anchors.fill: parent
        model: TabModel
        header: PageHeader {
            title: qsTr("Tabs")
        }

        PullDownMenu {
            MenuItem {
                objectName: "closeAllTabsMenu"
                text: qsTr("Close all tabs")
                onClicked: Remorse.popupAction(tabsPage, qsTr("Closing all tabs"), function () {
                    TabModel.closeAllTabs()
                    pageStack.pop()
                })
            }
            MenuItem {
                objectName: "newPrivateTabMenu"
                text: qsTr("New private tab")
                onClicked: {
                    TabModel.newTab(Settings.homePage, true)
                    pageStack.pop()
                }
            }
            MenuItem {
                objectName: "newTabMenu"
                text: qsTr("New tab")
                onClicked: {
                    TabModel.newTab(Settings.homePage)
                    pageStack.pop()
                }
            }
        }

        delegate: TabDelegate {
            onClicked: {
                TabModel.activateTab(index)
                pageStack.pop()
            }
            onCloseRequested: TabModel.closeTab(index)
        }

        ViewPlaceholder {
            enabled: TabModel.count === 0
            text: qsTr("No open tabs")
            hintText: qsTr("Pull down to open one")
        }

        VerticalScrollDecorator {}
    }
}
