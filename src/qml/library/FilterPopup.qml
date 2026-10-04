import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// Engine, app type and store filters. Most people never open this.
Popup {
    id: pop

    readonly property bool manyStores: [Steam.count, Heroic.count, Itch.count, CustomGames.count].filter(c => c > 0).length
    > 1

    function focusFirst() {
        const stack = [contentItem];
        while (stack.length) {
            const item = stack.shift();
            if (!item || item.visible === false)
                continue;
            if (item.activeFocusOnTab && item.enabled !== false && item.width > 1) {
                item.forceActiveFocus();
                return;
            }
            const kids = item.children;
            for (let i = 0; kids && i < kids.length; ++i)
                stack.push(kids[i]);
        }
    }

    margins: 8
    padding: 16
    width: 330

    background: Rectangle {
        border.color: Theme.glassLine
        border.width: 1.5
        color: Theme.glassPanel
        radius: 18
    }
    contentItem: Column {
        spacing: 10

        VText {
            color: Theme.glassMuted
            font.pixelSize: 12
            font.weight: Font.Bold
            text: "Engine (a best guess)"
        }

        Flow {
            spacing: 6
            width: parent.width

            Repeater {
                model: [
                    {
                        "value": Game.Unreal,
                        "label": "Unreal"
                    },
                    {
                        "value": Game.Unity,
                        "label": "Unity"
                    },
                    {
                        "value": Game.Source,
                        "label": "Source"
                    },
                    {
                        "value": Game.Godot,
                        "label": "Godot"
                    },
                    {
                        "value": Game.UnknownEngine,
                        "label": "Other"
                    }
                ]

                Chip {
                    required property var modelData

                    checked: (GamesFilterModel.engineFilter, GamesFilterModel.isEngineFilterSet(modelData.value))
                    showLed: false
                    text: modelData.label

                    onToggled: GamesFilterModel.setEngineFilter(modelData.value, !checked)
                }
            }
        }

        VText {
            color: Theme.glassMuted
            font.pixelSize: 12
            font.weight: Font.Bold
            text: "Kind of app"
            topPadding: 4
        }

        Flow {
            spacing: 6
            width: parent.width

            Repeater {
                model: [
                    {
                        "value": Game.Game,
                        "label": "Game"
                    },
                    {
                        "value": Game.Demo,
                        "label": "Demo"
                    },
                    {
                        "value": Game.App,
                        "label": "Application"
                    },
                    {
                        "value": Game.Tool,
                        "label": "Tool"
                    },
                    {
                        "value": Game.Music,
                        "label": "Music"
                    },
                    {
                        "value": Game.Other,
                        "label": "Other"
                    }
                ]

                Chip {
                    required property var modelData

                    checked: (GamesFilterModel.typeFilter, GamesFilterModel.isTypeFilterSet(modelData.value))
                    showLed: false
                    text: modelData.label

                    onToggled: GamesFilterModel.setTypeFilter(modelData.value, !checked)
                }
            }
        }

        VText {
            color: Theme.glassMuted
            font.pixelSize: 12
            font.weight: Font.Bold
            text: "Store"
            topPadding: 4
            visible: pop.manyStores
        }

        Flow {
            spacing: 6
            visible: pop.manyStores
            width: parent.width

            Repeater {
                model: [
                    {
                        "value": Game.Steam,
                        "label": "Steam",
                        "count": Steam.count
                    },
                    {
                        "value": Game.Heroic,
                        "label": "Heroic",
                        "count": Heroic.count
                    },
                    {
                        "value": Game.Itch,
                        "label": "itch",
                        "count": Itch.count
                    },
                    {
                        "value": Game.Custom,
                        "label": "Added by hand",
                        "count": CustomGames.count
                    }
                ].filter(s => s.count > 0)

                Chip {
                    required property var modelData

                    checked: (GamesFilterModel.storeFilter, GamesFilterModel.isStoreFilterSet(modelData.value))
                    count: modelData.count
                    showLed: false
                    text: modelData.label

                    onToggled: GamesFilterModel.setStoreFilter(modelData.value, !checked)
                }
            }
        }
    }

    onOpened: Qt.callLater(pop.focusFirst)
}
