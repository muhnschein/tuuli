// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick 2.2
import Sailfish.Silica 1.0
import Tuuli 1.0

Page {
    id: page

    property Tab tab
    readonly property string origin: tab ? tab.url.toString().replace(/^([a-z]+:\/\/[^\/]+).*$/, "$1") : ""

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                //% "Page info"
                title: qsTrId("tuuli-me-page_info")
            }

            DetailItem {
                //% "Title"
                label: qsTrId("tuuli-la-title")
                value: tab ? tab.title : ""
            }
            DetailItem {
                //% "Address"
                label: qsTrId("tuuli-la-address")
                value: tab ? tab.url : ""
            }
            DetailItem {
                //% "Connection"
                label: qsTrId("tuuli-la-connection")
                value: tab && tab.url.toString().indexOf("https:") === 0
                       //% "Encrypted (HTTPS)"
                       ? qsTrId("tuuli-la-https")
                       //% "Not encrypted"
                       : qsTrId("tuuli-la-not_https")
            }
            DetailItem {
                //% "Mode"
                label: qsTrId("tuuli-la-mode")
                value: (tab && tab.isPrivate ? qsTrId("tuuli-la-private_label") + ", " : "")
                       + (tab && tab.desktopMode ? qsTrId("tuuli-me-desktop_site") : qsTrId("tuuli-me-mobile_site"))
            }
            DetailItem {
                //% "Proxy"
                label: qsTrId("tuuli-la-proxy")
                //% "System proxy active"
                value: Browser.proxyActive ? qsTrId("tuuli-la-proxy_active")
                                           //% "Direct connection"
                                           : qsTrId("tuuli-la-proxy_direct")
            }

            SectionHeader {
                //% "Permissions for this site"
                text: qsTrId("tuuli-he-site_permissions_here")
            }
            Repeater {
                model: ["geolocation", "notifications", "camera", "microphone"]
                delegate: DetailItem {
                    property int kind: index
                    label: modelData
                    value: {
                        var d = Browser.permissions.decisionFor(origin, kind)
                        //% "allowed"
                        if (d === 1) return qsTrId("tuuli-la-allowed")
                        //% "denied"
                        if (d === 2) return qsTrId("tuuli-la-denied")
                        //% "ask"
                        return qsTrId("tuuli-la-ask")
                    }
                }
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                //% "Forget decisions for this site"
                text: qsTrId("tuuli-bt-forget_site")
                onClicked: Browser.permissions.clearOrigin(origin)
            }
        }
        VerticalScrollDecorator {}
    }
}
