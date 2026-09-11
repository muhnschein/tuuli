import QtQuick 2.6

Item {
    id: appWindow

    property var initialPage
    property var cover
    property int allowedOrientations: 0
    property alias pageStack: stack
    property Item coverItem: null
    property int activateCount: 0

    width: 1080
    height: 2520

    function activate() {
        activateCount += 1
    }

    function deactivate() {
    }

    PageStack {
        id: stack

        anchors.fill: parent
    }

    Component.onCompleted: {
        if (initialPage) {
            stack.push(initialPage)
        }
        if (cover) {
            var component = (typeof cover === "object" && cover.createObject) ? cover
                                                                              : Qt.createComponent(cover)
            if (component.status === Component.Error) {
                console.error(component.errorString())
            } else {
                coverItem = component.createObject(appWindow)
            }
        }
    }
}
