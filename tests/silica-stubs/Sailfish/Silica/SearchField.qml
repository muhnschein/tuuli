import QtQuick 2.0
Item {
    property string text: ""
    property string placeholderText
    property int inputMethodHints
    property string label
    property string description
    property bool canHide: false
    property bool active: true
    signal clicked()
    implicitWidth: 400
    implicitHeight: 60
}
