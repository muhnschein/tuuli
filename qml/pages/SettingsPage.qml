// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
import QtQuick 2.6
import Sailfish.Silica 1.0
import Sailfish.WebEngine 1.0
import harbour.tuuli 1.0

Page {
    id: settingsPage

    objectName: "settingsPage"
    allowedOrientations: Orientation.Portrait

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column

            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Settings")
            }

            TextField {
                id: homePageField

                objectName: "homePageField"
                width: parent.width
                label: qsTr("Home page")
                placeholderText: label
                text: Settings.homePage
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: {
                    Settings.homePage = Settings.urlForInput(text)
                    focus = false
                }
            }

            ComboBox {
                objectName: "searchEngineCombo"
                width: parent.width
                label: qsTr("Search engine")
                currentIndex: Settings.searchEngineIndex
                menu: ContextMenu {
                    Repeater {
                        model: Settings.searchEngineNames

                        MenuItem {
                            text: modelData
                        }
                    }
                }
                onCurrentIndexChanged: Settings.searchEngineIndex = currentIndex
            }

            TextSwitch {
                objectName: "desktopModeSwitch"
                text: qsTr("Request desktop sites")
                description: qsTr("Identify as a desktop browser to web sites")
                checked: Settings.desktopMode
                onCheckedChanged: Settings.desktopMode = checked
            }

            SectionHeader {
                text: qsTr("Clear data")
            }

            Button {
                objectName: "clearHistoryButton"
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Clear history")
                onClicked: Remorse.popupAction(settingsPage, qsTr("Clearing history"), function () {
                    HistoryModel.clear()
                })
            }

            Button {
                objectName: "clearSiteDataButton"
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Clear cookies and site data")
                onClicked: Remorse.popupAction(settingsPage, qsTr("Clearing site data"), function () {
                    WebEngine.notifyObservers(EngineMessages.clearPrivateDataTopic,
                                              EngineMessages.cookiesAndSiteDataPayload)
                })
            }

            Button {
                objectName: "clearCacheButton"
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Clear cache")
                onClicked: Remorse.popupAction(settingsPage, qsTr("Clearing cache"), function () {
                    WebEngine.notifyObservers(EngineMessages.clearPrivateDataTopic,
                                              EngineMessages.cachePayload)
                })
            }

            Button {
                objectName: "closeAllTabsButton"
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Close all tabs")
                onClicked: Remorse.popupAction(settingsPage, qsTr("Closing all tabs"), function () {
                    TabModel.closeAllTabs()
                })
            }
        }

        VerticalScrollDecorator {}
    }
}
