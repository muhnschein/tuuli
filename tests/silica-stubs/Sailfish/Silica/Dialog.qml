import QtQuick 2.6

Page {
    id: dialog

    property bool canAccept: true
    property var acceptDestination

    signal accepted()
    signal rejected()

    function accept() {
        if (canAccept) {
            accepted()
        }
    }

    function reject() {
        rejected()
    }
}
