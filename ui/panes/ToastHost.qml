pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// Non-modal notifications driven by Backend.notifications.posted. Info toasts
// fade out after a few seconds; warnings and errors stay longer. A toast stays
// while hovered or while its Dismiss button has keyboard focus, and every toast
// is announced to screen readers (errors and warnings interrupt).
Item {
    id: root

    readonly property int maxToasts: 3

    // A toast was dismissed from the keyboard; focus should go back to the window.
    signal focusReleased()

    implicitWidth: 360
    implicitHeight: column.implicitHeight

    function push(level: string, title: string, text: string) {
        toasts.insert(0, { level: level, title: title, text: text, serial: ++serial })
        while (toasts.count > maxToasts)
            toasts.remove(toasts.count - 1, 1)
        const spoken = [title, text].filter(s => s.length > 0).join(". ")
        Accessible.announce(spoken, level === "error" || level === "warning" ? Accessible.Assertive
                                                                              : Accessible.Polite)
    }

    function dismiss(serial: int) {
        for (let i = 0; i < toasts.count; ++i) {
            if (toasts.get(i).serial === serial) {
                toasts.remove(i, 1)
                break
            }
        }
        if (toasts.count === 0)
            Backend.notifications.dismiss()
    }

    property int serial: 0

    ListModel {
        id: toasts
    }

    Connections {
        target: Backend.notifications
        function onPosted(level: string, title: string, text: string) {
            root.push(level, title, text)
        }
    }

    Column {
        id: column
        width: parent.width
        spacing: 6

        Repeater {
            model: toasts
            delegate: Rectangle {
                id: toast

                required property string level
                required property string title
                required property string text
                required property int serial

                width: column.width
                implicitHeight: content.implicitHeight + 16
                radius: 5
                color: Theme.dark ? "#2b2d32" : "#ffffff"
                border.color: Theme.border

                Accessible.role: Accessible.AlertMessage
                Accessible.name: title.length > 0 ? title : text
                Accessible.description: title.length > 0 ? text : ""

                Rectangle {
                    width: 3
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.margins: 6
                    x: 5
                    radius: 1.5
                    color: Theme.levelColor(toast.level)
                }

                Rectangle {
                    z: -1
                    anchors.fill: parent
                    anchors.topMargin: 2
                    anchors.bottomMargin: -3
                    anchors.leftMargin: -1
                    anchors.rightMargin: -1
                    radius: 6
                    color: Theme.dark ? "#55000000" : "#1a000000"
                }

                RowLayout {
                    id: content
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 16
                    anchors.rightMargin: 6
                    spacing: 8

                    Icon {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 1
                        name: Theme.levelIcon(toast.level)
                        color: Theme.levelColor(toast.level)
                        size: 14
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            visible: toast.title.length > 0
                            text: toast.title
                            textFormat: Text.PlainText
                            font.pixelSize: Theme.fontSize
                            font.weight: Font.DemiBold
                            color: Theme.text
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: toast.text.length > 0
                            text: toast.text
                            textFormat: Text.PlainText
                            font.pixelSize: Theme.fontSize
                            color: Theme.textMuted
                            wrapMode: Text.Wrap
                            maximumLineCount: 6
                            elide: Text.ElideRight
                        }
                    }
                    AppButton {
                        id: dismissButton
                        Layout.alignment: Qt.AlignTop
                        variant: "flat"
                        iconName: "x"
                        implicitWidth: 20
                        implicitHeight: 20
                        toolTipText: qsTr("Dismiss")
                        Accessible.name: qsTr("Dismiss notification")
                        onClicked: {
                            const focused = activeFocus
                            root.dismiss(toast.serial)
                            if (focused)
                                root.focusReleased()
                        }
                    }
                }

                HoverHandler {
                    id: toastHover
                }

                Timer {
                    interval: toast.level === "error" ? 15000 : toast.level === "warning" ? 9000 : 5000
                    running: !toastHover.hovered && !dismissButton.activeFocus
                    onTriggered: root.dismiss(toast.serial)
                }
            }
        }
    }
}
