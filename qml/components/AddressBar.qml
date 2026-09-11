// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
import QtQuick 2.6
import Sailfish.Silica 1.0

Item {
    id: addressBar

    property string url
    property bool privateTab: false
    property bool loading: false
    property int loadProgress: 0

    signal accepted(string text)

    height: field.height

    TextField {
        id: field

        objectName: "addressField"
        anchors {
            left: parent.left
            right: parent.right
        }
        label: addressBar.privateTab ? qsTr("Private tab") : ""
        placeholderText: qsTr("Search or enter address")
        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
        EnterKey.enabled: text.length > 0
        EnterKey.iconSource: "image://theme/icon-m-enter-accept"
        EnterKey.onClicked: {
            addressBar.accepted(text)
            focus = false
        }
        onActiveFocusChanged: {
            if (activeFocus) {
                selectAll()
            }
        }
    }

    // Show the page address unless the user is typing.
    Binding {
        target: field
        property: "text"
        value: addressBar.url
        when: !field.activeFocus
    }

    Rectangle {
        objectName: "loadProgress"
        anchors {
            left: parent.left
            bottom: parent.bottom
        }
        height: Theme.paddingSmall
        width: parent.width * addressBar.loadProgress / 100
        color: Theme.highlightColor
        visible: addressBar.loading
    }
}
