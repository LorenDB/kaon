import QtQuick

import dev.lorendb.kaon

// Two families of button: on the shell (ink on plastic) and on the glass (white on black).
Item {
    id: b

    readonly property bool hovered: mouse.containsMouse
    property string icon
    // No outline until the pointer is on it, for an action that should stay out of the way
    property bool quiet: false
    // Destructive confirm: red fill, light text. Takes priority over solid's white pill.
    property bool danger: false
    property bool shellStyle: false
    property bool small: false
    property bool solid: false
    property string text

    signal clicked

    activeFocusOnTab: true
    implicitHeight: small ? 32 : 40
    implicitWidth: (text === "" ? 0 : label.implicitWidth) + (icon !== "" ? 22 : 0) + (text === "" ? 0 : (icon !== "" ? 8 :
                                                                                                                        0)) + (small
                                                                                                                               ? 26 : 32)
    opacity: enabled ? 1 : 0.4

    Keys.onReturnPressed: b.clicked()
    Keys.onSpacePressed: b.clicked()

    Rectangle {
        anchors.fill: parent
        border.color: b.danger || b.solid || (b.quiet && !mouse.containsMouse && !b.activeFocus) ? "transparent" : (!b.shellStyle ? (
                                                                                                                            mouse.containsMouse
                                                                                                                            ? Theme.glassMuted :
                                                                                                                              Theme.glassLine) :
                                                                                                                        (mouse.containsMouse
                                                                                                                         ? Theme.inkMuted :
                                                                                                                           Theme.shellLine))
        border.width: 1.5
        color: b.danger ? (mouse.containsMouse ? "#ff6e66" : Theme.ledRed) : b.solid ? (!b.shellStyle ? (mouse.containsMouse ? "#ffffff" : Theme.glassText) : (mouse.containsMouse
                                                                                                 ? "#2a2d33" : Theme.ink)) : (
                             !b.shellStyle ? (mouse.containsMouse ? Theme.glassRaised : "transparent") : (mouse.containsMouse
                                                                                                          ? Theme.shellDeep :
                                                                                                            "transparent"))
        radius: height / 2
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -4
        border.color: Theme.ledBlue
        border.width: 2
        color: "transparent"
        radius: height / 2
        visible: b.activeFocus
    }

    Row {
        anchors.centerIn: parent
        spacing: 8

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            color: label.color
            name: b.icon
            size: b.small ? 16 : 18
            stroke: 2
            visible: b.icon !== ""
        }

        VText {
            id: label

            anchors.verticalCenter: parent.verticalCenter
            color: b.danger ? Theme.glassText : b.solid ? (!b.shellStyle ? Theme.glass : Theme.shell) : b.quiet && !mouse.containsMouse ? (!b.shellStyle
                                                                                                              ? Theme.glassMuted :
                                                                                                                Theme.inkMuted) :
                                                                                                             (!b.shellStyle
                                                                                                              ? Theme.glassText :
                                                                                                                Theme.ink)
            font.pixelSize: b.small ? 13 : 14
            font.weight: Font.Bold
            text: b.text
            visible: b.text !== ""
        }
    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: b.clicked()
    }
}
