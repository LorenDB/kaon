import QtCore
import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// Every mod Kaon knows, one line each: whether it is on disk, what it is for, and which version games get. A line opens
// for the rest. The pieces Kaon installs by itself sit at the bottom, folded away.
Flickable {
    id: view

    // settingsGroup of the mod whose line is open, or empty
    property string openMod: ""
    readonly property var sections: {
        const all = GameStatus.allMods();
        const vr = all.filter(mod => mod.providesVr);
        const forEngine = engine => vr.filter(mod => mod.supportsEngine(engine));
        return [
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
                        "mods": vr.filter(mod => !mod.supportsEngine(Game.Unreal) && !mod.supportsEngine(Game.Unity) &&
                                                 !mod.supportsEngine(Game.Source) && !mod.supportsEngine(Game.Godot))
                    },
                    {
                        "title": "Optional tools",
                        "note": "Each game has its own switch for these, on that game's page.",
                        "mods": all.filter(mod => mod.optional)
                    },
                    {
                        "title": "Installed for you",
                        "note": "Kaon puts these into a game when one of the mods above needs them.",
                        "mods": all.filter(mod => !mod.providesVr && !mod.optional),
                        "folds": true
                    }
                ].filter(section => section.mods.length > 0);
    }
    readonly property bool wide: width > 720

    // A mouse scrolls with its wheel. Dragging is left for selecting text.
    acceptedButtons: Qt.NoButton
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    contentHeight: col.height + Theme.notchClearance + 60

    ScrollBar.vertical: GlassScrollBar {
    }

    Settings {
        id: saved

        property bool supportOpen: false

        category: "mods"
    }

    Column {
        id: col

        spacing: 22
        width: Math.min(view.width - 2 * Theme.pad, 980)
        x: Math.max(Theme.pad, (view.width - width) / 2)
        y: 20

        VText {
            color: Theme.glassMuted
            font.pixelSize: 13
            text: "The version picked here is the one every game gets. A green light means it is on disk."
            width: parent.width
            wrapMode: Text.Wrap
        }

        Repeater {
            model: view.sections

            Column {
                id: section

                required property var modelData
                readonly property bool open: !modelData.folds || saved.supportOpen

                spacing: 8
                width: col.width

                Item {
                    id: heading

                    activeFocusOnTab: !!section.modelData.folds
                    height: 24
                    width: parent.width

                    Keys.onReturnPressed: saved.supportOpen = !saved.supportOpen
                    Keys.onSpacePressed: saved.supportOpen = !saved.supportOpen

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
                        visible: !!section.modelData.folds
                    }

                    VText {
                        id: sectionTitle

                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.glassFaint
                        font.pixelSize: 12
                        font.weight: Font.Bold
                        text: section.modelData.title + (section.open ? "" : "  " + section.modelData.mods.length)
                        x: fold.visible ? 22 : 0
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
                        enabled: !!section.modelData.folds

                        onClicked: saved.supportOpen = !saved.supportOpen
                    }
                }

                Rectangle {
                    border.color: Theme.glassLine
                    border.width: 1.5
                    color: Theme.glassPanel
                    height: rows.height
                    radius: 16
                    visible: section.open
                    width: parent.width

                    Column {
                        id: rows

                        width: parent.width

                        Repeater {
                            model: section.open ? section.modelData.mods : []

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
                                width: rows.width

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

                                        anchors.verticalCenter: parent.verticalCenter
                                        elide: Text.ElideRight
                                        font.pixelSize: 15
                                        font.weight: Font.ExtraBold
                                        text: row.modelData.name
                                        width: Math.min(implicitWidth, versions.x - x - 12)
                                        x: 40
                                    }

                                    VText {
                                        anchors.left: modName.right
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
                                        text: Theme.linkify(row.modelData.description + (row.modelData.info !== "" ? " "
                                                                                                                     + row.modelData.info :
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
                                        options: row.open ? row.modelData.launchOptions : ""
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
                                            cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        }
                                    }

                                    VText {
                                        color: Theme.glassFaint
                                        font.pixelSize: 12
                                        text: (row.stored === 0 ? "Nothing on disk" : row.stored === 1 ? "1 version on disk" :
                                                                                                         row.stored
                                                                                                         + " versions on disk")
                                              + (row.modelData.launchOptions !== ""
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
