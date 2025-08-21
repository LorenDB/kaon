import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

pragma ComponentBehavior: Bound

import dev.lorendb.kaon

RowLayout {
    id: gridRoot

    signal gameClicked(Game game)
    spacing: 10

    SystemPalette { id: palette }

    Frame {
        id: frame

        Layout.fillWidth: true
        Layout.fillHeight: true

        GridLayout {
            anchors.fill: parent
            rowSpacing: 10
            columns: 3

            TextField {
                Layout.fillWidth: true
                placeholderText: "Search..."
                onTextChanged: GamesFilterModel.search = text
            }

            Component.onCompleted: {
                if (GamesFilterModel.viewType === GamesFilterModel.Grid)
                    gridButton.checked = true;
                else if (GamesFilterModel.viewType === GamesFilterModel.List)
                    listButton.checked = true;
            }

            RadioButton {
                id: gridButton

                text: "Grid"
                onCheckedChanged: GamesFilterModel.viewType = GamesFilterModel.Grid
            }

            RadioButton {
                id: listButton

                text: "List"
                onCheckedChanged: GamesFilterModel.viewType = GamesFilterModel.List
            }

            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.columnSpan: 3

                Component {
                    id: gridView

                    SteamGridDelegate {
                        onGameClicked: (game) => { gridRoot.gameClicked(game) }
                    }
                }

                Component {
                    id: listView

                    SteamListDelegate {
                        onGameClicked: (game) => { gridRoot.gameClicked(game) }
                    }
                }

                sourceComponent: {
                    if (gridButton.checked)
                        return gridView;
                    else if (listButton.checked)
                        return listView;
                    else
                        return null;
                }
            }
        }
    }

    ScrollView {
        Layout.preferredWidth: filterColumn.implicitWidth + effectiveScrollBarWidth + 10
        Layout.fillHeight: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: filterColumn

            spacing: 10

            Label {
                text: "Filter by game engine"
                font.bold: true
            }

            Label {
                text: "(may not be fully accurate)"
                font.italic: true
            }

            RowLayout {
                spacing: 10

                Button {
                    text: "All"
                    onClicked: {
                        unrealCb.checked = true
                        unityCb.checked = true
                        godotCb.checked = true
                        sourceCb.checked = true
                        unknownCb.checked = true
                    }
                }

                Button {
                    text: "None"
                    onClicked: {
                        unrealCb.checked = false
                        unityCb.checked = false
                        godotCb.checked = false
                        sourceCb.checked = false
                        unknownCb.checked = false
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
                Component.onCompleted: checked = GamesFilterModel.isEngineFilterSet(Game.Unknown)
                onCheckedChanged: GamesFilterModel.setEngineFilter(Game.Unknown, checked)
            }

            MenuSeparator {}

            Label {
                text: "Filter by type"
                font.bold: true
            }

            RowLayout {
                spacing: 10

                Button {
                    text: "All"
                    onClicked: {
                        gameCb.checked = true
                        demoCb.checked = true
                        appCb.checked = true
                        toolCb.checked = true
                        musicCb.checked = true
                    }
                }

                Button {
                    text: "None"
                    onClicked: {
                        gameCb.checked = false
                        demoCb.checked = false
                        appCb.checked = false
                        toolCb.checked = false
                        musicCb.checked = false
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

            MenuSeparator {}

            Label {
                text: "Filter by store"
                font.bold: true
            }

            RowLayout {
                spacing: 10

                Button {
                    text: "All"
                    onClicked: {
                        steamCb.checked = true;
                        itchCb.checked = true;
                        heroicCb.checked = true;
                    }
                }

                Button {
                    text: "None"
                    onClicked: {
                        steamCb.checked = false;
                        itchCb.checked = false;
                        heroicCb.checked = false;
                    }
                }
            }

            CheckBox {
                id: steamCb

                text: "Steam"
                Component.onCompleted: checked = GamesFilterModel.isStoreFilterSet(Game.Steam)
                onCheckedChanged: GamesFilterModel.setStoreFilter(Game.Steam, checked)
            }

            CheckBox {
                id: heroicCb

                text: "Heroic"
                Component.onCompleted: checked = GamesFilterModel.isStoreFilterSet(Game.Heroic)
                onCheckedChanged: GamesFilterModel.setStoreFilter(Game.Heroic, checked)
            }

            CheckBox {
                id: itchCb

                text: "Itch"
                Component.onCompleted: checked = GamesFilterModel.isStoreFilterSet(Game.Itch)
                onCheckedChanged: GamesFilterModel.setStoreFilter(Game.Itch, checked)
            }

            Item { Layout.fillHeight: true }
        }
    }
}
