import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

Item {
    id: view

    readonly property var extraMods: game ? (GameStatus.revision, GameStatus.extraMods(game)) : []
    readonly property Game game: Nav.game
    readonly property string group: game ? (GameStatus.revision, GameStatus.group(game)) : "none"
    property string heldId: ""
    property int heldStore: -1
    readonly property real heroH: Math.max(190, Math.min(height * 0.36, width * 0.25))
    readonly property bool launching: game !== null && Launcher.game === game && Launcher.phase !== Launcher.Idle
    readonly property Mod mod: game ? (GameStatus.revision, GameStatus.preferredMod(game)) : null
    property int popupEpoch: 0
    readonly property int remaining: steps.filter(s => s.state === "todo" || s.state === "busy" || s.state === "wait").length
    property bool showOtherMods: false
    // The loader hides this page without destroying it, so scroll and open menus survive a trip back to the library.
    readonly property bool shown: parent !== null && parent.visible
    readonly property var steps: game ? (GameStatus.revision, GameStatus.steps(game)) : []
    readonly property var vrMods: game ? (GameStatus.revision, GameStatus.vrMods(game)) : []

    // "Unreal Engine game on Steam", or "Game on Flatpak Steam" for a sandboxed install
    function describe(g) {
        if (!g)
            return "";
        const kind = g.type === Game.Demo ? "demo" : g.type === Game.App ? "application" : g.type === Game.Tool ? "tool" :
                                                                                                                  "game";


        const flatpak = g.flatpakAppId ? "Flatpak " : "";
        const where = g.store === Game.Steam ? "on " + flatpak + "Steam" : g.store === Game.Heroic ? "from " + flatpak
                                                                                                     + "Heroic" : g.store
                                                                                                     === Game.Itch
                                                                                                     ? "from itch" :
                                                                                                       "added by hand";
        const engine = engineName(g);
        return (engine !== "" ? engine + " " + kind : kind.charAt(0).toUpperCase() + kind.slice(1)) + " " + where;
    }

    function engineName(g) {
        return !g ? "" : g.engine === Game.Unreal ? "Unreal Engine" : g.engine === Game.Unity ? "Unity" : g.engine === Game.Godot
                                                                                                ? "Godot" : g.engine
                                                                                                  === Game.Source ? "Source" :
                                                                                                                    "";
    }

    function syncGame() {
        // game.store is an enum. A mixed enum-or-number temporary crashes Qt's compiled QML when stored in heldStore.
        if (!game) {
            if (heldId !== "")
                heldId = "";
            if (heldStore !== -1)
                heldStore = -1;
            return;
        }
        const store = Number(game.store);
        if (heldId === game.id && heldStore === store)
            return;
        heldId = game.id;
        heldStore = store;
        showOtherMods = false;
        if (pageFlick)
            pageFlick.contentY = 0;
    }

    Component.onCompleted: syncGame()
    onGameChanged: Qt.callLater(syncGame)
    onShownChanged: if (!shown)
                        popupEpoch += 1

    Flickable {
        id: pageFlick

        anchors.fill: parent
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        contentHeight: body.y + body.height + Theme.notchHeight + 36
        opacity: 1 - lensView.shown
        visible: view.game !== null

        ScrollBar.vertical: GlassScrollBar {
        }

        Item {
            id: hero

            height: view.heroH
            width: view.width

            Rectangle {
                anchors.fill: parent
                color: Theme.glassPanel
            }

            RoundedImage {
                id: heroArt

                anchors.fill: parent
                radius: 0
                source: view.game ? view.game.heroImage : ""
                sourceSize.width: 2400
            }

            Image {
                readonly property real pad: hero.width * 0.05

                asynchronous: true
                fillMode: Image.PreserveAspectFit
                height: hero.height * Math.max(25, view.game ? view.game.logoHeight : 40) / 100 * 0.7
                horizontalAlignment: view.game && view.game.logoHPosition === Game.Left ? Image.AlignLeft : view.game
                                                                                          && view.game.logoHPosition
                                                                                          === Game.Right ? Image.AlignRight :
                                                                                                           Image.AlignHCenter
                mipmap: true
                source: view.game ? view.game.logoImage : ""
                verticalAlignment: Image.AlignVCenter
                visible: heroArt.ready
                width: hero.width * Math.max(20, view.game ? view.game.logoWidth : 40) / 100 * 0.7
                x: view.game && view.game.logoHPosition === Game.Left ? pad : view.game && view.game.logoHPosition
                                                                        === Game.Right ? hero.width - width - pad : (
                                                                                             hero.width - width) / 2
                y: view.game && view.game.logoVPosition === Game.Top ? pad * 0.6 : (hero.height - height) / 2 - 14
            }

            Rectangle {
                anchors.bottom: parent.bottom
                height: parent.height * 0.6
                width: parent.width

                gradient: Gradient {
                    GradientStop {
                        color: "#00050608"
                        position: 0
                    }

                    GradientStop {
                        color: Theme.glass
                        position: 1
                    }
                }
            }

            VButton {
                icon: "back"
                small: true
                text: "Library"
                x: 14
                y: 14

                onClicked: Nav.goLibrary()

                Rectangle {
                    anchors.fill: parent
                    color: "#99050608"
                    radius: height / 2
                    z: -1
                }
            }

            Row {
                anchors.margins: 14
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: 4

                Repeater {
                    model: view.game ? [view.game.type === Game.Demo ? "Demo" : "", view.game.vrOnly ? "VR only" : ""].filter(
                                           t => t !== "") : []

                    Rectangle {
                        required property string modelData

                        color: "#d9050608"
                        height: 20
                        radius: 10
                        width: tagText.implicitWidth + 14

                        VText {
                            id: tagText

                            anchors.centerIn: parent
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            text: parent.modelData
                        }
                    }
                }
            }
        }

        Column {
            id: titleBlock

            spacing: 6
            width: view.width - 2 * Theme.pad
            x: Theme.pad
            y: view.heroH - 40

            VText {
                elide: Text.ElideRight
                font.pixelSize: 30
                font.weight: Font.ExtraBold
                maximumLineCount: 2
                text: view.game ? view.game.name : ""
                width: parent.width
                wrapMode: Text.Wrap
            }

            Flow {
                spacing: 9
                width: parent.width

                // a Flow ignores y, so the light sits in a box as tall as the text beside it
                Item {
                    height: statusText.height
                    width: 9

                    Led {
                        anchors.centerIn: parent
                        blinking: view.steps.some(s => s.state === "busy")
                        color: Theme.led(view.group)
                        size: 9
                    }
                }

                VText {
                    id: statusText

                    color: Theme.glassMuted
                    font.pixelSize: 14
                    rightPadding: 10
                    text: view.group === "ready" ? (view.mod.type === Mod.Launchable ? "Ready for VR with " + view.mod.name :
                                                                                       view.mod.name + " is installed") :
                                                   view.group === "setup" ? (view.remaining === 1 ? "One step before VR" :
                                                                                                    view.remaining
                                                                                                    + " steps before VR") :
                                                                            view.group === "native"
                                                                            ? "Has its own VR mode, so it doesn't need a mod" :
                                                                              (view.game ? (GameStatus.revision,
                                                                                            GameStatus.summary(view.game)) :
                                                                                           "")
                }

                VText {
                    color: Theme.glassFaint
                    font.pixelSize: 14
                    text: view.describe(view.game)
                }
            }
        }

        Item {
            id: body

            readonly property bool wide: view.width > 860

            height: Math.max(checks.height, mods.visible ? mods.y + mods.height : 0)
            width: view.width - 2 * Theme.pad
            x: Theme.pad
            y: titleBlock.y + titleBlock.height + 18

            Column {
                id: checks

                spacing: 0
                width: body.wide && mods.visible ? body.width * 0.56 : body.width

                VText {
                    color: Theme.glassMuted
                    font.pixelSize: 14
                    lineHeight: 1.4
                    text: !view.game ? "" : view.group === "native" ? view.game.name
                                                                      + " ships with VR support. Its own VR mode will look and run better than a mod, so the button below just starts the game." :
                                                                      view.game.noWindowsSupport
                                                                      ? "VR mods attach to a game's Windows build, and "
                                                                        + view.game.name
                                                                        + " only ships for other platforms." :
                                                                        "No Kaon mod supports " + (view.engineName(
                                                                                                       view.game) === ""
                                                                                                   ? "this game's engine" :
                                                                                                     view.engineName(
                                                                                                         view.game)
                                                                                                     + " games")
                                                                        + " yet. You can still play it normally."
                    visible: view.group === "native" || view.group === "none"
                    width: parent.width
                    wrapMode: Text.Wrap
                }

                Repeater {
                    model: view.group === "native" || view.group === "none" ? [] : view.steps

                    Item {
                        id: row

                        required property int index
                        required property var modelData

                        height: Math.max(50, rowText.height + 18)
                        width: checks.width

                        Rectangle {
                            color: Theme.glassLine
                            height: 1
                            visible: row.index > 0
                            width: parent.width
                        }

                        Led {
                            blinking: row.modelData.state === "busy"
                            color: Theme.stepLed(row.modelData.state)
                            size: 9
                            x: 3
                            y: 16
                        }

                        Column {
                            id: rowText

                            spacing: 1
                            width: parent.width - 28 - (actions.width > 0 ? actions.width + 14 : 0)
                            x: 28
                            y: 9

                            VText {
                                elide: Text.ElideRight
                                font.pixelSize: 14
                                font.weight: Font.Bold
                                text: row.modelData.title
                                width: parent.width
                            }

                            VText {
                                color: Theme.glassMuted
                                font.pixelSize: 13
                                font.weight: Font.Normal
                                text: row.modelData.detail
                                width: parent.width
                                wrapMode: Text.Wrap
                            }
                        }

                        Row {
                            id: actions

                            anchors.right: parent.right
                            spacing: 6
                            y: 9

                            VButton {
                                small: true
                                text: row.modelData.secondaryLabel ?? ""
                                visible: !!row.modelData.secondaryAction

                                onClicked: GameStatus.runStep(view.game, row.modelData.key, true)
                            }

                            VButton {
                                small: true
                                solid: row.modelData.state === "todo"
                                text: row.modelData.actionLabel ?? ""
                                visible: !!row.modelData.action

                                onClicked: GameStatus.runStep(view.game, row.modelData.key, false)
                            }
                        }
                    }
                }

                // For games whose engine may have been misdetected: launchable mods can be opened anyway
                Column {
                    spacing: 8
                    topPadding: 16
                    visible: view.group === "none" && view.game !== null && !view.game.noWindowsSupport
                             && view.game.hasValidWine()
                    width: parent.width

                    VButton {
                        small: true
                        text: view.showOtherMods ? "Hide" : "Engine looks wrong? Open a mod anyway"

                        onClicked: view.showOtherMods = !view.showOtherMods
                    }

                    Repeater {
                        model: view.showOtherMods ? GameStatus.allMods().filter(m => m.providesVr && m.type
                                                                                     === Mod.Launchable) : []

                        Row {
                            required property var modelData

                            spacing: 10

                            VButton {
                                small: true
                                text: "Open " + parent.modelData.name

                                onClicked: {
                                    const r = parent.modelData.currentRelease;
                                    if (r && r.downloaded)
                                        parent.modelData.launchMod(view.game);
                                    else
                                        Nav.notify("Download " + parent.modelData.name + " on the Mods page first");
                                }
                            }

                            VText {
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.glassFaint
                                font.pixelSize: 12
                                text: "It may not work, and it needs its own requirements installed."
                            }
                        }
                    }
                }
            }

            Column {
                id: mods

                spacing: 8
                visible: view.vrMods.length > 0 || view.extraMods.length > 0
                width: body.wide ? body.width * 0.44 - 28 : body.width
                x: body.wide ? body.width - width : 0
                y: body.wide ? 0 : checks.height + 22

                VText {
                    color: Theme.glassFaint
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    text: "Play with"
                    visible: view.vrMods.length > 1
                }

                Repeater {
                    model: view.vrMods

                    Rectangle {
                        id: modRow

                        required property var modelData
                        readonly property bool selected: view.mod === modelData

                        border.color: selected ? Theme.glassMuted : Theme.glassLine
                        border.width: 1.5
                        color: selected ? Theme.glassPanel : "transparent"
                        height: modCol.height + 22
                        radius: 16
                        width: mods.width

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: view.vrMods.length > 1 ? Qt.PointingHandCursor : Qt.ArrowCursor

                            onClicked: GameStatus.setPreferredMod(view.game, modRow.modelData)
                        }

                        Column {
                            id: modCol

                            spacing: 4
                            width: parent.width - 28
                            x: 14
                            y: 11

                            // Above the card's click target, so the launch-option text can be selected.
                            z: 1

                            Item {
                                height: 32
                                width: parent.width

                                // The name picks this mod. The version menu beside it is its own control.
                                Item {
                                    id: modName

                                    activeFocusOnTab: view.vrMods.length > 1
                                    anchors.left: parent.left
                                    anchors.right: versions.left
                                    anchors.rightMargin: 8
                                    height: 32

                                    Keys.onReturnPressed: GameStatus.setPreferredMod(view.game, modRow.modelData)
                                    Keys.onSpacePressed: GameStatus.setPreferredMod(view.game, modRow.modelData)

                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: -4
                                        border.color: Theme.ledBlue
                                        border.width: 2
                                        color: "transparent"
                                        radius: height / 2
                                        visible: modName.activeFocus
                                    }

                                    VText {
                                        anchors.verticalCenter: parent.verticalCenter
                                        elide: Text.ElideRight
                                        font.pixelSize: 15
                                        font.weight: Font.ExtraBold
                                        text: modRow.modelData.name
                                        width: parent.width
                                    }
                                }

                                VersionMenu {
                                    id: versions

                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    game: view.game
                                    mod: modRow.modelData
                                    popupEpoch: view.popupEpoch
                                }
                            }

                            VText {
                                color: Theme.glassMuted
                                font.pixelSize: 13
                                font.weight: Font.Normal
                                lineHeight: 1.3
                                linkColor: Theme.ledBlue
                                text: Theme.linkify(modRow.modelData.description + (modRow.modelData.info !== "" ? " "
                                                                                                                   + modRow.modelData.info :
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
                                options: modRow.modelData.launchOptions
                            }

                            VButton {
                                small: true
                                text: "Open " + modRow.modelData.name + " without starting the game"
                                visible: modRow.selected && view.group === "ready" && modRow.modelData.type
                                         === Mod.Launchable && !view.launching

                                onClicked: modRow.modelData.launchMod(view.game)
                            }
                        }
                    }
                }

                VText {
                    color: Theme.glassFaint
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    text: "Also available"
                    topPadding: 8
                    visible: view.extraMods.length > 0
                }

                Repeater {
                    model: view.extraMods

                    Item {
                        id: extraRow

                        readonly property bool installed: (GameStatus.revision, modelData.isInstalledForGame(view.game))
                        required property var modelData

                        height: 44
                        width: mods.width

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - extraButton.width - 12

                            VText {
                                font.pixelSize: 14
                                font.weight: Font.Bold
                                text: extraRow.modelData.name
                            }

                            VText {
                                color: Theme.glassMuted
                                elide: Text.ElideRight
                                font.pixelSize: 12
                                text: extraRow.modelData.description
                                width: parent.width
                            }
                        }

                        VButton {
                            id: extraButton

                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            small: true
                            text: extraRow.installed ? "Uninstall" : "Install"

                            onClicked: {
                                const m = extraRow.modelData;
                                if (extraRow.installed)
                                    m.uninstallMod(view.game);
                                else if (m.currentRelease && m.currentRelease.downloaded)
                                    m.installMod(view.game);
                                else
                                    GameStatus.download(m, m.currentRelease);
                            }
                        }
                    }
                }
            }
        }
    }

    LensView {
        id: lensView

        anchors.fill: parent
        game: view.game
        shown: view.launching ? 1 : 0
    }
}
