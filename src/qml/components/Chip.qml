import QtQuick

import dev.lorendb.kaon

// Stereo's toggle chip, set on the glass. A lit status light means the group is shown.
Item {
    id: chip

    property bool checked: false
    property int count: -1
    // Amber LED while something non-default is applied, even when the chip itself is unchecked
    property bool attention: false
    property color led: Theme.ledOff
    property bool showLed: true
    property string text

    signal toggled

    activeFocusOnTab: true
    implicitHeight: 32
    implicitWidth: row.implicitWidth + 26

    Keys.onReturnPressed: toggled()
    Keys.onSpacePressed: toggled()

    Rectangle {
        anchors.fill: parent
        border.color: chip.checked ? "transparent" : (mouse.containsMouse ? Theme.glassMuted : Theme.glassLine)
        border.width: 1.5
        color: chip.checked ? Theme.glassRaised : "transparent"
        radius: height / 2
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -4
        border.color: Theme.ledBlue
        border.width: 2
        color: "transparent"
        radius: height / 2
        visible: chip.activeFocus
    }

    Row {
        id: row

        anchors.centerIn: parent
        spacing: 8

        Led {
            anchors.verticalCenter: parent.verticalCenter
            color: chip.attention ? Theme.ledAmber : (chip.checked ? chip.led : Theme.ledOff)
            size: 7
            visible: chip.showLed || chip.attention
        }

        VText {
            anchors.verticalCenter: parent.verticalCenter
            color: chip.checked ? Theme.glassText : Theme.glassMuted
            font.pixelSize: 13
            font.weight: Font.Bold
            text: chip.text
        }

        VText {
            anchors.verticalCenter: parent.verticalCenter
            color: chip.checked ? Theme.glassMuted : Theme.glassFaint
            font.features: ({
                                "tnum": 1
                            })
            font.pixelSize: 13
            font.weight: Font.Bold
            text: chip.count
            visible: chip.count >= 0
        }
    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: chip.toggled()
    }
}
