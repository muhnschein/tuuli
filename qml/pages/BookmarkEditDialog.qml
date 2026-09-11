// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.tuuli 1.0

Dialog {
    id: dialog

    property int bookmarkIndex: -1
    property alias url: urlField.text
    property alias title: titleField.text

    objectName: "bookmarkEditDialog"
    allowedOrientations: Orientation.Portrait
    canAccept: urlField.text.length > 0
    onAccepted: BookmarkModel.edit(bookmarkIndex, urlField.text, titleField.text)

    Column {
        width: parent.width

        DialogHeader {
            title: qsTr("Edit bookmark")
            acceptText: qsTr("Save")
        }

        TextField {
            id: titleField

            objectName: "bookmarkTitleField"
            width: parent.width
            label: qsTr("Title")
            placeholderText: label
            EnterKey.iconSource: "image://theme/icon-m-enter-next"
            EnterKey.onClicked: urlField.focus = true
        }

        TextField {
            id: urlField

            objectName: "bookmarkUrlField"
            width: parent.width
            label: qsTr("Address")
            placeholderText: label
            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
            EnterKey.enabled: text.length > 0
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: dialog.accept()
        }
    }
}
