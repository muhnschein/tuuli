// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0

// Settings (spec 7.1): engine prefs, privacy, downloads location, UA
// override, developer toggles.
Page {
    id: page

    allowedOrientations: Orientation.Portrait | Orientation.Landscape | Orientation.LandscapeInverted

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                //% "Settings"
                title: qsTrId("tuuli-he-settings")
            }

            SectionHeader {
                //% "General"
                text: qsTrId("tuuli-he-general")
            }

            ComboBox {
                id: searchCombo
                //% "Search engine"
                label: qsTrId("tuuli-la-search_engine")
                //% "No default-search revenue arrangement of any kind."
                description: qsTrId("tuuli-la-search_engine_desc")
                currentIndex: {
                    var engines = Browser.searchEngines
                    for (var i = 0; i < engines.length; ++i)
                        if (engines[i].id === Browser.prefs.searchEngine) return i
                    return 0
                }
                menu: ContextMenu {
                    Repeater {
                        model: Browser.searchEngines
                        MenuItem {
                            text: modelData.name
                            onClicked: Browser.prefs.searchEngine = modelData.id
                        }
                    }
                }
            }

            TextField {
                width: parent.width
                //% "Home page"
                label: qsTrId("tuuli-la-home_page")
                //% "Leave empty for the start page"
                placeholderText: qsTrId("tuuli-ph-home_page")
                text: Browser.prefs.homePage
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: { Browser.prefs.homePage = text; focus = false }
                onActiveFocusChanged: if (!activeFocus) Browser.prefs.homePage = text
            }

            TextSwitch {
                //% "Restore tabs on start"
                text: qsTrId("tuuli-la-restore_session")
                checked: Browser.prefs.restoreSession
                onCheckedChanged: Browser.prefs.restoreSession = checked
            }

            SectionHeader {
                //% "Privacy"
                text: qsTrId("tuuli-he-privacy")
            }

            TextSwitch {
                //% "Block third-party cookies"
                text: qsTrId("tuuli-la-block_third_party_cookies")
                checked: Browser.prefs.blockThirdPartyCookies
                onCheckedChanged: Browser.prefs.blockThirdPartyCookies = checked
            }
            TextSwitch {
                //% "Send Do Not Track and Global Privacy Control"
                text: qsTrId("tuuli-la-send_dnt_gpc")
                checked: Browser.prefs.sendDoNotTrack && Browser.prefs.sendGlobalPrivacyControl
                onCheckedChanged: {
                    Browser.prefs.sendDoNotTrack = checked
                    Browser.prefs.sendGlobalPrivacyControl = checked
                }
            }
            ComboBox {
                //% "Referrer policy"
                label: qsTrId("tuuli-la-referrer_policy")
                property var policies: ["strict-origin-when-cross-origin", "no-referrer", "same-origin", "strict-origin"]
                currentIndex: Math.max(0, policies.indexOf(Browser.prefs.referrerPolicy))
                menu: ContextMenu {
                    MenuItem { text: "strict-origin-when-cross-origin"; onClicked: Browser.prefs.referrerPolicy = text }
                    MenuItem { text: "no-referrer"; onClicked: Browser.prefs.referrerPolicy = text }
                    MenuItem { text: "same-origin"; onClicked: Browser.prefs.referrerPolicy = text }
                    MenuItem { text: "strict-origin"; onClicked: Browser.prefs.referrerPolicy = text }
                }
            }
            TextSwitch {
                //% "Cosmetic filtering"
                text: qsTrId("tuuli-la-cosmetic_filtering")
                //% "Hides page elements matched by filter lists in %1. Network requests are not blocked; this is not ad blocking. %n rules loaded."
                description: qsTrId("tuuli-la-cosmetic_filtering_desc", Browser.cosmeticRuleCount).arg(Browser.dataDirectory + "/filters")
                checked: Browser.prefs.cosmeticFiltering
                onCheckedChanged: Browser.prefs.cosmeticFiltering = checked
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                //% "Reload filter lists"
                text: qsTrId("tuuli-bt-reload_filters")
                onClicked: Browser.reloadCosmeticRules()
            }
            BackgroundItem {
                width: parent.width
                onClicked: pageStack.push(Qt.resolvedUrl("PermissionsPage.qml"))
                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    //% "Site permissions"
                    text: qsTrId("tuuli-la-site_permissions")
                    color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                //% "Clear browsing data"
                text: qsTrId("tuuli-bt-clear_data")
                onClicked: pageStack.push(clearDataDialog)
            }

            SectionHeader {
                //% "Engine"
                text: qsTrId("tuuli-he-engine")
            }
            TextSwitch {
                //% "JavaScript"
                text: qsTrId("tuuli-la-javascript")
                checked: Browser.prefs.javascriptEnabled
                onCheckedChanged: Browser.prefs.javascriptEnabled = checked
            }
            TextField {
                width: parent.width
                //% "User agent override"
                label: qsTrId("tuuli-la-ua_override")
                //% "Empty uses the mobile default"
                placeholderText: qsTrId("tuuli-ph-ua_override")
                text: Browser.prefs.userAgentOverride
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: { Browser.prefs.userAgentOverride = text; focus = false }
                onActiveFocusChanged: if (!activeFocus) Browser.prefs.userAgentOverride = text
            }

            SectionHeader {
                //% "Downloads"
                text: qsTrId("tuuli-he-downloads")
            }
            TextField {
                width: parent.width
                //% "Download folder"
                label: qsTrId("tuuli-la-download_dir")
                text: Browser.prefs.downloadDirectory
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: { Browser.prefs.downloadDirectory = text; focus = false }
                onActiveFocusChanged: if (!activeFocus) Browser.prefs.downloadDirectory = text
            }

            SectionHeader {
                //% "Developer"
                text: qsTrId("tuuli-he-developer")
            }
            Slider {
                width: parent.width
                //% "Content pixel ratio override"
                label: qsTrId("tuuli-la-dpr_override")
                minimumValue: 0
                maximumValue: 4
                stepSize: 0.25
                value: Browser.prefs.devicePixelRatioOverride
                //% "auto"
                valueText: value === 0 ? qsTrId("tuuli-la-auto") : value.toFixed(2)
                onReleased: Browser.prefs.devicePixelRatioOverride = value
            }
            Slider {
                width: parent.width
                //% "Live web views kept in memory"
                label: qsTrId("tuuli-la-max_live_webviews")
                minimumValue: 1
                maximumValue: 16
                stepSize: 1
                value: Browser.prefs.maxLiveWebViews
                valueText: value
                onReleased: Browser.prefs.maxLiveWebViews = value
            }
            TextSwitch {
                //% "Show frame statistics"
                text: qsTrId("tuuli-la-show_frame_stats")
                checked: Browser.prefs.showFrameStats
                onCheckedChanged: Browser.prefs.showFrameStats = checked
            }
            TextSwitch {
                //% "Single-threaded render loop (restart required)"
                text: qsTrId("tuuli-la-basic_render_loop")
                //% "Fallback if the engine misbehaves on the threaded scene graph"
                description: qsTrId("tuuli-la-basic_render_loop_desc")
                checked: Browser.prefs.basicRenderLoop
                onCheckedChanged: Browser.prefs.basicRenderLoop = checked
            }
            TextSwitch {
                //% "Engine logging (restart required)"
                text: qsTrId("tuuli-la-engine_logging")
                checked: Browser.prefs.engineLogging
                onCheckedChanged: Browser.prefs.engineLogging = checked
            }
            TextSwitch {
                //% "Performance logging"
                text: qsTrId("tuuli-la-perf_logging")
                //% "Writes timing samples for tools/perf/run-budgets.py"
                description: qsTrId("tuuli-la-perf_logging_desc")
                checked: Browser.prefs.perfLogging
                onCheckedChanged: Browser.prefs.perfLogging = checked
            }

            SectionHeader {
                //% "About"
                text: qsTrId("tuuli-he-about")
            }
            BackgroundItem {
                width: parent.width
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    //% "About Tuuli %1 (engine: %2 %3)"
                    text: qsTrId("tuuli-la-about_line").arg(Browser.version).arg(Browser.engineName).arg(Browser.engineVersion)
                    color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                    truncationMode: TruncationMode.Fade
                    width: parent.width - 2 * Theme.horizontalPageMargin
                }
            }
        }
        VerticalScrollDecorator {}
    }

    Component {
        id: clearDataDialog
        Dialog {
            Column {
                width: parent.width
                DialogHeader {
                    //% "Clear"
                    acceptText: qsTrId("tuuli-he-clear")
                }
                TextSwitch { id: clearHistory; checked: true
                    //% "History"
                    text: qsTrId("tuuli-la-clear_history") }
                TextSwitch { id: clearCookies; checked: true
                    //% "Cookies"
                    text: qsTrId("tuuli-la-clear_cookies") }
                TextSwitch { id: clearCache; checked: true
                    //% "Cache"
                    text: qsTrId("tuuli-la-clear_cache") }
                TextSwitch { id: clearStorage; checked: false
                    //% "Site storage"
                    text: qsTrId("tuuli-la-clear_storage") }
                TextSwitch { id: clearPermissions; checked: false
                    //% "Site permissions"
                    text: qsTrId("tuuli-la-site_permissions") }
            }
            onAccepted: Browser.clearBrowsingData(clearHistory.checked, clearCookies.checked, clearCache.checked,
                                                  clearStorage.checked, clearPermissions.checked)
        }
    }
}
