import QtCore
import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// Every mod Kaon knows, one line each: whether it is on disk, what it is for, and which version games get. A line opens
// for the rest. Each section folds on its own, and that choice is remembered. Prerequisites starts folded. Universal
// mods cover every game on an engine, and say which engine. Game-specific ones are written for particular games.
// Optional tools are switched per game. "Hide mods for games I don't own" drops a game-specific mod when that game
// isn't in the library. Engine-wide mods stay, since they aren't written for one game.
Flickable {
    id: view

    // settingsGroup of the mod whose line is open, or empty
    property string openMod: ""
    readonly property var sections: {
        const all = GameStatus.allMods();
        const universal = all.filter(mod => mod.providesVr && !mod.gameSpecific && !mod.optional);
        const forEngine = engine => universal.filter(mod => mod.supportsEngine(engine));
        const named = [Game.Unreal, Game.Unity, Game.Source, Game.Godot];
        return [
                    {
                        "id": "universal",
                        "title": "Universal mods",
                        "note": "One mod covers every game on that engine.",
                        // Engine groups inside the card. The other sections are one flat list each.
                        "groups": [
                            {
                                "title": "Unreal Engine",
                                "mods": forEngine(Game.Unreal)
                            },
                            {
                                "title": "Unity",
                                "mods": forEngine(Game.Unity)
                            },
                            {
                                "title": "Source",
                                "mods": forEngine(Game.Source)
                            },
                            {
                                "title": "Godot",
                                "mods": forEngine(Game.Godot)
                            },
                            {
                                "title": "Other engines",
                                "mods": universal.filter(mod => !named.some(engine => mod.supportsEngine(engine)))
                            }
                        ].filter(group => group.mods.length > 0)
                    },
                    {
                        "id": "games",
                        "title": "Game-specific mods",
                        "note": "Made for particular games.",
                        "groups": [
                            {
                                "mods": all.filter(mod => mod.providesVr && mod.gameSpecific && !mod.optional && view.listed(
                                                              mod))
                            }
                        ]
                    },
                    {
                        "id": "tools",
                        "title": "Optional tools",
                        "note": "Each game has its own switch for these, on that game's page.",
                        "groups": [
                            {
                                "mods": all.filter(mod => mod.optional)
                            }
                        ]
                    },
                    {
                        "id": "prerequisites",
                        "title": "Prerequisites",
                        "note": "Kaon puts these into a game when one of the mods above needs them.",
                        "groups": [
                            {
                                "mods": all.filter(mod => !mod.providesVr && !mod.optional)
                            }
                        ]
                    }
                ].filter(section => view.modCount(section) > 0);
    }
    readonly property bool wide: width > 720

    // Game-specific mods can be limited to games in the library. Anything else is always listed.
    function listed(mod) {
        if (!saved.hideUnowned || !mod.gameSpecific)
            return true;
        // Read these so the list is built again when a scan publishes. A scan that hasn't published yet has nothing
        // to match against, and hiding now would clear the list and fill it back in a moment later.
        const revision = GamesFilterModel.libraryRevision;
        if (GamesFilterModel.scanning && revision === 0)
            return true;
        for (const game of GamesFilterModel.library())
            if (mod.targetsGame(game))
                return true;
        return false;
    }

    // How many mods a section holds, across its engine groups
    function modCount(section) {
        let n = 0;
        for (const group of section.groups)
            n += group.mods.length;
        return n;
    }

    function setFolded(id, fold) {
        let folded = saved.foldedSections.slice();
        const i = folded.indexOf(id);
        if (fold && i < 0)
            folded.push(id);
        else if (!fold && i >= 0)
            folded.splice(i, 1);
        else
            return;
        saved.foldedSections = folded;
    }

    function toggleSection(id) {
        setFolded(id, saved.foldedSections.indexOf(id) < 0);
    }

    // A mouse scrolls with its wheel. Dragging is left for selecting text.
    acceptedButtons: Qt.NoButton
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    contentHeight: col.height + Theme.notchClearance + 60

    ScrollBar.vertical: GlassScrollBar {
    }

    Component.onCompleted: {
        if (saved.foldMigrated)
            return;
        saved.foldMigrated = true;
        if (saved.supportOpen)
            view.setFolded("prerequisites", false);
    }

    Settings {
        id: saved

        property bool foldMigrated: false

        // Ids of folded sections. A section missing from the list stays open, so a new one starts open.
        property var foldedSections: ["prerequisites"]
        property bool hideUnowned: false
        // The old Prerequisites fold. Read once so a section someone had opened stays open.
        property bool supportOpen: false

        category: "mods"
    }

    Column {
        id: col

        spacing: 22
        width: Math.min(view.width - 2 * Theme.pad, 980)
        x: Math.max(Theme.pad, (view.width - width) / 2)
        y: 20

        Column {
            spacing: 14
            width: parent.width

            VText {
                color: Theme.glassMuted
                font.pixelSize: 13
                text: "Use this page to manage installations and set the default version for new games."
                width: parent.width
                wrapMode: Text.Wrap
            }

            Item {
                height: 32
                width: parent.width

                VText {
                    anchors.left: parent.left
                    anchors.right: hideSwitch.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    font.pixelSize: 14
                    text: "Hide mods for games I don't own"
                }

                MouseArea {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: hideSwitch.left
                    anchors.top: parent.top
                    cursorShape: Qt.PointingHandCursor

                    onClicked: saved.hideUnowned = !saved.hideUnowned
                }

                ToggleSwitch {
                    id: hideSwitch

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: saved.hideUnowned

                    onToggled: saved.hideUnowned = !saved.hideUnowned
                }
            }
        }

        Repeater {
            model: view.sections

            Column {
                id: section

                required property var modelData
                readonly property bool open: saved.foldedSections.indexOf(modelData.id) < 0

                spacing: 8
                width: col.width

                Item {
                    id: heading

                    activeFocusOnTab: true
                    height: 24
                    width: parent.width

                    Keys.onReturnPressed: view.toggleSection(section.modelData.id)
                    Keys.onSpacePressed: view.toggleSection(section.modelData.id)

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4
                        border.color: Theme.ledBlue
                        border.width: 2
                        color: "transparent"
                        radius: 10
                        visible: heading.activeFocus
                    }

                    Icon {
                        id: fold

                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.glassFaint
                        name: section.open ? "down" : "forward"
                        size: 14
                        stroke: 2.2
                    }

                    VText {
                        id: sectionTitle

                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.glassFaint
                        font.pixelSize: 12
                        font.weight: Font.Bold
                        text: section.modelData.title + (section.open ? "" : "  " + view.modCount(section.modelData))
                        x: 22
                    }

                    VText {
                        anchors.left: sectionTitle.right
                        anchors.leftMargin: 12
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.glassFaint
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        text: section.modelData.note ?? ""
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor

                        onClicked: view.toggleSection(section.modelData.id)
                    }
                }

                Rectangle {
                    border.color: Theme.glassLine
                    border.width: 1.5
                    color: Theme.glassPanel
                    height: groupsCol.height
                    radius: 16
                    visible: section.open
                    width: parent.width

                    Column {
                        id: groupsCol

                        width: parent.width

                        Repeater {
                            model: section.open ? section.modelData.groups : []

                            Column {
                                id: group

                                required property int index
                                required property var modelData

                                width: groupsCol.width

                                // A line between engine groups. A flat section has one group, so it stays a plain list.
                                Item {
                                    height: 13
                                    visible: group.index > 0
                                    width: parent.width

                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: Theme.glassLine
                                        height: 1
                                        width: parent.width - 28
                                        x: 14
                                    }
                                }

                                Item {
                                    height: group.modelData.title ? 30 : 0
                                    visible: !!group.modelData.title
                                    width: parent.width

                                    VText {
                                        anchors.bottom: parent.bottom
                                        anchors.bottomMargin: 4
                                        color: Theme.glassFaint
                                        font.pixelSize: 11
                                        font.weight: Font.Bold
                                        text: group.modelData.title ?? ""
                                        x: 18
                                    }
                                }

                                Repeater {
                                    model: group.modelData.mods

                                    Item {
                                        id: row

                                        required property int index
                                        required property var modelData
                                        readonly property bool open: view.openMod === modelData.settingsGroup
                                        readonly property int stored: modelData.downloadedCount

                                        function toggle() {
                                            view.openMod = open ? "" : modelData.settingsGroup;
                                        }

                                        height: 54 + (open ? details.height + 16 : 0)
                                        width: parent.width

                                        Rectangle {
                                            color: Theme.glassLine
                                            height: 1
                                            visible: row.index > 0
                                            width: parent.width - 28
                                            x: 14
                                        }

                                        // The line itself opens and closes. The version menu on it is its own control.
                                        Item {
                                            id: line

                                            activeFocusOnTab: true
                                            height: 54
                                            width: parent.width

                                            Keys.onReturnPressed: row.toggle()
                                            Keys.onSpacePressed: row.toggle()

                                            Rectangle {
                                                anchors.fill: parent
                                                anchors.margins: 4
                                                border.color: line.activeFocus ? Theme.ledBlue : "transparent"
                                                border.width: 2
                                                color: lineMouse.containsMouse ? Theme.glassHover : "transparent"
                                                radius: 12
                                            }

                                            MouseArea {
                                                id: lineMouse

                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                hoverEnabled: true

                                                onClicked: row.toggle()
                                            }

                                            Led {
                                                anchors.verticalCenter: parent.verticalCenter
                                                color: row.stored > 0 ? Theme.ledGreen : Theme.ledOff
                                                size: 9
                                                x: 18
                                            }

                                            VText {
                                                id: modName

                                                ToolTip.delay: 500
                                                ToolTip.text: row.modelData.name
                                                ToolTip.visible: truncated && lineMouse.containsMouse
                                                anchors.verticalCenter: parent.verticalCenter
                                                elide: Text.ElideRight
                                                font.pixelSize: 15
                                                font.weight: Font.ExtraBold
                                                text: row.modelData.name
                                                width: Math.max(0, Math.min(implicitWidth, versions.x - x - 12 - (platformTag.fits
                                                                                                                  ? platformTag.implicitWidth
                                                                                                                    + 10 : 0)))
                                                x: 40
                                            }

                                            // The builds of a game the mod works with. The open line says more. It is
                                            // left out where the name would have to be cut short for it.
                                            VText {
                                                id: platformTag

                                                readonly property bool fits: modName.x + modName.implicitWidth + 10 + implicitWidth
                                                                             + 12 <= versions.x

                                                anchors.left: modName.right
                                                anchors.leftMargin: fits ? 10 : 0
                                                anchors.verticalCenter: parent.verticalCenter
                                                color: Theme.glassFaint
                                                font.pixelSize: 12
                                                font.weight: Font.Bold
                                                text: row.modelData.platformNames.join(" · ")
                                                visible: fits
                                                width: fits ? implicitWidth : 0
                                            }

                                            VText {
                                                ToolTip.delay: 500
                                                ToolTip.text: row.modelData.description
                                                ToolTip.visible: truncated && lineMouse.containsMouse
                                                anchors.left: platformTag.right
                                                anchors.leftMargin: 14
                                                anchors.right: versions.left
                                                anchors.rightMargin: 14
                                                anchors.verticalCenter: parent.verticalCenter
                                                color: Theme.glassMuted
                                                elide: Text.ElideRight
                                                font.pixelSize: 13
                                                font.weight: Font.Normal
                                                text: row.modelData.description
                                                visible: view.wide && !row.open
                                            }

                                            VersionMenu {
                                                id: versions

                                                anchors.right: chevron.left
                                                anchors.rightMargin: 10
                                                anchors.verticalCenter: parent.verticalCenter
                                                mod: row.modelData
                                            }

                                            Icon {
                                                id: chevron

                                                anchors.right: parent.right
                                                anchors.rightMargin: 16
                                                anchors.verticalCenter: parent.verticalCenter
                                                color: Theme.glassFaint
                                                name: row.open ? "up" : "down"
                                                size: 14
                                                stroke: 2.2
                                            }
                                        }

                                        Column {
                                            id: details

                                            spacing: 10
                                            visible: row.open
                                            width: parent.width - x - 18
                                            x: 40
                                            y: 50

                                            VText {
                                                color: Theme.glassMuted
                                                font.pixelSize: 13
                                                font.weight: Font.Normal
                                                lineHeight: 1.3
                                                linkColor: Theme.ledBlue
                                                text: Theme.linkify(row.modelData.description + (row.modelData.info !== ""
                                                                                                 ? " " + row.modelData.info :
                                                                                                   ""))
                                                textFormat: Text.StyledText
                                                width: parent.width
                                                wrapMode: Text.Wrap

                                                onLinkActivated: link => Qt.openUrlExternally(link)

                                                HoverHandler {
                                                    cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor :
                                                                                             Qt.ArrowCursor
                                                }
                                            }

                                            LaunchOptions {
                                                options: row.open ? row.modelData.launchOptions : ""
                                            }

                                            VText {
                                                color: Theme.glassFaint
                                                font.pixelSize: 12
                                                text: row.modelData.platformNote
                                                width: parent.width
                                                wrapMode: Text.Wrap
                                            }

                                            VText {
                                                color: Theme.glassFaint
                                                font.pixelSize: 12
                                                linkColor: Theme.ledBlue
                                                text: row.modelData.homepage !== "" ? '<a href="' + row.modelData.homepage
                                                                                      + '">Project page</a>' : ""
                                                textFormat: Text.StyledText
                                                visible: row.modelData.homepage !== ""
                                                width: parent.width
                                                wrapMode: Text.Wrap

                                                onLinkActivated: link => Qt.openUrlExternally(link)

                                                HoverHandler {
                                                    cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor :
                                                                                             Qt.ArrowCursor
                                                }
                                            }

                                            VText {
                                                color: Theme.glassFaint
                                                font.pixelSize: 12
                                                text: (row.stored === 0 ? "Nothing on disk" : row.stored === 1
                                                                          ? "1 version on disk" : row.stored
                                                                            + " versions on disk") + (
                                                          row.modelData.launchOptions !== ""
                                                          ? ". A game's page shows these launch options together with the ones the game already has." :
                                                            "")
                                                width: parent.width
                                                wrapMode: Text.Wrap
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
