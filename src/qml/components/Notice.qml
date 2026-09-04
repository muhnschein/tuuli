// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0

// In-app transient notice (system notifications are M4, spec 8.3).
Rectangle {
    id: notice

    property alias text: label.text

    function show(message) {
        label.text = message
        opacity = 1.0
        hideTimer.restart()
    }

    width: Math.min(label.implicitWidth + 2 * Theme.paddingLarge, parent ? parent.width - 2 * Theme.horizontalPageMargin : 0)
    height: label.implicitHeight + 2 * Theme.paddingMedium
    radius: Theme.paddingSmall
    color: Theme.rgba(Theme.highlightBackgroundColor, Theme.highlightBackgroundOpacity)
    opacity: 0.0
    visible: opacity > 0
    z: 1000

    Behavior on opacity { FadeAnimation {} }

    Label {
        id: label
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.primaryColor
    }

    Timer {
        id: hideTimer
        interval: 3500
        onTriggered: notice.opacity = 0.0
    }

    MouseArea {
        anchors.fill: parent
        onClicked: notice.opacity = 0.0
    }
}
