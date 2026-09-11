// Stub: values exist only so bindings resolve; nothing here imitates layout.
pragma Singleton
import QtQuick 2.6

QtObject {
    readonly property real paddingSmall: 6
    readonly property real paddingMedium: 12
    readonly property real paddingLarge: 24
    readonly property real horizontalPageMargin: 24
    readonly property real fontSizeTiny: 20
    readonly property real fontSizeExtraSmall: 24
    readonly property real fontSizeSmall: 28
    readonly property real fontSizeMedium: 32
    readonly property real fontSizeLarge: 40
    readonly property real fontSizeExtraLarge: 50
    readonly property real iconSizeSmall: 32
    readonly property real iconSizeMedium: 64
    readonly property real iconSizeLarge: 96
    readonly property real itemSizeSmall: 80
    readonly property real itemSizeMedium: 100
    readonly property real itemSizeLarge: 110
    readonly property real itemSizeExtraLarge: 135
    readonly property color primaryColor: "#ffffff"
    readonly property color secondaryColor: "#b0ffffff"
    readonly property color highlightColor: "#aaccff"
    readonly property color secondaryHighlightColor: "#b0aaccff"
    readonly property color highlightBackgroundColor: "#aaccff"
    readonly property real highlightBackgroundOpacity: 0.3

    function rgba(color, opacity) {
        return Qt.rgba(color.r, color.g, color.b, opacity)
    }
}
