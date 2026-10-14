import QtQuick

import dev.lorendb.kaon

// Stereo's segmented control: the chosen option sits on a raised pill.
// Flow so long choice lists (Downscaler, DX11 upscaler, and the like) wrap in a narrow form.
Flow {
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
            readonly property real naturalWidth: optText.implicitWidth + 26
            required property var modelData

            activeFocusOnTab: true
            height: 32
            // Cap to the Flow width so a long label cannot force the row past the form.
            width: seg.width > 0 ? Math.min(naturalWidth, seg.width) : naturalWidth

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

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.durationFast
                    }
                }
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
                elide: Text.ElideRight
                font.pixelSize: 13
                font.weight: Font.Bold
                text: opt.modelData.label
                width: Math.min(implicitWidth, parent.width - 16)
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
