import QtQuick

// In-memory stand-in for AppSettings (nothing is persisted by the preview).
QtObject {
    property string theme: "system"
    readonly property bool effectiveDark: theme === "dark"
        || (theme === "system" && Qt.styleHints.colorScheme === Qt.ColorScheme.Dark) // qmllint disable missing-property
    property int windowX: 0
    property int windowY: 0
    property int windowWidth: 0
    property int windowHeight: 0
    property bool windowMaximized: false
    property int leftPaneWidth: 0
    property int rightPaneWidth: 0
    property list<int> columnWidths: []
    readonly property string lastGameKey: "stellaris"

    function save() {
        console.log("[fake] settings.save()", theme, windowWidth + "x" + windowHeight,
                    leftPaneWidth, rightPaneWidth, JSON.stringify(columnWidths))
    }
}
