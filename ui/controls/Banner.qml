import QtQuick
import QtQuick.Layouts
import Pmm.Ui

// Inline message bar. `level`: "info" | "warning" | "error" | "success".
// Children are placed as actions on the right.
Rectangle {
    id: root

    property string level: "info"
    property string title
    property string text
    property string iconName: Theme.levelIcon(level)
    property bool closable: false
    property bool actionsBelow: false
    // Spoken when the banner appears or its message changes (errors by default).
    property bool announced: level === "error"
    default property alias actions: actionRow.data

    signal closed()

    function announce() {
        if (announced && visible && (title.length > 0 || text.length > 0))
            Accessible.announce(Accessible.name, Accessible.Assertive)
    }

    implicitHeight: layout.implicitHeight + 10
    radius: Theme.radius
    color: Theme.levelBackground(level)
    border.width: 1
    border.color: Qt.alpha(Theme.levelColor(level), Theme.dark ? 0.45 : 0.35)

    // A named group: the message is read when focus enters one of its actions.
    // (AlertMessage would surface as a separate window in UI Automation.)
    Accessible.role: Accessible.Grouping
    Accessible.name: [level === "error" ? qsTr("Error") : level === "warning" ? qsTr("Warning") : "",
                      title, text].filter(s => s.length > 0).join(". ")

    onVisibleChanged: Qt.callLater(announce)
    Accessible.onNameChanged: Qt.callLater(announce)

    GridLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 7
        anchors.rightMargin: root.closable ? 4 : 7
        columns: root.actionsBelow ? 3 : 4
        columnSpacing: 7
        rowSpacing: 4

        Icon {
            Layout.row: 0
            Layout.column: 0
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
            name: root.iconName
            color: Theme.levelColor(root.level)
            size: 14
        }

        ColumnLayout {
            Layout.row: 0
            Layout.column: 1
            Layout.fillWidth: true
            spacing: 1

            Text {
                Layout.fillWidth: true
                visible: root.title.length > 0
                text: root.title
                font.pixelSize: Theme.fontSize
                font.weight: Font.DemiBold
                color: Theme.text
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                visible: root.text.length > 0
                text: root.text
                font.pixelSize: Theme.fontSize
                color: Theme.text
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
        }

        Row {
            id: actionRow
            Layout.alignment: Qt.AlignVCenter
            Layout.row: root.actionsBelow ? 1 : 0
            Layout.column: root.actionsBelow ? 1 : 2
            spacing: 8
            visible: children.length > 0
        }

        AppButton {
            Layout.alignment: Qt.AlignTop
            Layout.row: 0
            Layout.column: root.actionsBelow ? 2 : 3
            visible: root.closable
            variant: "flat"
            iconName: "x"
            implicitWidth: 20
            implicitHeight: 20
            toolTipText: qsTr("Dismiss")
            onClicked: root.closed()
        }
    }
}
