pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// Appearance and version information.
AppDialog {
    id: dialog

    title: qsTr("Settings")
    preferredWidth: 420

    contentItem: ColumnLayout {
        spacing: 8

        SectionHeader {
            Layout.fillWidth: true
            text: qsTr("Appearance")
        }

        ButtonGroup {
            id: themeGroup
        }
        Repeater {
            model: [
                { value: "system", text: qsTr("Follow system") },
                { value: "light", text: qsTr("Light") },
                { value: "dark", text: qsTr("Dark") }
            ]
            delegate: AppRadioButton {
                required property var modelData
                text: modelData.text
                ButtonGroup.group: themeGroup
                checked: Backend.settings.theme === modelData.value
                onClicked: Backend.settings.theme = modelData.value
            }
        }

        SectionHeader {
            Layout.fillWidth: true
            Layout.topMargin: 6
            text: qsTr("About")
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 3

            DetailLabel { text: qsTr("Application") }
            DetailValue { text: qsTr("Paradox Mod Manager %1").arg(Backend.appVersion) }
            DetailLabel { text: qsTr("Share-code protocol") }
            DetailValue { text: qsTr("Version %1 (PMM%1 codes)").arg(Backend.protocolVersion) }
            DetailLabel { text: qsTr("Platform") }
            DetailValue {
                text: Qt.platform.os === "windows" ? "Windows" : Qt.platform.os === "osx" ? "macOS"
                    : Qt.platform.os === "linux" ? "Linux" : Qt.platform.os
            }
            DetailLabel { text: qsTr("Data folder") }
            DetailValue {
                text: Backend.dataDir
                selectable: true
                elideMode: Text.ElideMiddle
                toolTipText: Backend.dataDir
            }
            DetailLabel { text: qsTr("Logs") }
            DetailValue {
                text: Backend.logsDir
                selectable: true
                elideMode: Text.ElideMiddle
                toolTipText: Backend.logsDir
            }
        }
        LinkButton {
            text: qsTr("Open logs folder")
            onClicked: Backend.openLogsFolder()
        }
        Text {
            Layout.fillWidth: true
            Layout.topMargin: 4
            text: qsTr("No account, server or telemetry. Playsets are stored locally on this computer.")
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
    }

    footer: DialogFooter {
        leading: [
            LinkButton {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: qsTr("Copy diagnostics")
                toolTipText: qsTr("Copy a redacted report to the clipboard")
                onClicked: Backend.launcher.copyDiagnostics()
            }
        ]
        AppButton {
            variant: "accent"
            text: qsTr("Close")
            onClicked: {
                Backend.settings.save()
                dialog.close()
            }
        }
    }
}
