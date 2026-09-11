pragma Singleton
import QtQuick 2.6

QtObject {
    property var notifications: []

    function notifyObservers(topic, value) {
        var list = notifications
        list.push({ "topic": topic, "value": value })
        notifications = list
    }
}
