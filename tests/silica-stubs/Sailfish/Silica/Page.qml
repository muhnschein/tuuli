import QtQuick 2.6

Item {
    // Silica pages fill the window; a size lets list views instantiate delegates.
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    property int allowedOrientations: 0
    property int status: 0
    property int orientation: 1
    property bool isPortrait: true
    property bool isLandscape: false
    property bool backNavigation: true
    property bool forwardNavigation: false
    property bool showNavigationIndicator: true
    property bool canNavigateForward: false
}
