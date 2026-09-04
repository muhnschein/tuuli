// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0

// window.alert / confirm / prompt as a Silica dialog.
Dialog {
    id: dialog

    property SimpleDialogRequest request
    property bool answered: false
    readonly property bool isPrompt: request && request.kind === 2
    readonly property bool isAlert: request && request.kind === 0

    onAccepted: {
        answered = true
        request.accept(isPrompt ? promptField.text : "")
    }
    onRejected: {
        answered = true
        request.dismiss()
    }
    Component.onDestruction: if (!answered && request) request.dismiss()

    Column {
        width: parent.width
        spacing: Theme.paddingLarge

        DialogHeader {
            //% "OK"
            acceptText: qsTrId("tuuli-he-ok")
            //% "Cancel"
            cancelText: isAlert ? "" : qsTrId("tuuli-he-cancel")
        }

        Label {
            width: parent.width - 2 * Theme.horizontalPageMargin
            x: Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            color: Theme.highlightColor
            text: request ? request.message : ""
        }

        TextField {
            id: promptField
            visible: isPrompt
            width: parent.width
            text: request ? request.defaultValue : ""
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: dialog.accept()
        }
    }
}
