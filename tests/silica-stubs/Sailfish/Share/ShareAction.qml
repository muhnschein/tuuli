import QtQuick 2.6

QtObject {
    property var resources
    property string mimeType
    property string title
    property int triggerCount: 0

    function trigger() {
        triggerCount += 1
    }
}
