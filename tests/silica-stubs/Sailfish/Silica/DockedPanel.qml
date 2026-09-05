import QtQuick 2.0
Item {
    property int dock: 1
    property bool open: false
    property bool modal: false
    property bool moving: false
    property bool expanded: open
    property int animationDuration: 200
    property bool background: true
    function show() { open = true }
    function hide() { open = false }
}
