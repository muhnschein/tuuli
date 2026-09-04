// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0

// The hidden TextInput Maliit attaches to (spec 6.3).  The engine reports
// editable focus through WebView.inputMethod; every committed edit is
// diffed there and turned into key / composition events for the engine.
Item {
    id: proxy

    property InputMethodProxy ime
    readonly property bool keyboardVisible: Qt.inputMethod.visible && input.activeFocus
    readonly property int keyboardHeight: keyboardVisible ? Qt.inputMethod.keyboardRectangle.height : 0

    width: 1
    height: 1
    opacity: 0.0

    TextInput {
        id: input
        anchors.fill: parent
        clip: true
        color: "transparent"
        cursorVisible: false
        inputMethodHints: ime ? ime.inputMethodHints : Qt.ImhNone
        echoMode: ime && ime.passwordMode ? TextInput.Password : TextInput.Normal
        EnterKey.enabled: true
        EnterKey.iconSource: ime && ime.enterKeyType === Qt.EnterKeySearch ? "image://theme/icon-m-search"
                           : ime && ime.enterKeyType === Qt.EnterKeyGo ? "image://theme/icon-m-enter-accept"
                           : "image://theme/icon-m-enter-next"
        EnterKey.onClicked: {
            if (!ime) return
            if (ime.multiline) {
                ime.textEdited(text + "\n")
            } else {
                ime.submit()
                if (!ime.multiline) proxy.dismiss()
            }
        }

        // Guard against re-entrancy while the engine mirrors state back.
        property bool syncing: false

        onTextChanged: {
            if (syncing || !ime || !ime.active) return
            ime.textEdited(text)
        }

        Keys.onPressed: {
            if (!ime) return
            if (event.key === Qt.Key_Left || event.key === Qt.Key_Right
                    || event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                ime.sendKey(event.key, event.text, event.modifiers)
                event.accepted = true
            }
        }
    }

    function syncFromEngine() {
        input.syncing = true
        input.text = ime ? ime.text : ""
        if (ime) {
            var pos = Math.min(ime.cursorPosition, input.text.length)
            input.cursorPosition = pos
        }
        input.syncing = false
    }

    function dismiss() {
        if (ime) ime.dismiss()
        input.focus = false
        Qt.inputMethod.hide()
    }

    Connections {
        target: ime
        onActiveChanged: {
            if (ime.active) {
                proxy.syncFromEngine()
                input.forceActiveFocus()
                Qt.inputMethod.show()
            } else {
                input.focus = false
                Qt.inputMethod.hide()
            }
        }
        onTextChanged: if (ime.text !== input.text) proxy.syncFromEngine()
        onSelectionChanged: {
            if (input.syncing) return
            if (ime.cursorPosition !== input.cursorPosition) proxy.syncFromEngine()
        }
    }
}
