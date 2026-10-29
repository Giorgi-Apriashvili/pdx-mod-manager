import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui
import Pmm.Backend

// One game in the sidebar: name, installation status text and a status glyph.
// Unsupported games are shown muted and cannot be selected.
ItemDelegate {
    id: control

    required property var model

    readonly property bool current: model.key === Backend.games.currentKey
    readonly property bool supported: model.supported

    implicitHeight: 40
    leftPadding: 10
    rightPadding: 8
    topPadding: 4
    bottomPadding: 4
    hoverEnabled: true
    focusPolicy: supported ? Qt.StrongFocus : Qt.NoFocus

    Accessible.role: Accessible.ListItem
    Accessible.name: model.name + ", " + model.statusText
    Accessible.description: supported ? (current ? qsTr("Selected game") : qsTr("Press Enter to select"))
                                      : qsTr("Support for this game is not implemented yet")
    Accessible.selected: current

    ToolTip.visible: hovered && (!supported || model.installPath.length > 0)
    ToolTip.delay: 700
    ToolTip.text: supported ? model.installPath
                            : qsTr("Not supported yet: this game has no adapter, so it cannot be managed or launched.")

    onClicked: {
        if (supported)
            Backend.games.select(model.key)
    }
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    Keys.onUpPressed: nextItemInFocusChain(false).forceActiveFocus(Qt.TabFocusReason)
    Keys.onDownPressed: nextItemInFocusChain(true).forceActiveFocus(Qt.TabFocusReason)

    contentItem: Item {
        implicitHeight: 32

        Icon {
            id: glyph
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            size: 14
            name: !control.supported ? "circle-minus" : control.model.installed ? "check-circle" : "circle-alert"
            color: !control.supported ? Theme.textFaint
                 : control.model.installed ? Theme.success : Theme.warning
        }
        Column {
            anchors.left: glyph.right
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            Text {
                width: parent.width
                text: control.model.name
                font.pixelSize: Theme.fontSize
                font.weight: control.current ? Font.DemiBold : Font.Normal
                color: control.supported ? Theme.text : Theme.textMuted
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: control.model.statusText
                font.pixelSize: Theme.fontSizeSmall
                color: control.supported ? Theme.textMuted : Theme.textFaint
                font.italic: !control.supported
                elide: Text.ElideRight
            }
        }
    }

    background: Rectangle {
        color: control.current ? Theme.selection
             : (control.hovered && control.supported) ? Theme.hover : "transparent"
        radius: Theme.radius

        Rectangle {
            visible: control.current
            width: 3
            height: parent.height - 12
            anchors.verticalCenter: parent.verticalCenter
            radius: 1.5
            color: Theme.accent
        }

        FocusRing {
            visible: control.visualFocus
            inset: 1
        }
    }
}
