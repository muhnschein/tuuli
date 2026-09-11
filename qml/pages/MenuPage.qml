// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
import QtQuick 2.6
import Sailfish.Silica 1.0
import Sailfish.Share 1.0
import harbour.tuuli 1.0

Page {
    id: menuPage

    objectName: "menuPage"
    allowedOrientations: Orientation.Portrait

    ShareAction {
        id: shareAction

        objectName: "shareAction"
        mimeType: "text/x-url"
        resources: [{
                "type": "text/x-url",
                "linkTitle": TabModel.activeTitle,
                "status": TabModel.activeUrl
            }]
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column

            width: parent.width

            PageHeader {
                title: TabModel.activeTitle.length > 0 ? TabModel.activeTitle : qsTr("Menu")
                description: TabModel.activeUrl
            }

            ListItem {
                objectName: "newTabItem"
                onClicked: {
                    TabModel.newTab(Settings.homePage)
                    pageStack.pop()
                }

                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("New tab")
                }
            }

            ListItem {
                objectName: "newPrivateTabItem"
                onClicked: {
                    TabModel.newTab(Settings.homePage, true)
                    pageStack.pop()
                }

                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("New private tab")
                }
            }

            ListItem {
                objectName: "bookmarkItem"
                enabled: TabModel.activeUrl.length > 0
                onClicked: {
                    if (BookmarkModel.activeUrlBookmarked) {
                        BookmarkModel.removeByUrl(TabModel.activeUrl)
                    } else {
                        BookmarkModel.add(TabModel.activeUrl, TabModel.activeTitle,
                                          TabModel.activeFavicon)
                    }
                    pageStack.pop()
                }

                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: BookmarkModel.activeUrlBookmarked ? qsTr("Remove bookmark")
                                                            : qsTr("Bookmark this page")
                }
            }

            ListItem {
                objectName: "shareItem"
                enabled: TabModel.activeUrl.length > 0
                onClicked: shareAction.trigger()

                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Share")
                }
            }

            SectionHeader {
                text: qsTr("Browse")
            }

            ListItem {
                objectName: "bookmarksItem"
                onClicked: pageStack.replace(Qt.resolvedUrl("BookmarksPage.qml"))

                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Bookmarks")
                }
            }

            ListItem {
                objectName: "historyItem"
                onClicked: pageStack.replace(Qt.resolvedUrl("HistoryPage.qml"))

                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("History")
                }
            }

            ListItem {
                objectName: "settingsItem"
                onClicked: pageStack.replace(Qt.resolvedUrl("SettingsPage.qml"))

                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Settings")
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
