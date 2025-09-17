pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dev.lorendb.kaon

RowLayout {
    id: gridRoot

    readonly property bool shouldFilterStores: [Steam.count, Heroic.count, Itch.count].filter(x => x !== 0).length > 1

    signal gameClicked(Game game)

    spacing: 10

    SystemPalette {
        id: palette

    }

    ButtonGroup {
        id: viewButtonGroup

    }

    ButtonGroup {
        id: sortButtonGroup

    }

    Frame {
        id: frame

        Layout.fillHeight: true
        Layout.fillWidth: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            TextField {
                Layout.fillWidth: true
                placeholderText: "Search..."

                onTextChanged: GamesFilterModel.search = text
            }

            RowLayout {
                spacing: 10

                Component.onCompleted: {
                    if (GamesFilterModel.viewType === GamesFilterModel.Grid)
                    gridButton.checked = true;
                    else if (GamesFilterModel.viewType === GamesFilterModel.List)
                    listButton.checked = true;

                    if (GamesFilterModel.sortType === GamesFilterModel.LastPlayed)
                    sortLastPlayedButton.checked = true;
                    else if (GamesFilterModel.sortType === GamesFilterModel.Alphabetical)
                    sortAlphabeticalButton.checked = true;
                }

                Label {
                    text: "View:"
                }

                RadioButton {
                    id: gridButton

                    ButtonGroup.group: viewButtonGroup
                    text: "Grid"

                    onCheckedChanged: GamesFilterModel.viewType = GamesFilterModel.Grid
                }

                RadioButton {
                    id: listButton

                    ButtonGroup.group: viewButtonGroup
                    text: "List"

                    onCheckedChanged: GamesFilterModel.viewType = GamesFilterModel.List
                }

                ToolSeparator {
                }

                Label {
                    text: "Sort:"
                }

                RadioButton {
                    id: sortLastPlayedButton

                    ButtonGroup.group: sortButtonGroup
                    text: "Last played"

                    onCheckedChanged: GamesFilterModel.sortType = GamesFilterModel.LastPlayed
                }

                RadioButton {
                    id: sortAlphabeticalButton

                    ButtonGroup.group: sortButtonGroup
                    text: "Alphabetical"

                    onCheckedChanged: GamesFilterModel.sortType = GamesFilterModel.Alphabetical
                }

                ToolSeparator {
                }

                ToolButton {
                    icon.color: palette.buttonText
                    icon.name: "list-add"
                    icon.source: Qt.resolvedUrl("icons/list-add.svg")
                    text: "Add game..."

                    onClicked: {
                        theStack.push(newGameComponent);
                    }
                }

                ToolButton {
                    icon.color: palette.buttonText
                    icon.name: "view-refresh"
                    icon.source: Qt.resolvedUrl("icons/view-refresh.svg")

                    onClicked: {
                        Steam.scanStore();
                        Heroic.scanStore();
                        Itch.scanStore();
                    }
                }
            }

            Loader {
                Layout.columnSpan: 3
                Layout.fillHeight: true
                Layout.fillWidth: true
                sourceComponent: {
                    if (gridButton.checked)
                    return gridView;
                    else if (listButton.checked)
                    return listView;
                    else
                    return null;
                }

                Component {
                    id: gridView

                    GameGridDelegate {
                        onGameClicked: game => {
                            gridRoot.gameClicked(game);
                        }
                    }
                }

                Component {
                    id: listView

                    GameListDelegate {
                        onGameClicked: game => {
                            gridRoot.gameClicked(game);
                        }
                    }
                }
            }
        }
    }

    ScrollView {
        Layout.fillHeight: true
        Layout.preferredWidth: filterColumn.implicitWidth + effectiveScrollBarWidth + 10
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: filterColumn

            spacing: 10

            /////////////////////////////////////
            // Game engine filter
            /////////////////////////////////////

            Label {
                font.bold: true
                text: "Filter by game engine"
            }

            Label {
                font.italic: true
                text: "(may not be fully accurate)"
            }

            RowLayout {
                spacing: 10

                Button {
                    text: "All"

                    onClicked: {
                        unrealCb.checked = true;
                        unityCb.checked = true;
                        godotCb.checked = true;
                        sourceCb.checked = true;
                        unknownCb.checked = true;
                    }
                }

                Button {
                    text: "None"

                    onClicked: {
                        unrealCb.checked = false;
                        unityCb.checked = false;
                        godotCb.checked = false;
                        sourceCb.checked = false;
                        unknownCb.checked = false;
                    }
                }
            }

            CheckBox {
                id: unrealCb

                text: "Unreal Engine"

                Component.onCompleted: checked = GamesFilterModel.isEngineFilterSet(Game.Unreal)
                onCheckedChanged: GamesFilterModel.setEngineFilter(Game.Unreal, checked)
            }

            CheckBox {
                id: unityCb

                text: "Unity"

                Component.onCompleted: checked = GamesFilterModel.isEngineFilterSet(Game.Unity)
                onCheckedChanged: GamesFilterModel.setEngineFilter(Game.Unity, checked)
            }

            CheckBox {
                id: godotCb

                text: "Godot"

                Component.onCompleted: checked = GamesFilterModel.isEngineFilterSet(Game.Godot)
                onCheckedChanged: GamesFilterModel.setEngineFilter(Game.Godot, checked)
            }

            CheckBox {
                id: sourceCb

                text: "Source"

                Component.onCompleted: checked = GamesFilterModel.isEngineFilterSet(Game.Source)
                onCheckedChanged: GamesFilterModel.setEngineFilter(Game.Source, checked)
            }

            CheckBox {
                id: unknownCb

                text: "Other/Unknown"

                Component.onCompleted: checked = GamesFilterModel.isEngineFilterSet(Game.UnknownEngine)
                onCheckedChanged: GamesFilterModel.setEngineFilter(Game.UnknownEngine, checked)
            }

            /////////////////////////////////////
            // Game type filter
            /////////////////////////////////////

            MenuSeparator {
            }

            Label {
                font.bold: true
                text: "Filter by type"
            }

            RowLayout {
                spacing: 10

                Button {
                    text: "All"

                    onClicked: {
                        gameCb.checked = true;
                        demoCb.checked = true;
                        appCb.checked = true;
                        toolCb.checked = true;
                        musicCb.checked = true;
                    }
                }

                Button {
                    text: "None"

                    onClicked: {
                        gameCb.checked = false;
                        demoCb.checked = false;
                        appCb.checked = false;
                        toolCb.checked = false;
                        musicCb.checked = false;
                    }
                }
            }

            CheckBox {
                id: gameCb

                text: "Game"

                Component.onCompleted: checked = GamesFilterModel.isTypeFilterSet(Game.Game)
                onCheckedChanged: GamesFilterModel.setTypeFilter(Game.Game, checked)
            }

            CheckBox {
                id: demoCb

                text: "Demo"

                Component.onCompleted: checked = GamesFilterModel.isTypeFilterSet(Game.Demo)
                onCheckedChanged: GamesFilterModel.setTypeFilter(Game.Demo, checked)
            }

            CheckBox {
                id: appCb

                text: "Application"

                Component.onCompleted: checked = GamesFilterModel.isTypeFilterSet(Game.App)
                onCheckedChanged: GamesFilterModel.setTypeFilter(Game.App, checked)
            }

            CheckBox {
                id: toolCb

                text: "Tool"

                Component.onCompleted: checked = GamesFilterModel.isTypeFilterSet(Game.Tool)
                onCheckedChanged: GamesFilterModel.setTypeFilter(Game.Tool, checked)
            }

            CheckBox {
                id: musicCb

                text: "Music"

                Component.onCompleted: checked = GamesFilterModel.isTypeFilterSet(Game.Music)
                onCheckedChanged: GamesFilterModel.setTypeFilter(Game.Music, checked)
            }

            /////////////////////////////////////
            // Features filter
            /////////////////////////////////////

            MenuSeparator {
            }

            Label {
                font.bold: true
                text: "Filter by features"
            }

            RowLayout {
                spacing: 10

                Button {
                    text: "All"

                    onClicked: {
                        flatscreenCb.checked = true;
                        vrCb.checked = true;
                        anticheatCb.checked = true;
                    }
                }

                Button {
                    text: "None"

                    onClicked: {
                        flatscreenCb.checked = false;
                        vrCb.checked = false;
                        anticheatCb.checked = false;
                    }
                }
            }

            RowLayout {
                spacing: 10

                Label {
                    text: "Matching:"
                }

                RadioButton {
                    text: "Any"

                    Component.onCompleted: checked = GamesFilterModel.featureFilterType === GamesFilterModel.HasAnyFilter
                    onClicked: GamesFilterModel.featureFilterType = GamesFilterModel.HasAnyFilter
                }

                RadioButton {
                    text: "All"

                    Component.onCompleted: checked = GamesFilterModel.featureFilterType === GamesFilterModel.HasAllFilters
                    onClicked: GamesFilterModel.featureFilterType = GamesFilterModel.HasAllFilters
                }
            }

            CheckBox {
                id: flatscreenCb

                text: "Supports flatscreen"

                Component.onCompleted: checked = GamesFilterModel.isFeatureFilterSet(Game.Flatscreen)
                onCheckedChanged: GamesFilterModel.setFeatureFilter(Game.Flatscreen, checked)
            }

            CheckBox {
                id: vrCb

                text: "Supports VR"

                Component.onCompleted: checked = GamesFilterModel.isFeatureFilterSet(Game.VR)
                onCheckedChanged: GamesFilterModel.setFeatureFilter(Game.VR, checked)
            }

            CheckBox {
                id: anticheatCb

                text: "Has anticheat"

                Component.onCompleted: checked = GamesFilterModel.isFeatureFilterSet(Game.Anticheat)
                onCheckedChanged: GamesFilterModel.setFeatureFilter(Game.Anticheat, checked)
            }

            /////////////////////////////////////
            // Store filter
            /////////////////////////////////////

            MenuSeparator {
                visible: gridRoot.shouldFilterStores
            }

            Label {
                font.bold: true
                text: "Filter by store"
                visible: gridRoot.shouldFilterStores
            }

            RowLayout {
                spacing: 10
                visible: gridRoot.shouldFilterStores

                Button {
                    text: "All"

                    onClicked: {
                        steamCb.checked = true;
                        itchCb.checked = true;
                        heroicCb.checked = true;
                        customCb.checked = true;
                    }
                }

                Button {
                    text: "None"

                    onClicked: {
                        steamCb.checked = false;
                        itchCb.checked = false;
                        heroicCb.checked = false;
                        customCb.checked = false;
                    }
                }
            }

            CheckBox {
                id: steamCb

                text: "Steam"
                visible: gridRoot.shouldFilterStores && Steam.count > 0

                Component.onCompleted: checked = GamesFilterModel.isStoreFilterSet(Game.Steam)
                onCheckedChanged: GamesFilterModel.setStoreFilter(Game.Steam, checked)
            }

            CheckBox {
                id: heroicCb

                text: "Heroic"
                visible: gridRoot.shouldFilterStores && Heroic.count > 0

                Component.onCompleted: checked = GamesFilterModel.isStoreFilterSet(Game.Heroic)
                onCheckedChanged: GamesFilterModel.setStoreFilter(Game.Heroic, checked)
            }

            CheckBox {
                id: itchCb

                text: "Itch"
                visible: gridRoot.shouldFilterStores && Itch.count > 0

                Component.onCompleted: checked = GamesFilterModel.isStoreFilterSet(Game.Itch)
                onCheckedChanged: GamesFilterModel.setStoreFilter(Game.Itch, checked)
            }

            CheckBox {
                id: customCb

                text: "Custom"
                visible: gridRoot.shouldFilterStores && CustomGames.count > 0

                Component.onCompleted: checked = GamesFilterModel.isStoreFilterSet(Game.Custom)
                onCheckedChanged: GamesFilterModel.setStoreFilter(Game.Custom, checked)
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }

    Component {
        id: newGameComponent

        AddCustomGame {
        }
    }
}
