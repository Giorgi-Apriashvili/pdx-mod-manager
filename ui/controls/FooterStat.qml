import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Status bar item: optional glyph plus short text. With `compact` (a narrow
// footer) it shows `shortText` instead, if set; `text` stays its accessible
// name and becomes its tool tip.
Row {
    id: root

    property string iconName
    property color iconColor: Theme.textMuted
    property string text
    property string shortText
    property bool compact: false
    property bool strong: false
    // The width with `text` shown, also while compact: the footer uses it to
    // decide when to switch to the short texts.
    readonly property real fullWidth: (icon.visible ? icon.width + spacing : 0) + metrics.advanceWidth

    readonly property bool _short: compact && shortText.length > 0

    spacing: 4
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    ToolTip.visible: _short && hover.hovered
    ToolTip.text: text
    ToolTip.delay: 600

    HoverHandler {
        id: hover
    }

    TextMetrics {
        id: metrics
        font: label.font
        text: root.text
    }

    Icon {
        id: icon
        anchors.verticalCenter: parent.verticalCenter
        name: root.iconName
        size: 12
        color: root.iconColor
    }
    Text {
        id: label
        anchors.verticalCenter: parent.verticalCenter
        text: root._short ? root.shortText : root.text
        textFormat: Text.PlainText
        font.pixelSize: Theme.fontSizeSmall
        font.weight: root.strong ? Font.DemiBold : Font.Normal
        color: root.strong ? Theme.text : Theme.textMuted
    }
}
