# Qt Quick presentation layer: static QML module "Pmm.Ui" (docs/ui-contract.md).
# The backend singleton "Pmm.Backend" is registered at run time by src/app, so
# it is not a build-time dependency of this module. ui/dev holds a QML-only
# stand-in used for previews, screenshots and linting; it is never built here.
set(CMAKE_AUTOMOC ON) # QML type registration needs moc metatypes
qt_add_library(pmm_ui STATIC)

# Generate a qmldir in controls/, panes/ and dialogs/ so files there resolve the
# module's types like files at the module root.
qt_policy(SET QTP0004 NEW)

set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(pmm_ui
  URI Pmm.Ui
  VERSION 1.0
  # A Pmm/Ui folder in the build tree lets the qml tool and qmllint import the
  # module directly (see ui/README.md).
  OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/Pmm/Ui"
  QML_FILES
    Main.qml
    Theme.qml
    controls/AppButton.qml
    controls/AppCheckBox.qml
    controls/AppComboBox.qml
    controls/AppDialog.qml
    controls/AppMenu.qml
    controls/AppMenuItem.qml
    controls/AppMenuSeparator.qml
    controls/AppProgressBar.qml
    controls/AppRadioButton.qml
    controls/AppScrollBar.qml
    controls/AppTextField.qml
    controls/Banner.qml
    controls/Chip.qml
    controls/CodeArea.qml
    controls/DetailLabel.qml
    controls/DetailValue.qml
    controls/DialogFooter.qml
    controls/EmptyState.qml
    controls/FocusRing.qml
    controls/FooterStat.qml
    controls/Icon.qml
    controls/IconPaths.js
    controls/LinkButton.qml
    controls/MessageLine.qml
    controls/PreviewCell.qml
    controls/SectionHeader.qml
    controls/Spinner.qml
    controls/StatTile.qml
    panes/DetailsSection.qml
    panes/DownloadsSection.qml
    panes/GameDelegate.qml
    panes/GamesPane.qml
    panes/JobDelegate.qml
    panes/ModColumnHeader.qml
    panes/ModRow.qml
    panes/ModsPane.qml
    panes/PlaySection.qml
    panes/PlaysetSection.qml
    panes/SidePane.qml
    panes/ToastHost.qml
    dialogs/AddModsDialog.qml
    dialogs/ConfirmDialog.qml
    dialogs/ExportDialog.qml
    dialogs/HistoryDialog.qml
    dialogs/ImportDialog.qml
    dialogs/InventoryRow.qml
    dialogs/LocateDialog.qml
    dialogs/NameDialog.qml
    dialogs/SettingsDialog.qml
)

target_link_libraries(pmm_ui PRIVATE Qt6::Quick Qt6::QuickControls2)
add_library(PMM::ui ALIAS pmm_ui)
