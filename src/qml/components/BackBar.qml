import QtQuick

import dev.lorendb.kaon

// The way back from a page that was opened from another page. It stays where it is while the page scrolls under it,
// and once the page's own title has scrolled away it carries that title itself.
Item {
    id: bar

    // The page this goes back to
    property string label: "Library"
    // A status light in front of the title. Left transparent, there is none.
    property color led: "transparent"
    // Set once the page's title has gone under the bar
    property bool raised: false
    property string title

    signal back

    height: 60

    Rectangle {
        anchors.fill: parent
        color: Theme.glass
        opacity: bar.raised ? 1 : 0

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.durationFast
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            color: Theme.glassLine
            height: 1
            width: parent.width
        }
    }

    // What has scrolled under the bar can't be seen, so it must not be clicked either. The wheel still gets through.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        visible: bar.raised
    }

    VButton {
        id: backButton

        icon: "back"
        small: true
        text: bar.label
        x: 14
        y: 14

        onClicked: bar.back()

        // Keeps the button readable on top of a game's art
        Rectangle {
            anchors.fill: parent
            color: "#99050608"
            radius: height / 2
            z: -1
        }
    }

    Row {
        anchors.left: backButton.right
        anchors.leftMargin: 16
        anchors.right: parent.right
        anchors.rightMargin: 24
        anchors.verticalCenter: backButton.verticalCenter
        opacity: bar.raised ? 1 : 0
        spacing: 10

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.durationFast
            }
        }

        Led {
            id: light

            anchors.verticalCenter: parent.verticalCenter
            color: bar.led
            size: 9
            visible: bar.led.a > 0
        }

        VText {
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            font.pixelSize: 16
            font.weight: Font.ExtraBold
            text: bar.title
            width: parent.width - (light.visible ? 19 : 0)
        }
    }
}
