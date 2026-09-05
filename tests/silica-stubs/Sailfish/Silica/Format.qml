pragma Singleton
import QtQuick 2.0
QtObject {
    function formatFileSize(bytes) { return bytes + " B" }
    function formatDate(date, format) { return "" + date }
}
