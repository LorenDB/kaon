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

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.durationFast
                easing.type: Theme.easeOut
            }
            NumberAnimation {
                property: "scale"
                from: 0.96
                to: 1
                duration: Theme.durationMed
                easing.type: Theme.easeOut
            }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: Theme.durationFast
                easing.type: Theme.easeIn
            }
            NumberAnimation {
                property: "scale"
                from: 1
                to: 0.98
                duration: Theme.durationFast
                easing.type: Theme.easeIn
            }
        }
    }

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

                    checked: GamesFilterModel.isEngineFilterSet(modelData.value, GamesFilterModel.filterRevision)
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

                    checked: GamesFilterModel.isTypeFilterSet(modelData.value, GamesFilterModel.filterRevision)
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

                    checked: GamesFilterModel.isStoreFilterSet(modelData.value, GamesFilterModel.filterRevision)
                    count: modelData.count
                    showLed: false
                    text: modelData.label

                    onToggled: GamesFilterModel.setStoreFilter(modelData.value, !checked)
                }
            }
        }

        Item {
            height: clearFilters.visible ? clearFilters.height + 8 : 0
            width: parent.width

            VButton {
                id: clearFilters

                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 8
                quiet: true
                small: true
                text: "Clear filters"
                visible: GamesFilterModel.filtersActive

                onClicked: GamesFilterModel.clearFilters()
            }
        }
    }

    Component.onDestruction: if (visible)
                                 Nav.popupShown(false)
    onOpened: Qt.callLater(pop.focusFirst)
    onVisibleChanged: Nav.popupShown(visible)
}
