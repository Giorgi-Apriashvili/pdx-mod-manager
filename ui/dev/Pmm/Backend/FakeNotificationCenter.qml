import QtQuick

QtObject {
    property var latest: ({})

    signal posted(string level, string title, string text)

    function post(level: string, title: string, text: string) {
        latest = { level: level, title: title, text: text }
        posted(level, title, text)
    }

    function dismiss() {
        latest = {}
    }
}
