import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// One game: what stands between it and VR on the left, what it is played with on the right.
Item {
    id: view

    // The mod the card on the right shows. It holds on to the last one while there is none, so that the card's bindings
    // have something to read until its loader has taken the card away.
    property Mod cardMod: null
    // Installed mods that put a config file in the game. revision keeps this live across installs.
    readonly property var configMods: {
        const revision = GameStatus.revision;
        if (!game)
            return [];
        return GameStatus.allMods().filter(mod => ModConfigs.available(mod, game, revision));
    }
    readonly property Game game: Nav.game
    readonly property string group: game ? GameStatus.group(game, GameStatus.revision) : "none"
    property string heldId: ""
    property int heldStore: -1
    // Without art there is nothing to show up there, and the title moves up under the way back
    readonly property real heroH: game && game.heroImage !== "" ? Math.max(190, Math.min(height * 0.36, width * 0.25)) : 104
    readonly property bool launching: game !== null && Launcher.game === game && Launcher.phase !== Launcher.Idle
    // A game with its own VR mode is not offered a mod, even when one would fit its engine
    readonly property Mod mod: game && group !== "native" ? GameStatus.preferredMod(game, GameStatus.revision) : null
    // The first check that isn't settled, which is the one everything after it is waiting for
    readonly property string openKey: steps.find(s => s.state !== "ok" && s.state !== "warn")?.key ?? ""
    property int popupEpoch: 0
    readonly property int remaining: steps.filter(s => s.state === "todo" || s.state === "busy" || s.state === "wait").length
    property bool showOtherMods: false
    // The loader hides this page without destroying it, so scroll and open menus survive a trip back to the library.
    readonly property bool shown: parent !== null && parent.visible
    readonly property string statusLine: {
        if (!game)
            return "";
        if (group === "ready" && mod) {
            // Only the mod's own options are worth a warning up here; a tool's are in the list below
            if (mod.launchOptions !== "" && steps.some(s => s.key === "launchOptions" && s.state === "warn"))
                return mod.name + " is in place. Set the launch options below before you play.";
            return mod.type === Mod.Launchable ? "Ready for VR with " + mod.name : mod.name + " is installed";
        }
        if (group === "setup")
            return remaining === 1 ? "One step before VR" : remaining + " steps before VR";
        if (group === "native")
            return "Has its own VR mode, so it doesn't need a mod";
        return GameStatus.summary(game, GameStatus.revision);
    }
    readonly property var steps: game ? GameStatus.steps(game, GameStatus.revision) : []
    readonly property var tools: game ? GameStatus.tools(game, GameStatus.revision) : []
    readonly property var vrMods: game ? GameStatus.vrMods(game, GameStatus.revision) : []

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

    Binding {
        property: "cardMod"
        restoreMode: Binding.RestoreNone
        target: view
        value: view.mod
        when: view.mod !== null
    }

    Flickable {
        id: pageFlick

        // How much of the top the way back covers, for whatever scrolls a focused item into view
        readonly property real pinnedTop: 60

        // A mouse scrolls with its wheel. Dragging is left for selecting the text on the page.
        // While another page covers this one, leave the wheel to that page. This offset stays where it was.
        acceptedButtons: Qt.NoButton
        anchors.fill: parent
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        contentHeight: body.y + body.height + Theme.notchHeight + 36
        interactive: view.shown
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
                    text: view.statusLine
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

            height: Math.max(checks.height, side.visible ? side.y + side.height : 0)
            width: view.width - 2 * Theme.pad
            x: Theme.pad
            y: titleBlock.y + titleBlock.height + 18

            // ------------------------------------------------------------ what stands between the game and VR
            Column {
                id: checks

                spacing: 0
                width: body.wide && side.visible ? body.width * 0.56 : body.width

                VText {
                    bottomPadding: 12
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
                                                                                                     + " games like this one")
                                                                        + " yet. You can still play it normally."
                    visible: view.group === "native" || view.group === "none"
                    width: parent.width
                    wrapMode: Text.Wrap
                }

                // Like the list on the Mods page: one panel, one line per check. A check that passed stays quiet.
                // One that needs something opens for its explanation and the button that deals with it.
                Rectangle {
                    border.color: Theme.glassLine
                    border.width: 1.5
                    color: Theme.glassPanel
                    height: rows.height
                    radius: 16
                    visible: checklistRepeater.count > 0
                    width: checks.width

                    Column {
                        id: rows

                        width: parent.width

                        Repeater {
                            id: checklistRepeater

                            // A game that needs no mod has no checklist, but a tool switched on for it can still ask for
                            // launch options
                            model: view.group === "native" || view.group === "none" ? view.steps.filter(s => s.key === "launchOptions") :
                                                                                      view.steps

                            Item {
                                id: row

                                required property int index
                                required property var modelData
                                readonly property bool open: !passed
                                // Also one line: a check that can only wait for an earlier one, with nothing to press. The
                                // first one that waits is what the page is stuck on, and says so in full.
                                readonly property bool passed: modelData.state === "ok" || (modelData.state === "wait" &&
                                                                                            !modelData.action
                                                                                            && modelData.key
                                                                                            !== view.openKey)
                                // Launch options to copy into Steam, when that is what this step asks for
                                readonly property string paste: modelData.copyText ?? ""

                                height: 54 + (open ? details.height + 16 : 0)
                                width: rows.width

                                Rectangle {
                                    color: Theme.glassLine
                                    height: 1
                                    visible: row.index > 0
                                    width: parent.width - 28
                                    x: 14
                                }

                                Item {
                                    id: line

                                    height: 54
                                    width: parent.width

                                    Led {
                                        anchors.verticalCenter: parent.verticalCenter
                                        blinking: row.modelData.state === "busy"
                                        color: Theme.stepLed(row.modelData.state)
                                        size: 9
                                        x: 18
                                    }

                                    VText {
                                        id: rowTitle

                                        anchors.verticalCenter: parent.verticalCenter
                                        elide: Text.ElideRight
                                        font.pixelSize: 15
                                        font.weight: Font.ExtraBold
                                        text: row.modelData.title
                                        width: Math.min(implicitWidth, actions.x - x - 12)
                                        x: 40
                                    }

                                    VText {
                                        anchors.left: rowTitle.right
                                        anchors.leftMargin: 14
                                        anchors.right: actions.left
                                        anchors.rightMargin: 14
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: Theme.glassMuted
                                        elide: Text.ElideRight
                                        font.pixelSize: 13
                                        font.weight: Font.Normal
                                        text: row.modelData.detail
                                        visible: row.passed
                                    }

                                    Row {
                                        id: actions

                                        anchors.right: parent.right
                                        anchors.rightMargin: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 6

                                        VButton {
                                            quiet: row.passed
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
                                        text: row.modelData.detail
                                        width: parent.width
                                        wrapMode: Text.Wrap
                                    }

                                    LaunchOptions {
                                        options: row.paste
                                        showLabel: false
                                    }
                                }
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

                        Flow {
                            id: otherMod

                            required property var modelData

                            spacing: 10
                            width: parent.width

                            VButton {
                                small: true
                                text: "Open " + otherMod.modelData.name

                                onClicked: {
                                    const r = otherMod.modelData.currentRelease;
                                    if (r && r.downloaded)
                                        otherMod.modelData.launchMod(view.game);
                                    else
                                        Nav.notify("Download " + otherMod.modelData.name + " on the Mods page first");
                                }
                            }

                            VText {
                                color: Theme.glassFaint
                                font.pixelSize: 12
                                height: 32
                                text: "It may not work, and it needs its own requirements installed."
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }

            // ------------------------------------------------------------ what the game is played with
            Column {
                id: side

                spacing: 12
                visible: view.mod !== null || view.tools.length > 0 || view.configMods.length > 0
                width: body.wide ? body.width * 0.44 - 28 : body.width
                x: body.wide ? body.width - width : 0
                y: body.wide ? 4 : checks.height + 22

                // Only the mod in use is spelled out. The others are a click away, not a card each.
                Flow {
                    spacing: 10
                    visible: view.mod !== null && view.vrMods.length > 1
                    width: parent.width

                    VText {
                        color: Theme.glassFaint
                        font.pixelSize: 12
                        font.weight: Font.Bold
                        height: 32
                        text: "Play with"
                        verticalAlignment: Text.AlignVCenter
                    }

                    Segmented {
                        current: view.mod ? view.mod.settingsGroup : ""
                        options: view.vrMods.map(m => ({
                            "id": m.settingsGroup,
                            "label": m.name
                        }))

                        onPicked: key => GameStatus.setPreferredMod(view.game, view.vrMods.find(m => m.settingsGroup
                                                                                                     === key))

                    }
                }

                Loader {
                    active: view.mod !== null && view.cardMod !== null
                    sourceComponent: modCard
                    visible: active
                    width: parent.width
                }

                Column {
                    spacing: 8
                    visible: view.configMods.length > 0
                    width: parent.width

                    VText {
                        color: Theme.glassFaint
                        font.pixelSize: 12
                        font.weight: Font.Bold
                        text: "Configure"
                    }

                    Repeater {
                        model: view.configMods

                        VButton {
                            required property var modelData

                            icon: "sliders"
                            small: true
                            text: modelData.name

                            onClicked: Nav.openModConfig(modelData)
                        }
                    }
                }

                ToolsPanel {
                    game: view.game
                    tools: view.tools
                    width: side.width
                }
            }
        }
    }

    Component {
        id: modCard

        Rectangle {
            border.color: Theme.glassLine
            border.width: 1.5
            color: Theme.glassPanel
            height: modCol.height + 26
            radius: 16

            Column {
                id: modCol

                spacing: 8
                width: parent.width - 28
                x: 14
                y: 12

                Item {
                    height: 32
                    width: parent.width

                    VText {
                        anchors.left: parent.left
                        anchors.right: versions.left
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight
                        font.pixelSize: 16
                        font.weight: Font.ExtraBold
                        text: view.cardMod.name
                    }

                    VersionMenu {
                        id: versions

                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        game: view.game
                        mod: view.cardMod
                        popupEpoch: view.popupEpoch
                    }
                }

                VText {
                    color: Theme.glassMuted
                    font.pixelSize: 13
                    font.weight: Font.Normal
                    lineHeight: 1.3
                    linkColor: Theme.ledBlue
                    text: Theme.linkify(view.cardMod.description + (view.cardMod.info !== "" ? " " + view.cardMod.info : ""))
                    textFormat: Text.StyledText
                    width: parent.width
                    wrapMode: Text.Wrap

                    onLinkActivated: link => Qt.openUrlExternally(link)

                    HoverHandler {
                        cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
                }

                // How long Play in VR waits before it opens an injector. Games differ in how long they take to load.
                Row {
                    spacing: 8
                    visible: GameStatus.usesLaunchDelay(view.game, GameStatus.revision)

                    VText {
                        ToolTip.delay: 500
                        ToolTip.text: "How long Play in VR gives the game to load before it opens " + view.cardMod.name
                        ToolTip.visible: delayHover.hovered
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.glassMuted
                        font.pixelSize: 13
                        rightPadding: 4
                        text: "Opens after"

                        HoverHandler {
                            id: delayHover
                        }
                    }

                    VButton {
                        ToolTip.delay: 500
                        ToolTip.text: "Wait less"
                        ToolTip.visible: hovered
                        enabled: view.cardMod.launchDelay > 5
                        icon: "minus"
                        small: true

                        onClicked: view.cardMod.launchDelay = Math.max(5, view.cardMod.launchDelay - 5)
                    }

                    VText {
                        anchors.verticalCenter: parent.verticalCenter
                        font.features: ({
                                            "tnum": 1
                                        })
                        font.pixelSize: 14
                        font.weight: Font.ExtraBold
                        horizontalAlignment: Text.AlignHCenter
                        text: view.cardMod.launchDelay + " s"
                        width: 40
                    }

                    VButton {
                        ToolTip.delay: 500
                        ToolTip.text: "Wait longer"
                        ToolTip.visible: hovered
                        enabled: view.cardMod.launchDelay < 300
                        icon: "plus"
                        small: true

                        onClicked: view.cardMod.launchDelay = Math.min(300, view.cardMod.launchDelay + 5)
                    }
                }

                VButton {
                    small: true
                    text: "Open " + view.cardMod.name + " now"
                    visible: view.group === "ready" && view.cardMod.type === Mod.Launchable && !view.launching

                    onClicked: view.cardMod.launchMod(view.game)
                }
            }
        }
    }

    // ------------------------------------------------------------ the way back, which never scrolls away
    BackBar {
        led: Theme.led(view.group)
        opacity: 1 - lensView.shown
        raised: pageFlick.contentY > titleBlock.y - 8
        title: view.game ? view.game.name : ""
        visible: view.game !== null
        width: view.width

        onBack: Nav.back()
    }

    LensView {
        id: lensView

        anchors.fill: parent
        game: view.game
        shown: view.launching ? 1 : 0
    }
}
