// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0

// Spec 9.2: the threat model is disclosed plainly in the app.
Page {
    id: page

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader {
                //% "About Tuuli"
                title: qsTrId("tuuli-he-about_tuuli")
                description: "Tuuli " + Browser.version + " · " + Browser.engineName + " " + Browser.engineVersion
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                //% "Tuuli is a Servo-based web browser for Sailfish OS. It is a second browser that ships alongside Sailfish Browser, not a replacement for it."
                text: qsTrId("tuuli-la-about_intro")
            }

            SectionHeader {
                //% "Security notice"
                text: qsTrId("tuuli-he-security_notice")
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                //% "Tuuli offers no meaningful sandbox between web content and the app's own privileges. The engine runs in the same process as the browser, and its sandboxing is incomplete upstream. Web content that achieves code execution gets everything the app's sailjail profile grants: internet, audio, and your Downloads, Pictures, Videos and Documents folders. If you need a hardened browser, use Sailfish Browser."
                text: qsTrId("tuuli-la-security_notice")
            }

            SectionHeader {
                //% "Licence"
                text: qsTrId("tuuli-he-licence")
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryColor
                //% "Tuuli and the Servo engine are released under the Mozilla Public License 2.0. Source: github.com/muhnschein/tuuli"
                text: qsTrId("tuuli-la-licence")
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                //% "Data directory: %1"
                text: qsTrId("tuuli-la-data_dir").arg(Browser.dataDirectory)
            }
        }
        VerticalScrollDecorator {}
    }
}
