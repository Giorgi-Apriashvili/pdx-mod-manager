import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Compact push button. `variant`: "normal" | "accent" | "danger" | "flat".
// `display: AbstractButton.IconOnly` hides the label (narrow toolbars); the
// text stays the accessible name. A label wider than the button is elided.
Button {
    id: control

    property string iconName
    property string variant: "normal"
    property string toolTipText
    property bool large: false
    property color iconColor: variant === "accent" ? Theme.accentText
                              : variant === "danger" ? Theme.danger : Theme.textMuted
    // false: drawn like a disabled button, but it keeps the keyboard focus and
    // still emits clicked(), so the handler can explain why nothing happens
    // (the WAI-ARIA aria-disabled pattern; Play while it is blocked).
    property bool available: true
    // The width the button needs with its label shown, also while `display`
    // hides it: toolbars use it to decide when to switch to icons.
    readonly property real labelledWidth: (iconName.length > 0 ? 7 + icon.size + spacing : 10)
                                          + label.implicitWidth + 10

    readonly property bool _accent: variant === "accent"
    readonly property bool _flat: variant === "flat"
    readonly property bool _active: enabled && available
    readonly property bool _showText: text.length > 0 && display !== AbstractButton.IconOnly

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: large ? Theme.controlHeightLarge : Theme.controlHeight
    padding: 0
    leftPadding: _showText ? (iconName.length > 0 ? 7 : 10) : 5
    rightPadding: _showText ? 10 : 5
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
        // From the natural label width, so eliding never feeds back into it.
        implicitWidth: (icon.visible ? icon.implicitWidth : 0) + (icon.visible && label.visible ? row.spacing : 0)
                       + (label.visible ? label.implicitWidth : 0)
        implicitHeight: Math.max(icon.implicitHeight, label.implicitHeight)

        Row {
            id: row
            anchors.centerIn: parent
            spacing: control.spacing
            opacity: control._active ? 1 : 0.45

            Icon {
                id: icon
                anchors.verticalCenter: parent.verticalCenter
                name: control.iconName
                color: control.iconColor
                size: control.large ? 15 : 13
            }
            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                // May use the padding; elided only 4 pixels from the edges.
                width: Math.max(0, Math.min(implicitWidth, control.width - 8
                                            - (icon.visible ? icon.width + row.spacing : 0)))
                visible: control._showText
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
        implicitWidth: control._showText ? 64 : Theme.controlHeight
        implicitHeight: Theme.controlHeight
        radius: Theme.radius
        color: {
            if (control._accent)
                return !control._active ? Theme.borderStrong
                     : control.down ? Theme.accentPressed
                     : control.hovered ? Theme.accentHover : Theme.accent
            if (!control._active)
                return control._flat ? "transparent" : Theme.button
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
