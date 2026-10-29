import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Pmm.Ui

// Value column of a details grid; optionally selectable for copying ids/paths.
Item {
    id: root

    property string text
    property bool selectable: false
    property bool monospace: false
    property int elideMode: Text.ElideRight
    property string toolTipText

    Layout.fillWidth: true
    implicitHeight: Math.max(label.implicitHeight, edit.implicitHeight)
    implicitWidth: Math.min(label.implicitWidth, 80)
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    Text {
        id: label
        anchors.fill: parent
        visible: !root.selectable
        text: root.text
        textFormat: Text.PlainText
        font.pixelSize: Theme.fontSize
        font.family: root.monospace ? Theme.monoFamily : Theme.uiFamily
        color: Theme.text
        elide: root.elideMode
        verticalAlignment: Text.AlignVCenter
    }

    TextEdit {
        id: edit
        anchors.fill: parent
        visible: root.selectable
        text: root.text
        textFormat: TextEdit.PlainText
        readOnly: true
        selectByMouse: true
        font.pixelSize: Theme.fontSize
        font.family: root.monospace ? Theme.monoFamily : Theme.uiFamily
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        verticalAlignment: TextEdit.AlignVCenter
        activeFocusOnPress: true
    }

    HoverHandler {
        id: hover
    }
    ToolTip.visible: root.toolTipText.length > 0 && hover.hovered && label.truncated
    ToolTip.text: root.toolTipText
    ToolTip.delay: 500
}
