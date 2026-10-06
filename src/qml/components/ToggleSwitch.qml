import QtQuick

import dev.lorendb.kaon

// Stereo's switch: the whole track fills when it's on. Here the fill is the ready light, so "on" reads as a status.
Item {
    id: sw

    property bool checked
    property bool enabled: true
    property bool shellStyle: false

    signal toggled

    activeFocusOnTab: enabled
    implicitHeight: 24
    implicitWidth: 42
    opacity: enabled ? 1 : 0.45

    Keys.onReturnPressed: if (sw.enabled)
                              sw.toggled()
    Keys.onSpacePressed: if (sw.enabled)
                             sw.toggled()

    Rectangle {
        anchors.fill: parent
        color: sw.checked ? Theme.ledGreen : sw.shellStyle ? Theme.shellLine : Theme.glassLine
        radius: height / 2

        Behavior on color {
            ColorAnimation {
                duration: 140
            }
        }

        Rectangle {
            color: sw.checked ? Theme.glass : sw.shellStyle ? Theme.inkMuted : Theme.glassMuted
            height: parent.height - 6
            radius: height / 2
            width: height
            x: sw.checked ? parent.width - width - 3 : 3
            y: 3

            Behavior on x {
                NumberAnimation {
                    duration: 140
                    easing.type: Easing.OutCubic
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -4
        border.color: Theme.ledBlue
        border.width: 2
        color: "transparent"
        radius: height / 2
        visible: sw.activeFocus
    }

    MouseArea {
        anchors.fill: parent
        anchors.margins: -4
        cursorShape: sw.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor

        onClicked: if (sw.enabled)
                       sw.toggled()
    }
}
