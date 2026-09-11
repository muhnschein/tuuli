import QtQuick 2.6

Item {
    property bool active: false

    function open(item) {
        active = true
    }

    function close() {
        active = false
    }
}
