// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.tuuli 1.0
import "../components"

Page {
    id: historyPage

    objectName: "historyPage"
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
        id: historyList

        objectName: "historyList"
        anchors.fill: parent
        model: HistoryModel
        header: Column {
            width: parent.width

            PageHeader {
                title: qsTr("History")
            }

            SearchField {
                objectName: "historySearch"
                width: parent.width
                placeholderText: qsTr("Search history")
                onTextChanged: HistoryModel.searchTerm = text
            }
        }

        PullDownMenu {
            MenuItem {
                objectName: "clearHistoryMenu"
                text: qsTr("Clear history")
                enabled: HistoryModel.count > 0
                onClicked: Remorse.popupAction(historyPage, qsTr("Clearing history"), function () {
                    HistoryModel.clear()
                })
            }
        }

        delegate: HistoryDelegate {
            onClicked: historyPage.open(model.url, false)
            onOpenInNewTabRequested: historyPage.open(model.url, true)
            onRemoveRequested: HistoryModel.remove(index)
        }

        ViewPlaceholder {
            enabled: HistoryModel.count === 0
            text: HistoryModel.searchTerm.length > 0 ? qsTr("No matches") : qsTr("No history yet")
        }

        VerticalScrollDecorator {}
    }
}
