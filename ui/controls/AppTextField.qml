import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Compact single-line input with an optional leading icon and clear button.
TextField {
    id: control

    property string iconName
    property bool clearable: false

    signal cleared()

    implicitWidth: 160
    implicitHeight: Theme.controlHeight
    leftPadding: iconName.length > 0 ? 24 : 7
    rightPadding: clearButton.visible ? 22 : 7
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    font.pixelSize: Theme.fontSize
    color: enabled ? Theme.text : Theme.textFaint
    placeholderTextColor: Theme.textFaint
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    selectByMouse: true

    Accessible.role: Accessible.EditableText
    Accessible.name: placeholderText

    background: Rectangle {
        radius: Theme.radius
        color: control.enabled ? Theme.input : Theme.pane
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.focus
                    : control.hovered ? Theme.borderStrong : Theme.border

        Icon {
            x: 7
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            color: Theme.textMuted
            size: 13
        }
    }

    AbstractButton {
        id: clearButton
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        padding: 3
        visible: control.clearable && control.text.length > 0
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Clear")
        onClicked: {
            control.clear()
            control.cleared()
            control.forceActiveFocus()
        }
        background: Rectangle {
            radius: 8
            color: clearButton.hovered ? Theme.buttonPressed : "transparent"
        }
        contentItem: Icon {
            name: "x"
            size: 10
            strokeWidth: 1.6
            color: Theme.textMuted
        }
    }
}
