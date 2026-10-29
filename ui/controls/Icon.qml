import QtQuick
import QtQuick.Shapes
import Pmm.Ui
import "IconPaths.js" as IconPaths

// Resolution-independent icon drawn from IconPaths (16x16 design grid).
Item {
    id: root

    property string name
    property color color: Theme.text
    property real size: Theme.iconSize
    property real strokeWidth: size <= 12 ? 1.35 : 1.5

    implicitWidth: size
    implicitHeight: size
    visible: name.length > 0
    Accessible.ignored: true

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            scale: Qt.size(root.width / 16, root.height / 16)
            strokeColor: IconPaths.strokePath(root.name).length > 0 ? root.color : "transparent"
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: IconPaths.strokePath(root.name) }
        }
        ShapePath {
            scale: Qt.size(root.width / 16, root.height / 16)
            strokeColor: "transparent"
            fillColor: IconPaths.fillPath(root.name).length > 0 ? root.color : "transparent"
            PathSvg { path: IconPaths.fillPath(root.name) }
        }
    }
}
