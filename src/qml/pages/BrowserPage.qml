// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0
import "../components"

// Page view (spec 7.1): full-bleed content, auto-hiding bottom toolbar,
// start view overlay when the tab is empty, find bar, context menu.
Page {
    id: page
    objectName: "browserPage"

    property Tab tab: Browser.tabs.currentTab
    property alias webView: webView
    readonly property bool startViewVisible: !tab || tab.url.toString().length === 0

    allowedOrientations: Orientation.Portrait | Orientation.Landscape | Orientation.LandscapeInverted

    function openInput(input) {
        Browser.openInput(input, tab ? tab.isPrivate : false, !tab)
    }

    function showTabs() {
        webView.grabThumbnail()
        pageStack.push(Qt.resolvedUrl("TabOverviewPage.qml"))
    }

    Component.onCompleted: {
        if (Browser.tabs.count === 0)
            Browser.tabs.newTab("", false, true)
    }

    // Spec 7.2, top pulley: reload, new tab, share, add bookmark.
    SilicaFlickable {
        id: flickable
        anchors.fill: parent
        contentHeight: height
        // The engine scrolls the page; the arbiter hands vertical drags at
        // the content edges to this flickable so the pulleys open (spec 7.2).
        interactive: true

        PullDownMenu {
            id: topMenu
            MenuItem {
                //% "Reload"
                text: qsTrId("tuuli-me-reload")
                enabled: tab && tab.url.toString().length > 0
                onClicked: tab.reload()
            }
            MenuItem {
                //% "New tab"
                text: qsTrId("tuuli-me-new_tab")
                onClicked: Browser.tabs.newTab("", false, true)
            }
            MenuItem {
                //% "Share"
                text: qsTrId("tuuli-me-share")
                enabled: tab && tab.url.toString().length > 0
                onClicked: Browser.share(tab.url, tab.title)
            }
            MenuItem {
                // bookmarks.count is read so the binding re-evaluates on changes
                text: tab && Browser.bookmarks.count >= 0 && Browser.bookmarks.contains(tab.url)
                      //% "Remove bookmark"
                      ? qsTrId("tuuli-me-remove_bookmark")
                      //% "Add bookmark"
                      : qsTrId("tuuli-me-add_bookmark")
                enabled: tab && tab.url.toString().length > 0
                onClicked: {
                    if (Browser.bookmarks.contains(tab.url))
                        Browser.bookmarks.remove(tab.url)
                    else
                        Browser.bookmarks.add(tab.url, tab.title)
                }
            }
        }

        // Spec 7.2, bottom pulley: find in page, desktop-mode toggle, page info.
        PushUpMenu {
            MenuItem {
                //% "Find in page"
                text: qsTrId("tuuli-me-find_in_page")
                enabled: !startViewVisible
                onClicked: findBar.show()
            }
            MenuItem {
                text: tab && tab.desktopMode
                      //% "Mobile site"
                      ? qsTrId("tuuli-me-mobile_site")
                      //% "Desktop site"
                      : qsTrId("tuuli-me-desktop_site")
                enabled: tab
                onClicked: tab.desktopMode = !tab.desktopMode
            }
            MenuItem {
                //% "Page info"
                text: qsTrId("tuuli-me-page_info")
                enabled: !startViewVisible
                onClicked: pageStack.push(Qt.resolvedUrl("PageInfoPage.qml"), { tab: tab })
            }
        }

        WebView {
            id: webView
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                bottom: parent.bottom
            }
            tab: page.tab
            visible: !startViewVisible
            devicePixelRatioOverride: Browser.prefs.devicePixelRatioOverride
            placeholderColor: Theme.rgba(Theme.highlightDimmerColor, 1.0)
            // Content covered by the keyboard or the toolbar (spec 6.3).
            bottomInset: Math.max(inputProxy.keyboardHeight, toolbar.open ? toolbar.height : 0)

            onBottomEdgeProgress: if (progress > 0.15) toolbar.open = true
            onBottomEdgeFinished: toolbar.open = committed || toolbar.open

            onContextMenuRequested: {
                contextMenu.linkUrl = linkUrl
                contextMenu.imageUrl = imageUrl
                contextMenu.selectedText = selectedText
                contextMenu.editable = editable
                contextMenuAnchor.x = x
                contextMenuAnchor.y = y
                contextMenu.show(contextMenuAnchor)
            }

            onEngineInitFailed: {
                //% "The web engine could not start on this device's graphics driver."
                Browser.notify(qsTrId("tuuli-la-engine_init_failed"))
            }

            // Hide the toolbar once content starts scrolling: the engine
            // owns scrolling, so we watch the viewport instead of touches.
            Connections {
                target: page.tab
                onViewportChanged: if (!toolbar.editing && !findBar.active) toolbar.open = false
                onLoadingChanged: if (page.tab.loading) toolbar.open = true
            }
        }

        // Anchor item for the Silica ContextMenu at the long-press point.
        Item {
            id: contextMenuAnchor
            width: 1
            height: 1
        }

        LinkContextMenu {
            id: contextMenu
            tab: page.tab
            webView: webView
        }

        StartView {
            id: startView
            anchors.fill: parent
            visible: startViewVisible
            focusSearchOnShow: true
            onInputEntered: page.openInput(input)
            onUrlActivated: Browser.openUrl(url, tab ? tab.isPrivate : false, false)
        }

        // Frame statistics overlay (developer toggle).
        Label {
            anchors { top: parent.top; right: parent.right; margins: Theme.paddingMedium }
            visible: Browser.prefs.showFrameStats && !startViewVisible
            font.pixelSize: Theme.fontSizeTiny
            color: Theme.highlightColor
            text: webView.lastFrameMs.toFixed(1) + " ms · #" + webView.frameCount + " · dpr " + webView.contentDevicePixelRatio
        }

        // Maliit attaches to this hidden proxy (spec 6.3).
        TextInputProxy {
            id: inputProxy
            ime: webView.inputMethod
        }
    }

    FindBar {
        id: findBar
        anchors.bottom: toolbar.top
        tab: page.tab
        z: 2
    }

    BottomToolbar {
        id: toolbar
        tab: page.tab
        z: 3
        onUrlEntered: page.openInput(input)
        onTabsRequested: page.showTabs()
        onOverflowRequested: overflow.open(overflowAnchor)
    }

    Item {
        id: overflowAnchor
        anchors.bottom: toolbar.top
        width: parent.width
        height: 1
    }

    ContextMenu {
        id: overflow
        MenuItem {
            //% "New private tab"
            text: qsTrId("tuuli-me-new_private_tab")
            onClicked: Browser.tabs.newTab("", true, true)
        }
        MenuItem {
            //% "History"
            text: qsTrId("tuuli-me-history")
            onClicked: pageStack.push(Qt.resolvedUrl("HistoryPage.qml"))
        }
        MenuItem {
            //% "Downloads"
            text: qsTrId("tuuli-me-downloads")
            onClicked: pageStack.push(Qt.resolvedUrl("DownloadsPage.qml"))
        }
        MenuItem {
            //% "Settings"
            text: qsTrId("tuuli-me-settings")
            onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
        }
    }

    onStatusChanged: {
        if (status === PageStatus.Active && Browser.tabs.count === 0)
            Browser.tabs.newTab("", false, true)
    }

    Keys.onPressed: {
        if (event.key === Qt.Key_F && (event.modifiers & Qt.ControlModifier)) {
            findBar.show()
            event.accepted = true
        } else if (event.key === Qt.Key_L && (event.modifiers & Qt.ControlModifier)) {
            toolbar.edit()
            event.accepted = true
        }
    }
}
