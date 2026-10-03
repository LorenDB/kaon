import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes

import dev.lorendb.kaon

// The window is drawn as the headset: a shell around a black visor, with the main button in the icon's nose notch.
// Navigation and search sit on the top strip; status and secondary actions on the bottom one.
ApplicationWindow {
    id: root

    readonly property bool compact: width < 1180
    readonly property Game game: Nav.game
    property int gameCount: 0
    readonly property Mod gameMod: inGame ? (GameStatus.revision, GameStatus.preferredMod(game)) : null
    readonly property string group: inGame ? (GameStatus.revision, GameStatus.group(game)) : ""
    readonly property bool inGame: Nav.view === "game" && game !== null
    readonly property bool launching: inGame && Launcher.game === game && Launcher.phase !== Launcher.Idle
    readonly property var openStep: inGame ? (GameStatus.revision, GameStatus.steps(game).find(s => s.state !== "ok" && s.state
                                                                                                    !== "warn")) : undefined
    // An Installable VR mod is copied into the game, so launching the game still runs it. A Launchable mod injects
    // afterwards, and the game itself can still be started flat.
    readonly property bool staticVrInstalled: inGame && (GameStatus.revision, flatLaunchBlocked(game))

    // vrMods() does not notify, so callers pass GameStatus.revision through the comma expression above.
    function flatLaunchBlocked(game) {
        if (!game)
            return false;
        const mods = GameStatus.vrMods(game);
        for (let i = 0; i < mods.length; ++i)
            if (mods[i].type === Mod.Installable && mods[i].isInstalledForGame(game))
                return true;
        return false;
    }

    color: Theme.shell
    height: 720
    minimumHeight: 480
    minimumWidth: 640
    title: "Kaon"
    visible: true
    width: 950

    Settings {
        property alias windowHeight: root.height
        property alias windowWidth: root.width
    }

    Connections {
        function onGamesChanged() {
            root.gameCount = GamesFilterModel.games().length;
        }

        target: GamesFilterModel
    }

    // ------------------------------------------------------------ the visor and what it shows
    Rectangle {
        id: visor

        color: Theme.glass
        height: root.height - Theme.topStrap - Theme.bottomStrap
        width: root.width - 2 * Theme.side
        x: Theme.side
        y: Theme.topStrap

        Loader {
            id: page

            anchors.fill: parent
            focus: true
            sourceComponent: root.inGame ? gameC : Nav.view === "mods" ? modsC : Nav.view === "settings" ? settingsC :
                                                                                                           Nav.view
                                                                                                           === "addGame"
                                                                                                           ? addGameC :
                                                                                                             libraryC
        }
    }

    Component {
        id: libraryC

        LibraryView {
        }
    }

    Component {
        id: gameC

        GameView {
        }
    }

    Component {
        id: modsC

        ModsView {
        }
    }

    Component {
        id: settingsC

        SettingsView {
        }
    }

    Component {
        id: addGameC

        AddGameView {
        }
    }

    VisorFrame {
        anchors.fill: parent
    }

    // ------------------------------------------------------------ top strip
    Item {
        height: Theme.topStrap
        width: parent.width

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.side + 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 9

            // Kaon's icon, cut to this frame: a visor with its notch and two lenses
            Shape {
                anchors.verticalCenter: parent.verticalCenter
                height: 20
                preferredRendererType: Shape.CurveRenderer
                width: 36

                ShapePath {
                    fillColor: Theme.ink
                    scale: Qt.size(0.45, 0.45)
                    strokeColor: "transparent"

                    PathSvg {
                        path: "M 12 0 H 68 A 12 12 0 0 1 80 12 V 32 A 12 12 0 0 1 68 44 H 47 C 44 44 44.5 33 40 33 C 35.5 33 36 44 33 44 H 12 A 12 12 0 0 1 0 32 V 12 A 12 12 0 0 1 12 0 Z"
                    }
                }

                ShapePath {
                    fillColor: Theme.shell
                    strokeColor: "transparent"

                    PathAngleArc {
                        centerX: 9.9
                        centerY: 9.5
                        radiusX: 4
                        radiusY: 4
                        sweepAngle: 360
                    }

                    PathAngleArc {
                        centerX: 26.1
                        centerY: 9.5
                        radiusX: 4
                        radiusY: 4
                        sweepAngle: 360
                    }
                }
            }

            VText {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.ink
                font.letterSpacing: -0.3
                font.pixelSize: 20
                font.weight: Font.ExtraBold
                text: "Kaon"
            }

            Item {
                height: 1
                width: 14
            }

            Repeater {
                model: [
                    {
                        "id": "library",
                        "label": "Games"
                    },
                    {
                        "id": "mods",
                        "label": "Mods"
                    },
                    {
                        "id": "settings",
                        "label": "Settings"
                    }
                ]

                Item {
                    id: tab

                    readonly property bool active: modelData.id === "library" ? (Nav.view === "library" || Nav.view
                                                                                 === "game") : Nav.view === modelData.id || (
                                                                                    modelData.id === "settings" && Nav.view
                                                                                    === "addGame")
                    required property var modelData

                    activeFocusOnTab: true
                    anchors.verticalCenter: parent.verticalCenter
                    height: 32
                    width: tabText.implicitWidth + 28

                    Keys.onReturnPressed: Nav.view = modelData.id
                    Keys.onSpacePressed: Nav.view = modelData.id

                    Rectangle {
                        anchors.fill: parent
                        color: tab.active ? Theme.ink : tabMouse.containsMouse ? Theme.shellDeep : "transparent"
                        radius: height / 2
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4
                        border.color: Theme.ledBlue
                        border.width: 2
                        color: "transparent"
                        radius: height / 2
                        visible: tab.activeFocus
                    }

                    VText {
                        id: tabText

                        anchors.centerIn: parent
                        color: tab.active ? Theme.shell : Theme.ink
                        font.pixelSize: 13
                        font.weight: Font.Bold
                        text: tab.modelData.label
                    }

                    MouseArea {
                        id: tabMouse

                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true

                        onClicked: Nav.view = tab.modelData.id
                    }
                }
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: Theme.side + 10
            anchors.verticalCenter: parent.verticalCenter
            border.color: search.activeFocus ? Theme.ink : "transparent"
            border.width: 1.5
            color: Theme.shellDeep
            height: 34
            radius: 17
            visible: Nav.view === "library"
            width: Math.min(300, root.width * 0.28)

            Icon {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.inkMuted
                name: "search"
                size: 17
                stroke: 2
            }

            TextInput {
                id: search

                anchors.left: parent.left
                anchors.leftMargin: 40
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                clip: true
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: 13
                font.weight: Font.Medium
                selectByMouse: true
                selectedTextColor: Theme.shell
                selectionColor: Theme.ink
                text: GamesFilterModel.search

                Keys.onEscapePressed: {
                    text = "";
                    focus = false;
                }
                onTextChanged: GamesFilterModel.search = text

                VText {
                    color: Theme.inkMuted
                    font.pixelSize: 13
                    text: "Search games"
                    visible: search.text === ""
                }
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Find]

        onActivated: {
            Nav.view = "library";
            search.forceActiveFocus();
        }
    }

    Shortcut {
        enabled: Nav.view === "game" || Nav.view === "addGame"
        sequence: "Esc"

        onActivated: Nav.goLibrary()
    }

    // ------------------------------------------------------------ bottom strip
    Item {
        anchors.bottom: parent.bottom
        height: Theme.bottomStrap
        width: parent.width

        // Left: the injection delay for this game's mod, or whatever Kaon is busy with
        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.side + 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            visible: root.gameMod !== null && root.gameMod.type === Mod.Launchable && root.game.canLaunch && (root.group
                                                                                                              === "ready"
                                                                                                              || root.group
                                                                                                              === "setup")
                     && !DownloadManager.downloading && Nav.notice === ""

            VText {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.inkMuted
                font.pixelSize: 13
                text: root.compact ? "Wait" : "Open " + (root.gameMod ? root.gameMod.name : "") + " after"
            }

            VButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: "minus"
                shellStyle: true
                small: true

                onClicked: root.gameMod.launchDelay = Math.max(5, root.gameMod.launchDelay - 5)
            }

            VText {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.ink
                font.features: ({
                                    "tnum": 1
                                })
                font.pixelSize: 15
                font.weight: Font.ExtraBold
                horizontalAlignment: Text.AlignHCenter
                text: (root.gameMod ? root.gameMod.launchDelay : 30) + " s"
                width: 44
            }

            VButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: "plus"
                shellStyle: true
                small: true

                onClicked: root.gameMod.launchDelay = Math.min(300, root.gameMod.launchDelay + 5)
            }
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.side + 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            visible: !(root.gameMod !== null && root.gameMod.type === Mod.Launchable && root.game.canLaunch && (root.group
                                                                                                                === "ready"
                                                                                                                || root.group
                                                                                                                === "setup"))
                     || DownloadManager.downloading || Nav.notice !== ""

            Led {
                anchors.verticalCenter: parent.verticalCenter
                blinking: DownloadManager.downloading || (Launcher.phase === Launcher.Countdown && Launcher.game
                                                          !== root.game)

                color: DownloadManager.downloading || Nav.notice !== "" || Launcher.phase !== Launcher.Idle ? Theme.ledGreen :
                                                                                                              Theme.ledOff
                size: 8
            }

            VText {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.inkMuted
                elide: Text.ElideRight
                font.pixelSize: 13
                text: {
                    if (DownloadManager.downloading)
                        return "Downloading " + DownloadManager.currentDownloadName;
                    if (Nav.notice !== "")
                        return Nav.notice;
                    if (Launcher.phase === Launcher.Countdown && Launcher.game !== root.game)
                        return "Opening " + Launcher.mod.name + " for " + Launcher.game.name + " in " + Launcher.remaining
                                + " s";
                    if (GamesFilterModel.scanning)
                        return "Scanning libraries";

                    return root.gameCount === 1 ? "1 game" : root.gameCount + " games";
                }
                width: Math.min(implicitWidth, root.width / 2 - Theme.button - 40)
            }
        }

        // Right: secondary actions for the page
        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.side + 6
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            VButton {
                ToolTip.delay: 500
                ToolTip.text: "Play without VR"
                ToolTip.visible: root.compact && hovered
                icon: root.compact ? "play" : ""
                shellStyle: true
                small: true
                text: root.compact ? "" : "Play without VR"
                visible: root.inGame && root.game.canLaunch && root.group !== "none" && !root.staticVrInstalled

                onClicked: {
                    root.game.launch();
                    Nav.notify("Starting " + root.game.name + " without VR");
                }
            }

            VButton {
                ToolTip.delay: 500
                ToolTip.text: "Open the game's folder"
                ToolTip.visible: hovered
                icon: "folder"
                shellStyle: true
                small: true
                text: root.compact ? "" : "Folder"
                visible: root.inGame

                onClicked: Qt.openUrlExternally("file://" + root.game.installDir)
            }

            VButton {
                ToolTip.delay: 500
                ToolTip.text: "Open the game's properties in Steam"
                ToolTip.visible: root.compact && hovered
                icon: "external"
                shellStyle: true
                small: true
                text: root.compact ? "" : "Steam properties"
                visible: root.inGame && root.game.canOpenSettings

                onClicked: Qt.openUrlExternally("steam://gameproperties/" + root.game.id)
            }

            VButton {
                ToolTip.delay: 500
                ToolTip.text: "Remove from Kaon"
                ToolTip.visible: root.compact && hovered
                icon: "trash"
                shellStyle: true
                small: true
                text: root.compact ? "" : "Remove"
                visible: root.inGame && root.game.store === Game.Custom

                onClicked: {
                    const g = root.game;
                    Nav.confirm("Remove " + g.name + " from Kaon?", "The game's files stay where they are.", "Remove", ()
                                => {

                                    Nav.goLibrary();
                                    CustomGames.deleteGame(g);
                                });
                }
            }

            VButton {
                ToolTip.delay: 500
                ToolTip.text: "Start SteamVR"
                ToolTip.visible: root.compact && hovered
                icon: "headset"
                shellStyle: true
                small: true
                text: root.compact ? "" : "Start SteamVR"
                visible: root.inGame && Steam.hasSteamVR

                onClicked: {
                    Steam.launchSteamVR();
                    Nav.notify("Starting SteamVR");
                }
            }

            VButton {
                icon: "refresh"
                shellStyle: true
                small: true
                text: GamesFilterModel.scanning ? "Scanning" : "Rescan"
                visible: Nav.view === "library"

                onClicked: {
                    GameStatus.rescanLibraries();
                    Nav.notify("Scanning your libraries");
                }
            }

            VButton {
                icon: "plus"
                shellStyle: true
                small: true
                text: "Add a game"
                visible: Nav.view === "library"

                onClicked: Nav.view = "addGame"
            }

            VButton {
                shellStyle: true
                small: true
                text: "Cancel"
                visible: Nav.view === "addGame"

                onClicked: Nav.goLibrary()
            }
        }
    }

    // ------------------------------------------------------------ the button in the notch
    NotchButton {
        readonly property bool busy: root.openStep !== undefined && root.openStep.state === "busy"

        enabled: {
            if (Nav.view === "addGame")
                return (page.item as AddGameView)?.valid ?? false;
            if (!root.inGame || root.launching)
                return true;
            if (root.group === "setup")
                return GameStatus.canSetUp(root.game) || (root.openStep !== undefined && root.openStep.state === "todo" && !
                                                          !root.openStep.action);
            if (root.group === "none")
                return root.game.canLaunch;
            return true;
        }
        icon: {
            if (Nav.view === "addGame")
                return "plus";
            if (!root.inGame)
                return Steam.hasSteamVR ? "headset" : "refresh";
            return root.group === "setup" ? "download" : "play";
        }
        label: {
            if (Nav.view === "addGame")
                return "Add game";
            if (!root.inGame)
                return Steam.hasSteamVR ? "Start SteamVR" : GamesFilterModel.scanning ? "Scanning" : "Rescan";
            if (root.launching)
                return Launcher.phase === Launcher.Countdown ? "Open now" : "Done";
            if (root.group === "setup")
                return busy ? "Working" : GameStatus.canSetUp(root.game) || root.openStep === undefined ? "Set up" :
                                                                                                          root.openStep.actionLabel;
            return root.group === "none" ? "Play" : "Play in VR";
        }
        led: !root.inGame ? Theme.ledOff : root.launching ? Theme.ledGreen : Theme.led(root.group)
        opacity: enabled ? 1 : 0.5
        phase: root.launching ? (Launcher.phase === Launcher.Countdown ? "waiting" : "running") : busy ? "starting" : ""
        progress: root.launching && Launcher.phase === Launcher.Countdown && Launcher.total > 0 ? Launcher.remaining
                                                                                                  / Launcher.total : 1
        seconds: Launcher.remaining
        x: (root.width - width) / 2
        y: root.height - Theme.bottomStrap - height / 2

        onClicked: {
            if (Nav.view === "addGame")
                (page.item as AddGameView)?.submit();
            else if (!root.inGame) {
                if (Steam.hasSteamVR) {
                    Steam.launchSteamVR();
                    Nav.notify("Starting SteamVR");
                } else {
                    GameStatus.rescanLibraries();
                    Nav.notify("Scanning your libraries");
                }
            } else if (root.launching)
                Launcher.phase === Launcher.Countdown ? Launcher.openModNow() : Launcher.stop();
            else if (root.group === "setup") {
                if (GameStatus.canSetUp(root.game))
                    GameStatus.setUp(root.game);
                else if (root.openStep !== undefined)
                    GameStatus.runStep(root.game, root.openStep.key, false);
            } else if (root.group === "none") {
                root.game.launch();
                Nav.notify("Starting " + root.game.name);
            } else
                Launcher.play(root.game);
        }
    }

    Dialogs {
    }
}
