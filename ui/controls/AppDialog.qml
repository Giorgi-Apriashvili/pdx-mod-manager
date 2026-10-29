import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Pmm.Ui

// Modal dialog frame: title bar with close button, themed surface and scrim.
// Content goes into the default property; set `footer: DialogFooter { ... }`.
Dialog {
    id: control

    property int preferredWidth: 460
    property int preferredHeight: 0
    property string subtitle

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(preferredWidth, parent ? parent.width - 32 : preferredWidth)
    height: preferredHeight > 0 ? Math.min(preferredHeight, parent ? parent.height - 32 : preferredHeight)
                                : implicitHeight
    modal: true
    focus: true
    padding: 14
    topPadding: 10
    closePolicy: Popup.CloseOnEscape
    font.pixelSize: Theme.fontSize

    // The popup item already reports the Dialog role; the title is its heading.
    background: Rectangle {
        color: Theme.pane
        radius: 6
        border.color: Theme.border

        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.margins: -1
            anchors.topMargin: 1
            anchors.bottomMargin: -4
            radius: 7
            color: Theme.dark ? "#40000000" : "#1f000000"
        }
    }

    header: Item {
        implicitHeight: titleColumn.implicitHeight + 18

        ColumnLayout {
            id: titleColumn
            anchors.left: parent.left
            anchors.right: closeButton.left
            anchors.leftMargin: 14
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: control.title
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Font.DemiBold
                color: Theme.text
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            Text {
                Layout.fillWidth: true
                visible: control.subtitle.length > 0
                text: control.subtitle
                font.pixelSize: Theme.fontSize
                color: Theme.textMuted
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }

        AppButton {
            id: closeButton
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.top: parent.top
            anchors.topMargin: 8
            variant: "flat"
            iconName: "x"
            toolTipText: qsTr("Close (Esc)")
            focusPolicy: Qt.NoFocus
            onClicked: control.reject()
        }
    }

    Overlay.modal: Rectangle {
        color: Theme.scrim
    }
}
