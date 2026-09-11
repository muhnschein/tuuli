// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.tuuli 1.0
import "../components"

Page {
    id: bookmarksPage

    objectName: "bookmarksPage"
    allowedOrientations: Orientation.Portrait

    function open(url, inNewTab) {
        var browser = pageStack.find(function (page) {
            return page.objectName === "browserPage"
        })
        if (inNewTab || !browser) {
            TabModel.newTab(url)
        } else {
            browser.openUrl(url)
        }
        pageStack.pop(browser)
    }

    SilicaListView {
        id: bookmarkList

        objectName: "bookmarkList"
        anchors.fill: parent
        model: BookmarkModel
        header: PageHeader {
            title: qsTr("Bookmarks")
        }

        PullDownMenu {
            MenuItem {
                objectName: "addBookmarkMenu"
                text: qsTr("Bookmark current page")
                enabled: TabModel.activeUrl.length > 0 && !BookmarkModel.activeUrlBookmarked
                onClicked: BookmarkModel.add(TabModel.activeUrl, TabModel.activeTitle,
                                             TabModel.activeFavicon)
            }
        }

        delegate: BookmarkDelegate {
            onClicked: bookmarksPage.open(model.url, false)
            onOpenInNewTabRequested: bookmarksPage.open(model.url, true)
            onEditRequested: pageStack.push(Qt.resolvedUrl("BookmarkEditDialog.qml"), {
                                                "bookmarkIndex": index,
                                                "url": model.url,
                                                "title": model.title
                                            })
            onRemoveRequested: BookmarkModel.remove(index)
        }

        ViewPlaceholder {
            enabled: BookmarkModel.count === 0
            text: qsTr("No bookmarks")
            hintText: qsTr("Pull down to bookmark the current page")
        }

        VerticalScrollDecorator {}
    }
}
