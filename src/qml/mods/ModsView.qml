import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// Every mod, two to a row on a wide window, with the version each one uses for every game.
Flickable {
    id: view

    readonly property real cardW: (width - 2 * Theme.pad - 16 * (cols - 1)) / cols
    readonly property int cols: width > 900 ? 2 : 1

    boundsBehavior: Flickable.StopAtBounds
    clip: true
    contentHeight: col.height + Theme.notchHeight + 50

    ScrollBar.vertical: GlassScrollBar {
    }

    Column {
        id: col

        spacing: 14
        width: view.width - 2 * Theme.pad
        x: Theme.pad
        y: 18

        VText {
            color: Theme.glassMuted
            font.pixelSize: 13
            text: "The version you pick for each mod is used by every game. Lights show what's on disk."
            width: parent.width
            wrapMode: Text.Wrap
        }

        Flow {
            spacing: 16
            width: parent.width

            Repeater {
                model: GameStatus.allMods()

                Rectangle {
                    id: card

                    required property var modelData
                    readonly property int stored: (GameStatus.revision, modelData.downloadedCount())

                    border.color: Theme.glassLine
                    border.width: 1.5
                    color: Theme.glassPanel
                    height: cardCol.height + 32
                    radius: 18
                    width: view.cardW

                    Led {
                        color: card.stored > 0 ? Theme.ledGreen : Theme.ledOff
                        x: 18
                        y: 24
                    }

                    Column {
                        id: cardCol

                        spacing: 6
                        width: parent.width - 56
                        x: 38
                        y: 14

                        Item {
                            height: 34
                            width: parent.width

                            VText {
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                font.pixelSize: 17
                                font.weight: Font.ExtraBold
                                text: card.modelData.name
                                width: parent.width - versions.width - 12
                            }

                            VersionMenu {
                                id: versions

                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                mod: card.modelData
                            }
                        }

                        VText {
                            color: Theme.glassMuted
                            font.pixelSize: 13
                            font.weight: Font.Normal
                            lineHeight: 1.3
                            linkColor: Theme.ledBlue
                            text: Theme.linkify(card.modelData.description + (card.modelData.info !== "" ? " "
                                                                                                           + card.modelData.info :
                                                                                                           ""))
                            textFormat: Text.StyledText
                            width: parent.width
                            wrapMode: Text.Wrap

                            onLinkActivated: link => Qt.openUrlExternally(link)

                            HoverHandler {
                                cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
                            }
                        }

                        VText {
                            color: Theme.glassFaint
                            font.pixelSize: 12
                            text: (card.stored === 0 ? "Nothing on disk" : card.stored === 1 ? "1 version on disk" :
                                                                                               card.stored
                                                                                               + " versions on disk") + (
                                      card.modelData.providesVr ? "" : ". Installed into each game that needs it.")
                            width: parent.width
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
