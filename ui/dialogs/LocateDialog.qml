import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQml
import Pmm.Ui
import Pmm.Backend

// Manual installation / user-data folder selection for the current game.
AppDialog {
    id: dialog

    property string gameKey
    property string gameName
    property url installUrl
    property url userDataUrl
    property string currentInstall
    property string currentUserData
    property bool currentManual

    title: qsTr("Locate %1").arg(gameName)
    subtitle: qsTr("Use this when the game is installed outside your Steam libraries or was not detected.")
    preferredWidth: 560

    component GameRow: QtObject {
        required property string key
        required property string installPath
        required property string userDataPath
        required property bool manualLocation
    }

    // Display paths of `key`, read from the games model rows.
    function find(key: string): GameRow {
        for (let i = 0; i < gameRows.count; ++i) {
            const row = gameRows.objectAt(i) as GameRow
            if (row && row.key === key)
                return row
        }
        return null
    }

    onAboutToShow: {
        gameKey = Backend.games.currentKey
        gameName = Backend.games.currentName
        installUrl = ""
        userDataUrl = ""
        const row = find(gameKey)
        currentInstall = row ? row.installPath : ""
        currentUserData = row ? row.userDataPath : ""
        currentManual = row ? row.manualLocation : false
    }

    onOpened: installBrowse.forceActiveFocus(Qt.TabFocusReason)

    Instantiator {
        id: gameRows
        model: Backend.games
        delegate: GameRow {}
    }

    FolderDialog {
        id: installPicker
        title: qsTr("Select the %1 installation folder").arg(dialog.gameName)
        onAccepted: dialog.installUrl = selectedFolder
    }
    FolderDialog {
        id: userDataPicker
        title: qsTr("Select the %1 user-data folder").arg(dialog.gameName)
        onAccepted: dialog.userDataUrl = selectedFolder
    }

    contentItem: GridLayout {
        columns: 3
        columnSpacing: 8
        rowSpacing: 6

        Text {
            Layout.columnSpan: 3
            text: qsTr("Installation folder (contains the game executable)")
            font.pixelSize: Theme.fontSize
            font.weight: Font.DemiBold
            color: Theme.text
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        AppTextField {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            readOnly: true
            text: dialog.installUrl.toString().length > 0 ? Theme.urlToPath(dialog.installUrl) : dialog.currentInstall
            placeholderText: qsTr("Not set")
            Accessible.name: qsTr("Installation folder")
        }
        AppButton {
            id: installBrowse
            text: qsTr("Browse…")
            iconName: "folder"
            Accessible.name: qsTr("Browse for the installation folder")
            onClicked: installPicker.open()
        }

        Text {
            Layout.columnSpan: 3
            Layout.topMargin: 6
            text: qsTr("User-data folder (mods, settings and saves)")
            font.pixelSize: Theme.fontSize
            font.weight: Font.DemiBold
            color: Theme.text
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        AppTextField {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            readOnly: true
            text: dialog.userDataUrl.toString().length > 0 ? Theme.urlToPath(dialog.userDataUrl) : dialog.currentUserData
            placeholderText: qsTr("Default location for this operating system")
            Accessible.name: qsTr("User-data folder")
        }
        AppButton {
            text: qsTr("Browse…")
            iconName: "folder"
            Accessible.name: qsTr("Browse for the user-data folder")
            onClicked: userDataPicker.open()
        }
        Text {
            Layout.columnSpan: 3
            Layout.fillWidth: true
            text: qsTr("Leave the user-data folder unchanged to use the default location.")
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textMuted
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }

    footer: DialogFooter {
        leading: [
            LinkButton {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                visible: dialog.currentManual
                text: qsTr("Reset to automatic detection")
                toolTipText: qsTr("Forget the manual folders and use the detected Steam libraries")
                onClicked: {
                    Backend.games.clearManualLocation(dialog.gameKey)
                    dialog.close()
                }
            }
        ]
        AppButton {
            text: qsTr("Cancel")
            onClicked: dialog.close()
        }
        AppButton {
            variant: "accent"
            text: qsTr("Use these folders")
            enabled: dialog.installUrl.toString().length > 0
            onClicked: {
                if (Backend.games.locate(dialog.gameKey, dialog.installUrl, dialog.userDataUrl))
                    dialog.close()
            }
        }
    }
}
