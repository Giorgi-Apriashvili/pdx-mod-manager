import QtQuick
import QtQuick.Layouts
import Pmm.Ui

// Row of the "Add mods" list; entries already in the playset are marked and locked.
Rectangle {
    id: row

    required property var model
    required property int index

    property bool checked: false
    property bool isCurrent: false
    readonly property bool inPlayset: model.inPlayset

    signal toggleRequested()

    implicitHeight: model.problems.length > 0 ? 40 : 26
    color: checked ? Theme.selection : hover.hovered && !inPlayset ? Theme.hover
         : index % 2 === 1 ? Theme.alternateBase : Theme.base

    Accessible.role: Accessible.CheckBox
    Accessible.name: model.name + (inPlayset ? qsTr(", already in playset") : "")
    Accessible.description: [model.version, model.sourceLabel, model.stateText, model.problems]
                            .filter(s => s && s.length > 0).join(", ")
    Accessible.checkable: !inPlayset
    Accessible.checked: checked || inPlayset
    Accessible.onToggleAction: toggleRequested()
    Accessible.onPressAction: toggleRequested()

    HoverHandler {
        id: hover
    }
    TapHandler {
        onTapped: row.toggleRequested()
    }

    // Keyboard focus: a 2 px ring inside the current row.
    Rectangle {
        anchors.fill: parent
        z: 1
        visible: row.isCurrent
        color: "transparent"
        border.width: 2
        border.color: Theme.focus
        radius: 2
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 10
        spacing: 8

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: 14
            implicitHeight: 14
            radius: 2
            color: row.inPlayset ? Theme.borderStrong : row.checked ? Theme.controlChecked : Theme.input
            border.width: row.checked || row.inPlayset ? 0 : 1
            border.color: Theme.controlBorder
            Icon {
                anchors.centerIn: parent
                visible: row.checked || row.inPlayset
                name: "check"
                size: 12
                strokeWidth: 1.8
                color: Theme.accentText
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: row.model.name
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSize
                color: row.inPlayset ? Theme.textMuted : Theme.text
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.fillWidth: true
                visible: row.model.problems.length > 0
                spacing: 4
                Icon {
                    name: "triangle-alert"
                    color: Theme.warning
                    size: 11
                }
                Text {
                    Layout.fillWidth: true
                    text: row.model.problems
                    textFormat: Text.PlainText
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.warning
                    elide: Text.ElideRight
                }
            }
        }

        Chip {
            visible: row.inPlayset
            text: qsTr("In playset")
        }
        Text {
            Layout.preferredWidth: 60
            text: row.model.version
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textMuted
            elide: Text.ElideRight
        }
        Text {
            Layout.preferredWidth: 64
            text: row.model.sourceLabel
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textMuted
            elide: Text.ElideRight
        }
        Text {
            Layout.preferredWidth: 110
            text: row.model.stateText
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textMuted
            elide: Text.ElideRight
        }
    }
}
