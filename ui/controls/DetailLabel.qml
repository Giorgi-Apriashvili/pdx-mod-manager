import QtQuick
import Pmm.Ui

// Key column of a details grid. Hidden from assistive technology: the value
// next to it carries the label in its accessible name (DetailValue.labelText).
Text {
    Accessible.ignored: true
    textFormat: Text.PlainText
    font.pixelSize: Theme.fontSize
    color: Theme.textMuted
}
