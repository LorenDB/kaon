import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts

pragma ComponentBehavior: Bound

import dev.lorendb.kaon

RowLayout {
    id: gridRoot

    signal gameClicked(int steamId)
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
                onTextChanged: SteamFilter.search = text
            }

            Component.onCompleted: {
                if (Steam.viewType === Steam.Grid)
                    gridButton.checked = true;
                else if (Steam.viewType === Steam.List)
                    listButton.checked = true;
            }

            RadioButton {
                id: gridButton

                text: "Grid"
                onCheckedChanged: Steam.viewType = Steam.Grid
            }

            RadioButton {
                id: listButton

                text: "List"
                onCheckedChanged: Steam.viewType = Steam.List
            }

            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.columnSpan: 3

                Component {
                    id: gridView

                    SteamGridDelegate {
                        onGameClicked: (steamId) => { gridRoot.gameClicked(steamId) }
                    }
                }

                Component {
                    id: listView

                    SteamListDelegate {
                        onGameClicked: (steamId) => { gridRoot.gameClicked(steamId) }
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
                Component.onCompleted: checked = SteamFilter.isEngineFilterSet(Game.Unreal)
                onCheckedChanged: SteamFilter.setEngineFilter(Game.Unreal, checked)
            }

            CheckBox {
                id: unityCb

                text: "Unity"
                Component.onCompleted: checked = SteamFilter.isEngineFilterSet(Game.Unity)
                onCheckedChanged: SteamFilter.setEngineFilter(Game.Unity, checked)
            }

            CheckBox {
                id: godotCb

                text: "Godot"
                Component.onCompleted: checked = SteamFilter.isEngineFilterSet(Game.Godot)
                onCheckedChanged: SteamFilter.setEngineFilter(Game.Godot, checked)
            }

            CheckBox {
                id: sourceCb

                text: "Source"
                Component.onCompleted: checked = SteamFilter.isEngineFilterSet(Game.Source)
                onCheckedChanged: SteamFilter.setEngineFilter(Game.Source, checked)
            }

            CheckBox {
                id: unknownCb

                text: "Other/Unknown"
                Component.onCompleted: checked = SteamFilter.isEngineFilterSet(Game.Unknown)
                onCheckedChanged: SteamFilter.setEngineFilter(Game.Unknown, checked)
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
                Component.onCompleted: checked = SteamFilter.isTypeFilterSet(Game.Game)
                onCheckedChanged: SteamFilter.setTypeFilter(Game.Game, checked)
            }

            CheckBox {
                id: demoCb

                text: "Demo"
                Component.onCompleted: checked = SteamFilter.isTypeFilterSet(Game.Demo)
                onCheckedChanged: SteamFilter.setTypeFilter(Game.Demo, checked)
            }

            CheckBox {
                id: appCb

                text: "Application"
                Component.onCompleted: checked = SteamFilter.isTypeFilterSet(Game.App)
                onCheckedChanged: SteamFilter.setTypeFilter(Game.App, checked)
            }

            CheckBox {
                id: toolCb

                text: "Tool"
                Component.onCompleted: checked = SteamFilter.isTypeFilterSet(Game.Tool)
                onCheckedChanged: SteamFilter.setTypeFilter(Game.Tool, checked)
            }

            CheckBox {
                id: musicCb

                text: "Music"
                Component.onCompleted: checked = SteamFilter.isTypeFilterSet(Game.Music)
                onCheckedChanged: SteamFilter.setTypeFilter(Game.Music, checked)
            }

            Item { Layout.fillHeight: true }
        }
    }
}
