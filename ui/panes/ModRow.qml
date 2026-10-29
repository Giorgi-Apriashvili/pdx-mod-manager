import QtQuick
import QtQuick.Controls.Basic
import QtQml.Models
import Pmm.Ui
import Pmm.Backend

// One playset entry in the center table (delegate of Backend.modView).
// Kept light: fixed height, plain-text cells, state read from model roles only.
Item {
    id: row

    required property var model
    required property int index

    property real gripWidth: 18
    property list<real> widths: [44, 30, 360, 90, 150]
    property bool reorderAllowed: true
    property bool listHasFocus: false
    property bool isCurrent: false
    property int selectionRevision: 0
    property ItemSelectionModel selectionModel
    property bool dragging: false

    // selectionRevision is read so the binding re-evaluates on selection changes.
    readonly property bool selected: selectionRevision >= 0 && selectionModel !== null
                                     && selectionModel.isSelected(Backend.modView.index(index, 0))
    readonly property bool modEnabled: model.enabled
    readonly property string status: model.status
    readonly property bool ready: status === "ready"

    signal rowPressed(int index, int modifiers, int button)
    signal toggleRequested(int index)
    signal contextMenuRequested(int index, real x, real y)

    height: Theme.rowHeight

    // Read when the row becomes current, so it carries everything the cells show.
    Accessible.role: Accessible.ListItem
    Accessible.name: qsTr("%1. %2, %3, %4, %5").arg(model.position).arg(model.name)
                     .arg(modEnabled ? qsTr("enabled") : qsTr("disabled")).arg(model.sourceLabel)
                     .arg(model.statusText)
    Accessible.description: model.hasWarnings ? Theme.lines(model.warnings).join(" ") : ""
    Accessible.selected: selected
    Accessible.selectable: true

    Rectangle {
        anchors.fill: parent
        color: row.selected ? (row.listHasFocus ? Theme.selection : Theme.selectionInactive)
             : hover.hovered ? Theme.hover
             : (row.index % 2 === 1 ? Theme.alternateBase : Theme.base)
    }

    // Keyboard focus: a 2 px ring inside the current row.
    Rectangle {
        anchors.fill: parent
        visible: row.isCurrent && row.listHasFocus
        color: "transparent"
        border.width: 2
        border.color: Theme.focus
        radius: 2
        z: 1
    }

    HoverHandler {
        id: hover
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        onPressed: mouse => row.rowPressed(row.index, mouse.modifiers, mouse.button)
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton)
                row.contextMenuRequested(row.index, mouse.x, mouse.y)
        }
    }

    Row {
        anchors.fill: parent

        // Drag handle (the drag itself is handled by the list overlay).
        Item {
            width: row.gripWidth
            height: parent.height
            Icon {
                anchors.centerIn: parent
                visible: row.reorderAllowed
                name: "grip"
                size: 12
                color: hover.hovered || row.selected ? Theme.textMuted : Theme.textFaint
                opacity: hover.hovered || row.selected ? 1 : 0.7
            }
        }

        // #
        Item {
            width: row.widths[0]
            height: parent.height
            clip: true
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: row.model.position
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSize
                font.features: { "tnum": 1 }
                color: Theme.textMuted
            }
        }

        // On
        Item {
            id: checkCell
            width: row.widths[1]
            height: parent.height

            Accessible.role: Accessible.CheckBox
            Accessible.name: qsTr("Enable %1").arg(row.model.name)
            Accessible.checkable: true
            Accessible.checked: row.modEnabled
            Accessible.onPressAction: row.toggleRequested(row.index)
            Accessible.onToggleAction: row.toggleRequested(row.index)

            Rectangle {
                anchors.left: parent.left
                anchors.leftMargin: 7
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                radius: 2
                color: row.modEnabled ? Theme.controlChecked : Theme.input
                border.width: row.modEnabled ? 0 : 1
                border.color: checkHover.hovered ? Theme.textMuted : Theme.controlBorder

                Icon {
                    anchors.centerIn: parent
                    visible: row.modEnabled
                    name: "check"
                    size: 12
                    strokeWidth: 1.8
                    color: Theme.accentText
                }
            }
            HoverHandler {
                id: checkHover
            }
            TapHandler {
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: row.toggleRequested(row.index)
            }
        }

        // Name, followed directly by the warnings indicator
        Item {
            width: row.widths[2]
            height: parent.height

            Text {
                id: nameText
                x: 6
                width: Math.min(implicitWidth, parent.width - 12 - (warnIcon.visible ? warnIcon.width : 0))
                anchors.verticalCenter: parent.verticalCenter
                text: row.model.name
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSize
                color: row.modEnabled ? Theme.text : Theme.textMuted
                elide: Text.ElideRight
            }
            Item {
                id: warnIcon
                x: nameText.x + nameText.width + 2
                width: 20
                height: parent.height
                visible: row.model.hasWarnings

                Accessible.role: Accessible.Indicator
                Accessible.name: qsTr("Warnings for %1").arg(row.model.name)
                Accessible.description: Theme.lines(row.model.warnings).join(" ")

                Icon {
                    anchors.centerIn: parent
                    name: "triangle-alert"
                    size: 13
                    color: Theme.warning
                }
                HoverHandler {
                    id: warnHover
                }
                ToolTip.visible: warnHover.hovered
                ToolTip.delay: 250
                ToolTip.text: Theme.lines(row.model.warnings).join("\n")
            }
        }

        // Source
        Item {
            width: row.widths[3]
            height: parent.height
            clip: true
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: row.model.sourceLabel
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSize
                color: Theme.textMuted
                elide: Text.ElideRight
            }
        }

        // Status: glyph + text, plus a thin bar while downloading
        Item {
            width: row.widths[4]
            height: parent.height
            clip: true

            Icon {
                id: statusIcon
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                name: Theme.statusIcon(row.status)
                size: 13
                color: Theme.statusColor(row.status)
            }
            Text {
                anchors.left: statusIcon.right
                anchors.leftMargin: 5
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: row.model.statusText
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSize
                font.weight: row.model.blocking ? Font.DemiBold : Font.Normal
                color: row.ready ? Theme.textMuted : Theme.statusColor(row.status)
                elide: Text.ElideRight
            }
            Rectangle {
                visible: row.model.progress >= 0
                anchors.left: statusIcon.left
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 2
                height: 2
                radius: 1
                color: Theme.dark ? "#33363c" : "#dfe1e5"

                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, row.model.progress))
                    height: parent.height
                    radius: 1
                    color: Theme.accent
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: row.dragging
        color: Qt.alpha(Theme.accent, 0.10)
    }
}
