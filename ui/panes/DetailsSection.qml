pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Pmm.Ui
import Pmm.Backend

// Details of the selected mod: identity, versions, tags, dependencies, problems.
ColumnLayout {
    id: root

    readonly property var details: Backend.details
    readonly property list<string> warningLines: Theme.lines(details.warnings)
    readonly property list<string> problemLines: Theme.lines(details.problems)
    readonly property list<string> tagList: Theme.lines(details.tags)

    spacing: 6

    function dependencyIcon(state: string): string {
        switch (state) {
        case "ok": return "check-circle"
        case "missing": return "circle-alert"
        case "not_in_playset": return "circle-minus"
        case "disabled": return "circle-minus"
        case "order": return "arrow-up"
        default: return "circle-help"
        }
    }

    function dependencyColor(state: string): color {
        return state === "ok" ? Theme.success : state === "missing" ? Theme.danger : Theme.warning
    }

    SectionHeader {
        Layout.fillWidth: true
        text: qsTr("Selected mod")
    }

    Text {
        Layout.fillWidth: true
        visible: !root.details.hasSelection
        text: qsTr("Select a mod in the list to see its details.")
        font.pixelSize: Theme.fontSize
        color: Theme.textMuted
        wrapMode: Text.Wrap
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: root.details.hasSelection
        spacing: 6

        Text {
            Layout.fillWidth: true
            text: root.details.name
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontSizeLarge
            font.weight: Font.DemiBold
            color: Theme.text
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 3

            DetailLabel { id: statusKey; text: qsTr("Status") }
            DetailValue { labelText: statusKey.text; text: root.details.statusText }

            DetailLabel { id: sourceKey; text: qsTr("Source") }
            DetailValue { labelText: sourceKey.text; text: root.details.sourceLabel }

            DetailLabel {
                id: idKey
                text: root.details.source === "local" ? qsTr("Folder ID") : qsTr("Workshop ID")
            }
            DetailValue {
                labelText: idKey.text
                text: root.details.providerId
                selectable: true
                monospace: true
            }

            DetailLabel { id: versionKey; text: qsTr("Version") }
            DetailValue {
                labelText: versionKey.text
                text: root.details.version.length > 0 ? root.details.version : qsTr("unknown")
            }

            DetailLabel { id: supportsKey; text: qsTr("Supports") }
            DetailValue {
                labelText: supportsKey.text
                text: root.details.supportedVersion.length > 0 ? root.details.supportedVersion : qsTr("not declared")
            }

            DetailLabel {
                id: locationKey
                visible: root.details.contentPath.length > 0
                text: qsTr("Location")
            }
            DetailValue {
                visible: root.details.contentPath.length > 0
                labelText: locationKey.text
                text: root.details.contentPath
                elideMode: Text.ElideMiddle
                toolTipText: root.details.contentPath
            }

            DetailLabel {
                id: tagsKey
                visible: root.tagList.length > 0
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 1
                text: qsTr("Tags")
            }
            Flow {
                Layout.fillWidth: true
                visible: root.tagList.length > 0
                spacing: 3
                Accessible.role: Accessible.StaticText
                Accessible.name: tagsKey.text + ": " + root.tagList.join(", ")
                Repeater {
                    model: root.tagList
                    delegate: Chip {
                        required property string modelData
                        text: modelData
                        Accessible.ignored: true
                    }
                }
            }
        }

        // Dependencies
        Text {
            Layout.topMargin: 2
            text: qsTr("Dependencies")
            font.pixelSize: Theme.fontSizeSmall
            font.weight: Font.DemiBold
            color: Theme.textMuted
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
        Text {
            Layout.fillWidth: true
            visible: root.details.dependencies.length === 0
            text: qsTr("None declared.")
            font.pixelSize: Theme.fontSize
            color: Theme.textFaint
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        Repeater {
            model: root.details.dependencies
            delegate: RowLayout {
                id: dep
                required property var modelData
                Layout.fillWidth: true
                spacing: 6
                Accessible.role: Accessible.StaticText
                Accessible.name: dep.modelData.name + ": " + dep.modelData.text

                Icon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 1
                    name: root.dependencyIcon(dep.modelData.state)
                    color: root.dependencyColor(dep.modelData.state)
                    size: 13
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        text: dep.modelData.name
                        font.pixelSize: Theme.fontSize
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: dep.modelData.text
                        font.pixelSize: Theme.fontSizeSmall
                        color: dep.modelData.state === "ok" ? Theme.textMuted : root.dependencyColor(dep.modelData.state)
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        // Warnings and problems
        Repeater {
            model: root.problemLines
            delegate: MessageLine {
                required property string modelData
                Layout.fillWidth: true
                level: "error"
                text: modelData
            }
        }
        Repeater {
            model: root.warningLines
            delegate: MessageLine {
                required property string modelData
                Layout.fillWidth: true
                level: "warning"
                text: modelData
            }
        }

        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 2
            spacing: 4

            AppButton {
                text: qsTr("Open Workshop page")
                iconName: "external"
                visible: root.details.source !== "local"
                enabled: root.details.workshopUrl.length > 0
                toolTipText: root.details.workshopUrl
                onClicked: root.details.openWorkshopPage()
            }
            AppButton {
                text: qsTr("Open folder")
                iconName: "folder"
                enabled: root.details.canOpenFolder
                toolTipText: root.details.canOpenFolder ? root.details.contentPath : qsTr("The mod is not installed")
                onClicked: root.details.openContentFolder()
            }
        }
    }
}
