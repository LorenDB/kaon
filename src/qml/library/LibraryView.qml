import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes

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
    property real heldY: 0
    readonly property real inner: width - 2 * Theme.pad
    // The chips wrap onto three lines in a small window unless they say less
    readonly property bool narrow: width < 1030
    readonly property var order: ["ready", "setup", "native", "none"]
    property bool ready: false
    property string rowsKey: ""
    property bool scrollLocked: false
    property int scrollTries: 0
    property bool settled: false
    readonly property var shortTitles: ({
                                            "ready": "Ready",
                                            "setup": "Setup",
                                            "native": "Built-in VR",
                                            "none": "No mod"
                                        })
    // The loader hides this page without destroying it. parent.visible is that loader.
    readonly property bool shown: parent !== null && parent.visible
    readonly property var titles: ({
                                       "ready": "Ready for VR",
                                       "setup": "Needs setup",
                                       "native": "Has its own VR mode",
                                       "none": "No VR mod yet"
                                   })
    property int total: 0

    function closePopups() {
        filters.close();
    }

    function placeScroll() {
        if (!scrollLocked)
            return;
        const maxY = list.originY + Math.max(0, list.contentHeight - list.height);
        // A hidden list reports no height yet. Guessing would pin the user at the top, so wait until it's on screen.
        const layoutShort = heldY > maxY + 0.5 && list.contentHeight <= list.height;
        if (layoutShort && (scrollTries > 0 || !shown)) {
            if (scrollTries > 0)
                scrollTries -= 1;
            if (shown)
                Qt.callLater(placeScroll);
            return;
        }
        list.contentY = Math.max(list.originY, Math.min(heldY, maxY));
        scrollLocked = false;
    }

    function rebuild() {
        let rows = [];
        // A search looks in every group. Someone who types a game's name wants that game, whichever group it is in.
        const searching = GamesFilterModel.search !== "";
        for (const key of order) {
            const list = groups[key];
            const hidden = librarySettings.shownGroups.indexOf(key) < 0;
            if (list.length === 0 || (hidden && !searching))
                continue;
            rows.push({
                          "kind": "header",
                          "group": key,
                          "count": list.length,
                          "found": hidden
                      });
            for (let i = 0; i < list.length; i += cols)
                rows.push({
                              "kind": "cards",
                              "group": key,
                              "games": list.slice(i, i + cols)
                          });
        }

        const key = cols + "|" + rows.map(r => r.kind === "header" ? r.group + r.count + r.found : r.games.map(g => g.store
                                                                                                                    + ":" + g.id).join(
                                                                         ",")).join("|");
        if (key === rowsKey)
            return;
        rowsKey = key;

        // Replacing the model jumps back to the top. Hold the offset the user was actually looking at.
        const y = scrollLocked ? heldY : list.contentY;
        scrollLocked = true;
        heldY = y;
        list.model = rows;
        scrollTries = 8;
        placeScroll();
    }

    function regroup() {
        // The first scan waits until every store has published, so games don't trickle in one library at a time.
        // A later rescan keeps this grid and only swaps in the new objects, or the cards would point at deleted games.
        if (GamesFilterModel.scanning && !settled)
            return;

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
        if (!GamesFilterModel.scanning)
            settled = true;
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

    Component.onCompleted: {
        ready = true;
        regroup();
    }
    onColsChanged: rebuild()
    onShownChanged: {
        if (!ready)
            return;
        if (!shown) {
            closePopups();
            return;
        }
        if (scrollLocked) {
            scrollTries = 8;
            placeScroll();
        }
    }

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

        function onScanningChanged() {
            // Starting a scan must not throw away the grid. Rebuild once the last store has published.
            if (!GamesFilterModel.scanning)
                view.regroup();
        }

        function onSortTypeChanged() {
            // Proxy invalidate should already emit gamesChanged; rebuild here so sort never looks stuck.
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
                        text: view.narrow ? view.shortTitles[modelData] : view.titles[modelData]

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

                    attention: GamesFilterModel.filtersActive
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

        // A mouse scrolls with its wheel; dragging the covers around is for touch screens
        acceptedButtons: Qt.NoButton
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
                    text: row.modelData.found === true ? "Hidden group, shown because it has what you searched for" :
                                                         "Kaon has mods for Unreal Engine and Unity games, Portal and Portal 2. Engines are a best guess."
                    visible: row.modelData.group === "none" || row.modelData.found === true
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
            height: Theme.notchClearance + 30
            width: 1
        }

        onContentHeightChanged: if (view.scrollLocked)
                                    view.placeScroll()
        onContentYChanged: if (!view.scrollLocked)
                               view.heldY = contentY
    }

    Column {
        id: emptyState

        // Groups that have games but are switched off
        readonly property var hiddenGroups: view.order.filter(k => view.groups[k].length > 0 && librarySettings.shownGroups.indexOf(
                                                                       k) < 0)

        function bodyText() {
            if (GamesFilterModel.scanning)
                return "Steam, Heroic, and itch are being read. Everything appears together when the last one finishes.";
            if (view.total === 0)
                return GamesFilterModel.search !== "" ? "Check the spelling, or clear the search to see your whole library." :
                                                        "Kaon looks for Steam, Heroic and itch libraries. Add a game by hand with the button below, or check the filters.";
            return "Turn on a group to see its games.";
        }

        function titleText() {
            if (GamesFilterModel.scanning)
                return "Looking through your libraries";
            if (view.total === 0)
                return GamesFilterModel.search !== "" ? "No games match “" + GamesFilterModel.search + "”" :
                                                        "No games found";
            return "Every group with games is switched off";
        }

        spacing: 8
        visible: list.count === 0
        width: view.inner
        x: Theme.pad
        y: top.height + 6

        Row {
            id: titleRow

            spacing: scanMark.visible ? 10 : 0
            width: parent.width

            Item {
                id: scanMark

                // Same arc the notch draws while a game is launching. The slot matches one line of the title.
                height: 22
                visible: GamesFilterModel.scanning
                width: visible ? 16 : 0

                Shape {
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    visible: scanMark.visible

                    RotationAnimation on rotation {
                        duration: 1000
                        from: 0
                        loops: Animation.Infinite
                        running: scanMark.visible && emptyState.visible
                        to: 360
                    }

                    ShapePath {
                        fillColor: "transparent"
                        strokeColor: Theme.glassLine
                        strokeWidth: 2.5

                        PathAngleArc {
                            centerX: scanMark.width / 2
                            centerY: scanMark.height / 2
                            radiusX: 6.5
                            radiusY: 6.5
                            sweepAngle: 360
                        }
                    }

                    ShapePath {
                        capStyle: ShapePath.RoundCap
                        fillColor: "transparent"
                        strokeColor: Theme.ledGreen
                        strokeWidth: 2.5

                        PathAngleArc {
                            centerX: scanMark.width / 2
                            centerY: scanMark.height / 2
                            radiusX: 6.5
                            radiusY: 6.5
                            startAngle: -90
                            sweepAngle: 100
                        }
                    }
                }
            }

            VText {
                font.pixelSize: 18
                font.weight: Font.ExtraBold
                text: emptyState.titleText()
                width: titleRow.width - scanMark.width - titleRow.spacing
                wrapMode: Text.Wrap
            }
        }

        VText {
            color: Theme.glassMuted
            font.pixelSize: 13
            text: emptyState.bodyText()
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
