import QtQuick
import QtQuick.Shapes

import dev.lorendb.kaon

// The button that sits in the visor's nose notch. Its ring is a status light, and during a launch it becomes the
// countdown. The label underneath is printed on the shell, like a legend on a hardware button.
Item {
    id: nb

    property string icon: "play"
    property string label
    property color led: Theme.ledGreen
    property string phase: ""
    property real progress: 1
    property int seconds: 0
    readonly property bool spinning: phase === "starting" || phase === "injecting"

    signal clicked

    activeFocusOnTab: true
    height: Theme.button
    width: Theme.button

    Keys.onReturnPressed: nb.clicked()
    Keys.onSpacePressed: nb.clicked()

    Rectangle {
        anchors.fill: parent
        anchors.margins: -5
        border.color: Theme.ledBlue
        border.width: 2
        color: "transparent"
        radius: width / 2
        visible: nb.activeFocus
    }

    Rectangle {
        anchors.fill: parent
        color: mouse.pressed ? "#1d2027" : mouse.containsMouse ? "#14161b" : Theme.glass
        radius: width / 2
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        RotationAnimation on rotation {
            duration: 1000
            from: 0
            loops: Animation.Infinite
            running: nb.spinning
            to: 360
        }

        ShapePath {
            fillColor: "transparent"
            strokeColor: Theme.glassLine
            strokeWidth: 3

            PathAngleArc {
                centerX: nb.width / 2
                centerY: nb.height / 2
                radiusX: nb.width / 2 - 5
                radiusY: nb.height / 2 - 5
                sweepAngle: 360
            }
        }

        ShapePath {
            capStyle: ShapePath.RoundCap
            fillColor: "transparent"
            strokeColor: nb.led
            strokeWidth: 3

            PathAngleArc {
                centerX: nb.width / 2
                centerY: nb.height / 2
                radiusX: nb.width / 2 - 5
                radiusY: nb.height / 2 - 5
                startAngle: -90
                sweepAngle: nb.spinning ? 90 : 360 * nb.progress
            }
        }
    }

    Icon {
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: nb.icon === "play" ? 2 : 0
        color: Theme.glassText
        name: nb.phase === "running" ? "check" : nb.icon
        size: nb.icon === "play" ? 22 : 20
        stroke: 2.2
        visible: nb.phase !== "waiting"
    }

    VText {
        anchors.centerIn: parent
        font.features: ({
                            "tnum": 1
                        })
        font.pixelSize: 19
        font.weight: Font.ExtraBold
        text: nb.seconds
        visible: nb.phase === "waiting"
    }

    VText {
        anchors.horizontalCenter: parent.horizontalCenter
        color: Theme.ink
        font.pixelSize: 11
        font.weight: Font.Bold
        text: nb.label
        y: parent.height + 3
    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: nb.clicked()
    }
}
