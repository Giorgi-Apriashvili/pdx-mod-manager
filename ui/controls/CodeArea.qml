import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Scrollable monospace text area for share codes (wraps anywhere).
ScrollView {
    id: root

    property alias text: area.text
    property alias placeholderText: area.placeholderText
    property alias readOnly: area.readOnly
    property alias textArea: area

    function selectAll() {
        area.selectAll()
    }

    implicitHeight: 80
    clip: true
    ScrollBar.vertical: AppScrollBar {
        parent: root
        x: root.width - width - 1
        y: 1
        height: root.height - 2
        active: true
    }
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    TextArea {
        id: area
        font.family: Theme.monoFamily
        font.pixelSize: Theme.fontSize
        wrapMode: TextEdit.WrapAnywhere
        selectByMouse: true
        persistentSelection: true
        color: Theme.text
        placeholderTextColor: Theme.textFaint
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        padding: 7
        rightPadding: 14
        Accessible.role: Accessible.EditableText
        Accessible.name: root.Accessible.name
        Keys.onTabPressed: event => {
            // Tab moves focus instead of inserting a tab character.
            nextItemInFocusChain(true).forceActiveFocus(Qt.TabFocusReason)
            event.accepted = true
        }
        Keys.onBacktabPressed: event => {
            nextItemInFocusChain(false).forceActiveFocus(Qt.BacktabFocusReason)
            event.accepted = true
        }

        background: Rectangle {
            color: area.readOnly ? Theme.pane : Theme.input
            radius: Theme.radius
            border.width: area.activeFocus ? 2 : 1
            border.color: area.activeFocus ? Theme.focus : Theme.controlBorder
        }
    }
}
