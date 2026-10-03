import QtQuick

import dev.lorendb.kaon

// A status light. Blinks while something is in progress, like the light on a headset that's booting.
Item {
    id: led

    property bool blinking: false
    property color color: Theme.ledOff
    property real size: 10

    implicitHeight: size
    implicitWidth: size

    Rectangle {
        anchors.centerIn: parent
        color: led.color
        height: led.size * 2
        opacity: led.color === Theme.ledOff ? 0 : 0.22
        radius: height / 2
        width: led.size * 2
    }

    Rectangle {
        id: dot

        anchors.fill: parent
        color: led.color
        radius: width / 2

        SequentialAnimation on opacity {
            loops: Animation.Infinite
            running: led.blinking

            onStopped: dot.opacity = 1

            NumberAnimation {
                duration: 420
                to: 0.25
            }

            NumberAnimation {
                duration: 420
                to: 1
            }
        }
    }
}
