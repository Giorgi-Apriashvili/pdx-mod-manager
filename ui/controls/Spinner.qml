import QtQuick
import QtQuick.Shapes
import Pmm.Ui

// Small busy indicator (rotating arc).
Item {
    id: root

    property bool running: true
    property real size: 14
    property color color: Theme.accent

    implicitWidth: size
    implicitHeight: size
    visible: running
    Accessible.role: Accessible.Animation
    Accessible.name: qsTr("Working")

    Shape {
        id: arc
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: root.color
            strokeWidth: 1.8
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: root.width / 2 - 1.5
                radiusY: root.height / 2 - 1.5
                startAngle: 0
                sweepAngle: 270
            }
        }

        RotationAnimator on rotation {
            running: root.running && root.visible
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
        }
    }
}
