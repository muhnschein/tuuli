// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
//
// The only file that imports Sailfish.WebView (SCOPE.md §5): a missing engine package
// breaks browsing, not the application.
import QtQuick 2.6
import Sailfish.Silica 1.0
import Sailfish.WebView 1.0
import harbour.tuuli 1.0
import "../components"

WebViewPage {
    id: browserPage

    // The WebView of the active tab, or null while it is being created.
    property Item currentView: null

    objectName: "browserPage"
    allowedOrientations: Orientation.Portrait

    function openUrl(url) {
        if (url.length === 0) {
            return
        }
        if (currentView) {
            currentView.url = url
        } else {
            TabModel.newTab(url)
        }
    }

    function updateCurrentView() {
        var loader = webViews.itemAt(TabModel.activeTabIndex)
        currentView = loader && loader.item ? loader.item : null
    }

    function ensureTab() {
        if (TabModel.count === 0) {
            TabModel.newTab(Settings.homePage)
        }
    }

    Component.onCompleted: {
        ensureTab()
        updateCurrentView()
    }

    Connections {
        target: TabModel
        onActiveTabChanged: browserPage.updateCurrentView()
        onCountChanged: browserPage.ensureTab()
    }

    AddressBar {
        id: addressBar

        objectName: "addressBar"
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        url: TabModel.activeUrl
        privateTab: TabModel.activeIsPrivate
        loading: currentView ? currentView.loading : false
        loadProgress: currentView ? currentView.loadProgress : 0
        onAccepted: browserPage.openUrl(Settings.urlForInput(text))
    }

    Item {
        id: viewArea

        anchors {
            top: addressBar.bottom
            bottom: toolbar.top
            left: parent.left
            right: parent.right
        }

        // One WebView per tab that has been shown this session. Restored tabs stay
        // unloaded until first activated (docs/DECISIONS/0003-one-webview-per-tab.md).
        Repeater {
            id: webViews

            objectName: "webViews"
            model: TabModel
            delegate: Loader {
                readonly property int tabId: model.tabId
                readonly property bool isCurrent: model.activeTab
                readonly property bool privateTab: model.privateTab
                readonly property string initialUrl: model.url
                property bool shown: false

                objectName: "webViewLoader"
                anchors.fill: parent
                active: shown
                visible: isCurrent
                sourceComponent: webViewComponent
                onIsCurrentChanged: {
                    if (isCurrent) {
                        shown = true
                    }
                }
                onItemChanged: browserPage.updateCurrentView()
                Component.onCompleted: {
                    if (isCurrent) {
                        shown = true
                    }
                }
            }
        }
    }

    Component {
        id: webViewComponent

        WebView {
            id: webView

            objectName: "webView"
            active: isCurrent && Qt.application.state === Qt.ApplicationActive
                    && (browserPage.status === PageStatus.Active
                        || browserPage.status === PageStatus.Deactivating)
            privateMode: privateTab
            desktopMode: Settings.desktopMode
            downloadsEnabled: true

            function fetchFavicon() {
                var pageUrl = url
                runJavaScript(EngineMessages.faviconScript, function (href) {
                    TabModel.updateFavicon(tabId, EngineMessages.resolveFavicon(pageUrl, href))
                }, function () {
                    TabModel.updateFavicon(tabId, EngineMessages.defaultFavicon(pageUrl))
                })
            }

            onUrlChanged: TabModel.updateUrl(tabId, url)
            onTitleChanged: TabModel.updateTitle(tabId, title)
            onLoadingChanged: {
                if (!loading) {
                    fetchFavicon()
                }
            }
            Component.onCompleted: url = initialUrl
        }
    }

    Toolbar {
        id: toolbar

        objectName: "toolbar"
        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }
        canGoBack: currentView ? currentView.canGoBack : false
        canGoForward: currentView ? currentView.canGoForward : false
        loading: currentView ? currentView.loading : false
        tabCount: TabModel.count
        onBack: currentView.goBack()
        onForward: currentView.goForward()
        onReload: currentView.reload()
        onStop: currentView.stop()
        onShowTabs: pageStack.push(Qt.resolvedUrl("TabsPage.qml"))
        onShowMenu: pageStack.push(Qt.resolvedUrl("MenuPage.qml"))
    }
}
