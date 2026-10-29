import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Compact push button. `variant`: "normal" | "accent" | "danger" | "flat".
Button {
    id: control

    property string iconName
    property string variant: "normal"
    property string toolTipText
    property bool large: false
    property color iconColor: variant === "accent" ? Theme.accentText
                              : variant === "danger" ? Theme.danger : Theme.textMuted

    readonly property bool _accent: variant === "accent"
    readonly property bool _flat: variant === "flat"

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: large ? Theme.controlHeightLarge : Theme.controlHeight
    padding: 0
    leftPadding: text.length > 0 ? (iconName.length > 0 ? 7 : 10) : 5
    rightPadding: text.length > 0 ? 10 : 5
    spacing: 5
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.pixelSize: large ? Theme.fontSizeLarge : Theme.fontSize
    font.weight: _accent ? Font.DemiBold : Font.Normal

    Accessible.role: Accessible.Button
    Accessible.name: text.length > 0 ? text : toolTipText
    Accessible.description: toolTipText

    ToolTip.visible: toolTipText.length > 0 && (hovered || visualFocus)
    ToolTip.text: toolTipText
    ToolTip.delay: 600

    // Space is built in; Enter and Return activate a focused button as well.
    Keys.onReturnPressed: event => activate(event)
    Keys.onEnterPressed: event => activate(event)

    function activate(event: KeyEvent) {
        event.accepted = true
        if (enabled)
            clicked()
    }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        Row {
            id: row
            anchors.centerIn: parent
            spacing: control.spacing
            opacity: control.enabled ? 1 : 0.45

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: control.iconName
                color: control.iconColor
                size: control.large ? 15 : 13
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: text.length > 0
                text: control.text
                textFormat: Text.PlainText
                font: control.font
                color: control._accent ? Theme.accentText
                     : control.variant === "danger" ? Theme.danger : Theme.text
                elide: Text.ElideRight
            }
        }
    }

    background: Rectangle {
        implicitWidth: control.text.length > 0 ? 64 : Theme.controlHeight
        implicitHeight: Theme.controlHeight
        radius: Theme.radius
        color: {
            if (control._accent)
                return !control.enabled ? Theme.borderStrong
                     : control.down ? Theme.accentPressed
                     : control.hovered ? Theme.accentHover : Theme.accent
            if (control._flat)
                return control.down ? Theme.buttonPressed
                     : (control.hovered || control.checked) ? Theme.hover : "transparent"
            return control.down ? Theme.buttonPressed
                 : (control.hovered || control.checked) ? Theme.buttonHover : Theme.button
        }
        border.width: control._flat || control._accent ? 0 : 1
        border.color: Theme.border

        // Filled buttons get a 1 px gap so the ring never merges with the fill.
        FocusRing {
            visible: control.visualFocus
            inset: control._accent ? -3 : -2
        }
    }
}
