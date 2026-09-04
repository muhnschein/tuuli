// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0

// Downloads (spec 7.1).  Non-private downloads also appear in the system
// Transfers page via Nemo Transfer Engine.
Page {
    id: page

    SilicaListView {
        anchors.fill: parent
        model: Browser.downloads

        header: PageHeader {
            //% "Downloads"
            title: qsTrId("tuuli-he-downloads")
            description: Browser.prefs.downloadDirectory
        }

        PullDownMenu {
            MenuItem {
                //% "Clear finished"
                text: qsTrId("tuuli-me-clear_finished")
                enabled: Browser.downloads.count > 0
                onClicked: Browser.downloads.clearFinished()
            }
        }

        ViewPlaceholder {
            enabled: Browser.downloads.count === 0
            //% "No downloads in this session"
            text: qsTrId("tuuli-la-no_downloads")
        }

        delegate: ListItem {
            width: ListView.view.width
            contentHeight: Theme.itemSizeMedium
            onClicked: if (model.finished && model.ok) Qt.openUrlExternally("file://" + model.path)
            menu: ContextMenu {
                MenuItem {
                    visible: !model.finished
                    //% "Cancel"
                    text: qsTrId("tuuli-me-cancel")
                    onClicked: Browser.downloads.cancel(model.downloadId)
                }
                MenuItem {
                    //% "Remove from list"
                    text: qsTrId("tuuli-me-remove_from_list")
                    onClicked: Browser.downloads.remove(model.downloadId)
                }
            }
            Column {
                anchors {
                    left: parent.left
                    right: parent.right
                    margins: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    text: model.fileName + (model.isPrivate ? " · " + qsTrId("tuuli-la-private_label") : "")
                }
                ProgressBar {
                    width: parent.width
                    visible: !model.finished
                    indeterminate: model.total <= 0
                    value: model.progress
                    leftMargin: 0
                    rightMargin: 0
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: model.finished && !model.ok ? Theme.errorColor : Theme.secondaryColor
                    text: model.finished
                          ? (model.ok
                             //% "Finished"
                             ? qsTrId("tuuli-la-download_finished")
                             //% "Failed: %1"
                             : qsTrId("tuuli-la-download_failed").arg(model.error))
                          : Format.formatFileSize(model.received) + (model.total > 0 ? " / " + Format.formatFileSize(model.total) : "")
                }
            }
        }
        VerticalScrollDecorator {}
    }
}
