pragma Singleton
import QtQuick 2.0
QtObject {
    function popupAction(parent, text, action) { if (action) action() }
    function itemAction(item, text, action) { if (action) action() }
    function deletedText(count) { return "Deleted" }
}
