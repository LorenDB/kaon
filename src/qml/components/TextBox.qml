import QtQuick

import dev.lorendb.kaon

// A text field set into the glass.
Rectangle {
    id: box

    property alias input: input
    property string placeholder
    property alias text: input.text

    border.color: input.activeFocus ? Theme.glassMuted : Theme.glassLine
    border.width: 1.5
    color: Theme.glassPanel
    implicitHeight: 38
    implicitWidth: 320
    radius: 12

    TextInput {
        id: input

        activeFocusOnTab: true
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        clip: true
        color: Theme.glassText
        font.family: Theme.font
        font.pixelSize: 14
        selectByMouse: true
        selectedTextColor: Theme.glass
        selectionColor: Theme.glassMuted

        VText {
            color: Theme.glassFaint
            font.pixelSize: 14
            text: box.placeholder
            visible: input.text === "" && !input.activeFocus
        }
    }
}
