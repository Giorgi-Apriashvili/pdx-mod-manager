import QtQuick
import QtQuick.Controls.Basic
import Pmm.Ui

// Text-only action rendered like a hyperlink (secondary actions, banners).
AbstractButton {
    id: control

    property string toolTipText
    property color color: Theme.link

    implicitWidth: label.implicitWidth + leftPadding + rightPadding
    implicitHeight: Math.max(18, label.implicitHeight + topPadding + bottomPadding)
    padding: 1
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.pixelSize: Theme.fontSize

    Accessible.role: Accessible.Link
    Accessible.name: text
    Accessible.description: toolTipText

    ToolTip.visible: toolTipText.length > 0 && hovered
    ToolTip.text: toolTipText
    ToolTip.delay: 600

    contentItem: Text {
        id: label
        text: control.text
        textFormat: Text.PlainText
        font.pixelSize: control.font.pixelSize
        font.underline: control.hovered
        color: control.enabled ? control.color : Theme.textFaint
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    background: Item {
        FocusRing {
            visible: control.visualFocus
            inset: -1
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }
}
