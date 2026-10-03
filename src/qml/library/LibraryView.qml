import QtCore
import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// The library, grouped by what Kaon can do for each game. Each group is a toggle chip, so hiding the games Kaon can't
// mod is one click and nothing is filtered away without saying so. Rows of covers are built here so the list stays
// virtualized for big libraries.
Item {
    id: view

    readonly property real cardW: (inner - 14 * (cols - 1)) / cols
    readonly property int cols: Math.max(2, Math.floor((inner + 14) / (118 + 14)))
    property var groups: ({
                              "ready": [],
                              "setup": [],
                              "native": [],
                              "none": []
                          })
    readonly property real inner: width - 2 * Theme.pad
    readonly property var order: ["ready", "setup", "native", "none"]
    property string rowsKey: ""
    readonly property var titles: ({
                                       "ready": "Ready for VR",
                                       "setup": "Needs setup",
                                       "native": "Has its own VR mode",
                                       "none": "No VR mod yet"
                                   })
    property int total: 0

    function rebuild() {
        let rows = [];
        for (const key of order) {
            const list = groups[key];
            if (list.length === 0 || librarySettings.shownGroups.indexOf(key) < 0)
                continue;
            rows.push({
                          "kind": "header",
                          "group": key,
                          "count": list.length
                      });
            for (let i = 0; i < list.length; i += cols)
                rows.push({
                              "kind": "cards",
                              "group": key,
                              "games": list.slice(i, i + cols)
                          });
        }

        const key = cols + "|" + rows.map(r => r.kind === "header" ? r.group + r.count : r.games.map(g => g.store + ":"
                                                                                                          + g.id).join(
                                                                         ",")).join("|");
        if (key === rowsKey)
            return;
        rowsKey = key;

        // Keep the scroll position when a game moves between groups
        const y = list.contentY;
        list.model = rows;
        Qt.callLater(() => list.contentY = Math.max(list.originY, Math.min(y, list.originY + list.contentHeight
                                                                           - list.height)));
    }

    function regroup() {
        let g = {
            "ready": [],
            "setup": [],
            "native": [],
            "none": []
        };
        const games = GamesFilterModel.games();
        for (const game of games)
            g[GameStatus.group(game)].push(game);
        groups = g;
        total = games.length;
        rebuild();
    }

    function toggleGroup(key) {
        let shown = librarySettings.shownGroups.slice();
        const i = shown.indexOf(key);
        if (i >= 0)
            shown.splice(i, 1);
        else
            shown.push(key);
        librarySettings.shownGroups = shown;
        rebuild();
    }

    Component.onCompleted: regroup()
    onColsChanged: rebuild()

    Settings {
        id: librarySettings

        property var shownGroups: ["ready", "setup", "native"]

        category: "library"
    }

    Connections {
        function onGamesChanged() {
            // Rescans replace the game objects even when the ids stay the same
            view.rowsKey = "";
            view.regroup();
        }

        target: GamesFilterModel
    }

    Connections {
        function onChanged() {
            view.regroup();
        }

        target: GameStatus
    }

    Item {
        id: top

        height: bar.height + 30
        width: view.width
        z: 1

        Item {
            id: bar

            height: Math.max(chips.height, sortRow.height)
            width: parent.width - 2 * Theme.pad
            x: Theme.pad
            y: 16

            Flow {
                id: chips

                spacing: 8
                width: parent.width - sortRow.width - 16

                Repeater {
                    model: view.order

                    Chip {
                        required property string modelData

                        checked: librarySettings.shownGroups.indexOf(modelData) >= 0
                        count: view.groups[modelData].length
                        led: Theme.led(modelData)
                        text: view.titles[modelData]

                        onToggled: view.toggleGroup(modelData)
                    }
                }
            }

            Row {
                id: sortRow

                anchors.right: parent.right
                spacing: 8

                Segmented {
                    current: GamesFilterModel.sortType
                    options: [
                        {
                            "id": GamesFilterModel.LastPlayed,
                            "label": "Recent"
                        },
                        {
                            "id": GamesFilterModel.Alphabetical,
                            "label": "A to Z"
                        }
                    ]

                    onPicked: key => GamesFilterModel.sortType = key
                }

                Chip {
                    id: filtersChip

                    checked: filters.opened
                    showLed: false
                    text: "Filters"

                    onToggled: filters.opened ? filters.close() : filters.open()

                    FilterPopup {
                        id: filters

                        x: filtersChip.width - width
                        y: filtersChip.height + 8
                    }
                }
            }
        }
    }

    ListView {
        id: list

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: top.bottom
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        reuseItems: true

        ScrollBar.vertical: GlassScrollBar {
        }
        delegate: Item {
            id: row

            required property var modelData

            height: modelData.kind === "header" ? 52 : view.cardW * 1.5 + 64
            width: view.width

            Row {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                spacing: 10
                visible: row.modelData.kind === "header"
                x: Theme.pad

                Led {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.led(row.modelData.group)
                    size: 9
                }

                VText {
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: 17
                    font.weight: Font.ExtraBold
                    text: view.titles[row.modelData.group] ?? ""
                }

                VText {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.glassFaint
                    font.pixelSize: 15
                    font.weight: Font.Bold
                    text: row.modelData.count ?? ""
                }

                VText {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.glassFaint
                    elide: Text.ElideRight
                    font.pixelSize: 13
                    leftPadding: 6
                    text: "Kaon's mods cover Unreal Engine games and Portal 2. Engine detection is a best guess."
                    visible: row.modelData.group === "none"
                    width: Math.min(implicitWidth, view.width - 2 * Theme.pad - 280)
                }
            }

            Row {
                spacing: 14
                visible: row.modelData.kind === "cards"
                x: Theme.pad

                Repeater {
                    model: row.modelData.kind === "cards" ? row.modelData.games : []

                    GameCard {
                        cardWidth: view.cardW
                    }
                }
            }
        }
        // room to scroll the last row clear of the notch
        footer: Item {
            height: Theme.notchHeight + 30
            width: 1
        }
    }

    Column {
        id: emptyState

        // Groups that have games but are switched off; a search can land entirely inside one of them
        readonly property var hiddenGroups: view.order.filter(k => view.groups[k].length > 0 && librarySettings.shownGroups.indexOf(
                                                                       k) < 0)

        spacing: 8
        visible: list.count === 0
        width: view.inner
        x: Theme.pad
        y: top.height + 6

        VText {
            font.pixelSize: 18
            font.weight: Font.ExtraBold
            text: view.total === 0 ? (GamesFilterModel.search !== "" ? "No games match “" + GamesFilterModel.search + "”" :
                                                                       "No games found") : GamesFilterModel.search !== ""
                                     ? "Your matches are in a hidden group" : "Every group with games is switched off"
            width: parent.width
            wrapMode: Text.Wrap
        }

        VText {
            color: Theme.glassMuted
            font.pixelSize: 13
            text: view.total === 0 ? (GamesFilterModel.search !== ""
                                      ? "Check the spelling, or clear the search to see your whole library." :
                                        "Kaon looks for Steam, Heroic and itch libraries. Add a game by hand with the button below, or check the filters.") :
                                     "Turn on a group to see its games."
            width: parent.width
            wrapMode: Text.Wrap
        }

        Flow {
            spacing: 8
            topPadding: 4
            width: parent.width

            Repeater {
                model: view.total > 0 ? emptyState.hiddenGroups : []

                VButton {
                    required property string modelData

                    small: true
                    text: "Show " + view.titles[modelData] + " (" + view.groups[modelData].length + ")"

                    onClicked: view.toggleGroup(modelData)
                }
            }
        }
    }
}
