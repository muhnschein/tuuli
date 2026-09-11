// Stub: remorse timers run to completion immediately so tests observe the action.
pragma Singleton
import QtQuick 2.6

QtObject {
    property int popupCount: 0
    property int itemCount: 0

    function popupAction(item, text, callback) {
        popupCount += 1
        callback()
    }

    function itemAction(item, text, callback) {
        itemCount += 1
        callback()
    }
}
