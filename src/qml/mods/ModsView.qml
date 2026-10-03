import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// Mods grouped by engine. Each group stacks cards down columns, so a tall card doesn't leave a gap under a short one.
Flickable {
    id: view

    readonly property int cols: width > 900 ? 2 : 1
    readonly property var engines: [
        {
            "label": "Unreal Engine",
            "engine": Game.Unreal
        },
        {
            "label": "Unity",
            "engine": Game.Unity
        },
        {
            "label": "Source",
            "engine": Game.Source
        },
        {
            "label": "Godot",
            "engine": Game.Godot
        },
        {
            "label": "Other",
            "engine": 0
        }
    ]

    function columnOf(mods, column) {
        if (column >= view.cols)
            return [];
        const out = [];
        for (let i = column; i < mods.length; i += view.cols)
            out.push(mods[i]);
        return out;
    }

    function modsFor(engine) {
        const all = GameStatus.allMods();
        if (engine === 0) {
            return all.filter(mod => !mod.supportsEngine(Game.Unreal) && !mod.supportsEngine(Game.Unity) &&
                                     !mod.supportsEngine(Game.Source) && !mod.supportsEngine(Game.Godot));
        }
        return all.filter(mod => mod.supportsEngine(engine));
    }

    boundsBehavior: Flickable.StopAtBounds
    clip: true
    contentHeight: col.height + Theme.notchHeight + 50

    ScrollBar.vertical: GlassScrollBar {
    }

    Column {
        id: col

        spacing: 20
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

        Repeater {
            model: view.engines

            Column {
                id: section

                required property var modelData
                readonly property var mods: view.modsFor(modelData.engine)

                spacing: 12
                visible: mods.length > 0
                width: col.width

                VText {
                    color: Theme.glassFaint
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    text: section.modelData.label
                }

                Row {
                    spacing: view.cols > 1 ? 16 : 0
                    width: parent.width

                    Column {
                        spacing: 16
                        width: view.cols > 1 ? (parent.width - 16) / 2 : parent.width

                        Repeater {
                            model: view.columnOf(section.mods, 0)

                            delegate: ModCard {
                            }
                        }
                    }

                    Column {
                        spacing: 16
                        visible: view.cols > 1
                        width: (parent.width - 16) / 2

                        Repeater {
                            model: view.columnOf(section.mods, 1)

                            delegate: ModCard {
                            }
                        }
                    }
                }
            }
        }
    }

    component ModCard: Rectangle {
        id: card

        required property var modelData
        readonly property int stored: (GameStatus.revision, modelData.downloadedCount())

        border.color: Theme.glassLine
        border.width: 1.5
        color: Theme.glassPanel
        height: cardCol.height + 32
        radius: 18
        width: parent.width

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
                text: Theme.linkify(card.modelData.description + (card.modelData.info !== "" ? " " + card.modelData.info :
                                                                                               ""))
                textFormat: Text.StyledText
                width: parent.width
                wrapMode: Text.Wrap

                onLinkActivated: link => Qt.openUrlExternally(link)

                HoverHandler {
                    cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
            }

            LaunchOptions {
                options: card.modelData.launchOptions
            }

            VText {
                color: Theme.glassFaint
                font.pixelSize: 12
                text: (card.stored === 0 ? "Nothing on disk" : card.stored === 1 ? "1 version on disk" : card.stored
                                                                                   + " versions on disk") + (
                          card.modelData.providesVr ? "" : ". Installed into each game that needs it.")
                width: parent.width
                wrapMode: Text.Wrap
            }
        }
    }
}
