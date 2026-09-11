// Stub of Sailfish.WebView's WebView: the properties and slots tuuli uses, recording
// calls so tests can assert on them. Property names follow qtmozembed's
// qmozview_defined_wrapper.h and sailfish-components-webview's WebView.qml.
import QtQuick 2.6

Item {
    id: webView

    property url url
    property string title
    property bool loading: false
    property int loadProgress: 0
    property bool canGoBack: false
    property bool canGoForward: false
    property bool active: false
    property bool privateMode: false
    property bool desktopMode: false
    property bool downloadsEnabled: false
    property bool domContentLoaded: false
    property string httpUserAgent
    property var popupProvider

    // Test hooks
    property var calls: []
    property string lastScript
    property string scriptResult: ""
    property bool scriptFails: false

    signal linkClicked(string url)
    signal viewInitialized()

    function record(name) {
        var list = calls
        list.push(name)
        calls = list
    }

    function goBack() {
        record("goBack")
    }

    function goForward() {
        record("goForward")
    }

    function reload() {
        record("reload")
    }

    function stop() {
        record("stop")
    }

    function load(target, fromExternal) {
        record("load")
        url = target
    }

    function runJavaScript(script, callback, errorCallback) {
        lastScript = script
        if (scriptFails) {
            if (errorCallback) {
                errorCallback("stub failure")
            }
        } else if (callback) {
            callback(scriptResult)
        }
    }
}
