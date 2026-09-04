// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0

// Long-press context menu (spec 6.2), populated from the engine hit test.
ContextMenu {
    id: menu

    property Tab tab
    property Item webView
    property url linkUrl
    property url imageUrl
    property string selectedText
    property bool editable: false

    readonly property bool hasLink: linkUrl.toString().length > 0
    readonly property bool hasImage: imageUrl.toString().length > 0
    readonly property bool hasSelection: selectedText.length > 0

    MenuLabel {
        visible: hasLink
        text: linkUrl
    }
    MenuItem {
        visible: hasLink
        //% "Open in new tab"
        text: qsTrId("tuuli-me-open_new_tab")
        onClicked: Browser.openUrl(linkUrl, tab ? tab.isPrivate : false, true)
    }
    MenuItem {
        visible: hasLink
        //% "Open in new private tab"
        text: qsTrId("tuuli-me-open_new_private_tab")
        onClicked: Browser.openUrl(linkUrl, true, true)
    }
    MenuItem {
        visible: hasLink
        //% "Copy link"
        text: qsTrId("tuuli-me-copy_link")
        onClicked: Browser.clipboard.setText(linkUrl)
    }
    MenuItem {
        visible: hasLink
        //% "Share link"
        text: qsTrId("tuuli-me-share_link")
        onClicked: Browser.share(linkUrl, linkUrl)
    }
    MenuItem {
        visible: hasImage
        //% "Open image in new tab"
        text: qsTrId("tuuli-me-open_image")
        onClicked: Browser.openUrl(imageUrl, tab ? tab.isPrivate : false, true)
    }
    MenuItem {
        visible: hasImage
        //% "Copy image address"
        text: qsTrId("tuuli-me-copy_image_url")
        onClicked: Browser.clipboard.setText(imageUrl)
    }
    MenuItem {
        visible: hasSelection
        //% "Copy"
        text: qsTrId("tuuli-me-copy")
        onClicked: Browser.clipboard.setText(selectedText)
    }
    MenuItem {
        visible: hasSelection
        //% "Search for selection"
        text: qsTrId("tuuli-me-search_selection")
        onClicked: Browser.openInput(selectedText, tab ? tab.isPrivate : false, true)
    }
    MenuItem {
        visible: editable && Browser.clipboard.hasText
        //% "Paste"
        text: qsTrId("tuuli-me-paste")
        onClicked: if (webView) webView.sendEditingAction(2) // EditingAction::Paste
    }
    MenuItem {
        visible: editable
        //% "Select all"
        text: qsTrId("tuuli-me-select_all")
        onClicked: if (webView) webView.sendEditingAction(3) // EditingAction::SelectAll
    }
    MenuItem {
        visible: !hasLink && !hasImage && !hasSelection && !editable
        //% "Copy page address"
        text: qsTrId("tuuli-me-copy_page_url")
        onClicked: if (tab) Browser.clipboard.setText(tab.url)
    }
}
