import QtQuick

import dev.lorendb.kaon

// Stereo's segmented control: the chosen option sits on a raised pill.
Row {
    id: seg

    property var current
    property var options: [] // [{ id, label }]
    property bool shellStyle: false

    signal picked(var key)

    spacing: 2

    Repeater {
        model: seg.options

        Item {
            id: opt

            readonly property bool active: seg.current === modelData.id
            required property var modelData

            activeFocusOnTab: true
            height: 32
            width: optText.implicitWidth + 26

            Keys.onReturnPressed: seg.picked(modelData.id)
            Keys.onSpacePressed: seg.picked(modelData.id)

            Rectangle {
                anchors.fill: parent
                color: opt.active ? (seg.shellStyle ? Theme.shellDeep : Theme.glassRaised) : optMouse.containsMouse ? (
                                                                                                                          seg.shellStyle
                                                                                                                          ? Theme.shellDeep :
                                                                                                                            "#14171c") :
                                                                                                                      "transparent"
                radius: height / 2
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                border.color: Theme.ledBlue
                border.width: 2
                color: "transparent"
                radius: height / 2
                visible: opt.activeFocus
            }

            VText {
                id: optText

                anchors.centerIn: parent
                color: opt.active ? (seg.shellStyle ? Theme.ink : Theme.glassText) : (seg.shellStyle ? Theme.inkMuted :
                                                                                                       Theme.glassMuted)
                font.pixelSize: 13
                font.weight: Font.Bold
                text: opt.modelData.label
            }

            MouseArea {
                id: optMouse

                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onClicked: seg.picked(opt.modelData.id)
            }
        }
    }
}
