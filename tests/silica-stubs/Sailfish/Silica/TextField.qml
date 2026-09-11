import QtQuick 2.6

Item {
    property string text
    property string label
    property string placeholderText
    property int inputMethodHints: 0
    property bool readOnly: false
    property int selectAllCount: 0

    function selectAll() {
        selectAllCount += 1
    }
}
