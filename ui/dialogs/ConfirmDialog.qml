import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Pmm.Ui

// Generic confirmation; `onConfirmed` runs the action. With `cancelIsDefault`
// (for what cannot be undone) Cancel has the focus, so Enter cancels as
// Escape does.
AppDialog {
    id: dialog

    property string message
    property string detail
    property string confirmText: qsTr("OK")
    property bool destructive: false
    property bool cancelIsDefault: false
    property var action: null

    preferredWidth: 420

    function ask(title: string, message: string, detail: string, confirmText: string,
                 destructive: bool, cancelIsDefault: bool, action: var) {
        dialog.title = title
        dialog.message = message
        dialog.detail = detail
        dialog.confirmText = confirmText
        dialog.destructive = destructive
        dialog.cancelIsDefault = cancelIsDefault
        dialog.action = action
        open()
    }

    onOpened: (dialog.cancelIsDefault ? cancelButton : confirmButton).forceActiveFocus()
    onClosed: action = null

    // Up, Down, Page Up, Page Down, Home and End on the buttons scroll a text
    // that is taller than the dialog: nothing in it takes the focus.
    function scrollText(key: int): bool {
        if (!scroller.interactive)
            return false
        const page = Math.max(20, scroller.height - 20)
        const bottom = scroller.contentHeight - scroller.height
        let y = scroller.contentY
        switch (key) {
        case Qt.Key_Up: y -= 40; break
        case Qt.Key_Down: y += 40; break
        case Qt.Key_PageUp: y -= page; break
        case Qt.Key_PageDown: y += page; break
        case Qt.Key_Home: y = 0; break
        case Qt.Key_End: y = bottom; break
        default: return false
        }
        scroller.contentY = Math.max(0, Math.min(bottom, y))
        return true
    }

    // Scrolls when a long list of files makes it taller than a small window.
    contentItem: Flickable {
        id: scroller
        objectName: "confirmScroller"
        implicitHeight: content.implicitHeight
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: content
            // Room for the scroll bar while there is one.
            width: scroller.width - (scroller.interactive ? 12 : 0)
            spacing: 6

            Text {
                Layout.fillWidth: true
                text: dialog.message
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSize
                color: Theme.text
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            Text {
                Layout.fillWidth: true
                visible: dialog.detail.length > 0
                text: dialog.detail
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontSize
                color: Theme.textMuted
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
    }

    footer: DialogFooter {
        Keys.onPressed: event => event.accepted = dialog.scrollText(event.key)

        AppButton {
            id: cancelButton
            objectName: "cancelButton"
            text: qsTr("Cancel")
            Accessible.description: dialog.cancelIsDefault ? dialog.message : ""
            onClicked: dialog.close()
        }
        AppButton {
            id: confirmButton
            objectName: "confirmButton"
            variant: dialog.destructive ? "danger" : "accent"
            text: dialog.confirmText
            // The question is read with the focused button (Cancel's, when it has the focus).
            Accessible.description: dialog.message
            onClicked: {
                const run = dialog.action
                dialog.close()
                if (typeof run === "function")
                    run()
            }
        }
    }
}
