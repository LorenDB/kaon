import QtCore
import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// The window is drawn as the headset: a shell around a black visor, with the main button in the icon's nose notch.
// Navigation and search sit on the top strip; status and secondary actions on the bottom one.
ApplicationWindow {
    id: root

    // A game with a mod installed into it starts in VR however it is launched, so there is no flat launch to offer.
    // Without a mod there is only one way to start the game, and the notch button is it.
    readonly property bool canPlayFlat: inGame && game.canLaunch && (group === "ready" || group === "setup") &&
                                        !staticVrInstalled

    readonly property bool downloading: DownloadManager.downloading && !DownloadManager.background
    readonly property bool editingText: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    readonly property Game game: Nav.game
    property int gameCount: 0
    readonly property string group: inGame ? GameStatus.group(game, GameStatus.revision) : ""
    readonly property bool inGame: Nav.view === "game" && game !== null
    property bool keepAdd: false
    property bool keepGame: false
    property bool keepMods: false
    property bool keepSettings: false
    readonly property bool launching: inGame && Launcher.game === game && Launcher.phase !== Launcher.Idle
    // Too little room on the bottom strip for the game's buttons; they move into its menu
    readonly property bool narrow: width < 820
    readonly property var openStep: inGame ? GameStatus.steps(game, GameStatus.revision).find(s => s.state !== "ok"
                                                                                                   && s.state !== "warn") :
                                             undefined
    // Menus and dialogs take Escape and the scrolling keys for themselves while they are open
    readonly property bool popupOpen: Nav.openPopups > 0
    // An Installable VR mod is copied into the game, so launching the game still runs it. A Launchable mod injects
    // afterwards, and the game itself can still be started flat.
    readonly property bool staticVrInstalled: inGame && flatLaunchBlocked(game, GameStatus.revision)
    readonly property string statusText: {
        if (downloading) {
            const progress = DownloadManager.progress;
            return "Downloading " + DownloadManager.currentDownloadName + (progress >= 0 ? " · " + Math.round(progress
                                                                                                              * 100) + "%" :
                                                                                           "");
        }
        if (Nav.notice !== "")
            return Nav.notice;
        if (Launcher.phase === Launcher.Countdown && Launcher.game !== root.game)
            return "Opening " + Launcher.mod.name + " for " + Launcher.game.name + " in " + Launcher.remaining + " s";
        if (GamesFilterModel.scanning)
            return "Scanning libraries";
        if (Nav.view !== "library")
            return "";
        return root.gameCount === 1 ? "1 game" : root.gameCount + " games";
    }

    function flatLaunchBlocked(game, revision) {
        const mods = GameStatus.vrMods(game, revision);
        for (let i = 0; i < mods.length; ++i)
            if (mods[i].type === Mod.Installable && GameStatus.isInstalled(mods[i], game, revision))
                return true;
        return false;
    }

    function playFlat() {
        game.launch();
        Nav.notify("Starting " + game.name + " without VR");
    }

    function removeGame() {
        const g = game;
        Nav.confirm("Remove " + g.name + " from Kaon?", "The game's files stay where they are.", "Remove", () => {
            Nav.goLibrary();
            CustomGames.deleteGame(g);
        }, true);
    }

    function rescan() {
        GameStatus.rescanLibraries();
        Nav.notify("Scanning your libraries");
    }

    function retainPage() {
        if (Nav.view === "game")
            keepGame = true;
        else if (Nav.view === "mods")
            keepMods = true;
        else if (Nav.view === "settings")
            keepSettings = true;
        else if (Nav.view === "addGame")
            keepAdd = true;
    }

    function startSteamVr() {
        Steam.launchSteamVR();
        Nav.notify("Starting SteamVR");
    }

    color: Theme.shell
    height: 720
    minimumHeight: 480
    minimumWidth: 640
    title: "Kaon"
    visible: true
    width: 950

    Component.onCompleted: retainPage()
    // Tab can land on something that is scrolled out of sight
    onActiveFocusItemChanged: if (activeFocusItem && activeFocusItem.activeFocusOnTab)
                                  pads.ensureVisible(activeFocusItem)

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

    Connections {
        function onViewChanged() {
            root.retainPage();
        }

        target: Nav
    }

    // ------------------------------------------------------------ the visor and what it shows
    Rectangle {
        id: visor

        color: Theme.glass
        height: root.height - Theme.topStrap - Theme.bottomStrap
        width: root.width - 2 * Theme.side
        x: Theme.side
        y: Theme.topStrap

        // The back button on a mouse. It sits under the pages, so it only gets what nothing on a page takes.
        MouseArea {
            acceptedButtons: Qt.BackButton
            anchors.fill: parent

            onClicked: Nav.back()
        }

        // Pages stay loaded after the first visit. Destroying them threw away scroll position, open menus,
        // and anything the page was in the middle of showing.
        Item {
            id: pages

            anchors.fill: parent

            Loader {
                id: libraryPage

                anchors.fill: parent
                focus: visible
                sourceComponent: libraryC
                visible: Nav.view === "library"
            }

            Loader {
                id: gamePage

                active: Nav.view === "game" || root.keepGame
                anchors.fill: parent
                focus: visible
                sourceComponent: gameC
                visible: root.inGame
            }

            Loader {
                id: modsPage

                active: Nav.view === "mods" || root.keepMods
                anchors.fill: parent
                focus: visible
                sourceComponent: modsC
                visible: Nav.view === "mods"
            }

            Loader {
                id: settingsPage

                active: Nav.view === "settings" || root.keepSettings
                anchors.fill: parent
                focus: visible
                sourceComponent: settingsC
                visible: Nav.view === "settings"
            }

            Loader {
                id: addPage

                active: Nav.view === "addGame" || root.keepAdd
                anchors.fill: parent
                focus: visible
                sourceComponent: addGameC
                visible: Nav.view === "addGame"
            }

            Loader {
                id: modConfigPage

                // Created only while it's open. Left loaded, it sat under the game page and picked up that page's scroll.
                active: Nav.view === "modConfig"
                anchors.fill: parent
                focus: visible
                sourceComponent: modConfigC
                visible: Nav.view === "modConfig"
            }
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

    Component {
        id: modConfigC

        ModConfigView {
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

            // The app logo
            Image {
                anchors.verticalCenter: parent.verticalCenter
                fillMode: Image.PreserveAspectFit
                height: 32
                smooth: true
                source: "icons/kaon.svg"
                sourceSize.height: 64
                sourceSize.width: 64
                width: 32
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

                    // A page opened from a tab keeps that tab lit: a game is still Games
                    readonly property bool active: modelData.id === "library" ? (Nav.view === "library" || Nav.view
                                                                                 === "game" || Nav.view === "modConfig" || (
                                                                                     Nav.view === "addGame"
                                                                                     && Nav.addGameFrom === "library")) :
                                                                                Nav.view === modelData.id || (modelData.id
                                                                                                              === "settings"
                                                                                                              && Nav.view
                                                                                                              === "addGame"
                                                                                                              && Nav.addGameFrom
                                                                                                              === "settings")
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

                function applySearch() {
                    if (text !== GamesFilterModel.search)
                        text = GamesFilterModel.search;
                }

                function clearSearch() {
                    GamesFilterModel.search = "";
                    applySearch();
                }

                activeFocusOnTab: true
                anchors.left: parent.left
                anchors.leftMargin: 40
                anchors.right: clearButton.visible ? clearButton.left : parent.right
                anchors.rightMargin: clearButton.visible ? 4 : 14
                anchors.verticalCenter: parent.verticalCenter
                clip: true
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: 13
                font.weight: Font.Medium
                selectByMouse: true
                selectedTextColor: Theme.shell
                selectionColor: Theme.ink

                Component.onCompleted: applySearch()
                Keys.onEscapePressed: {
                    clearSearch();
                    focus = false;
                }
                // textEdited is only user input. Hiding this field must not write an empty string back over the search.
                onTextEdited: GamesFilterModel.search = text

                Connections {
                    function onSearchChanged() {
                        search.applySearch();
                    }

                    target: GamesFilterModel
                }

                VText {
                    color: Theme.inkMuted
                    font.pixelSize: 13
                    text: "Search games"
                    visible: search.text === ""
                }
            }

            // The search is kept between runs, so a way to drop it has to be in plain sight
            Item {
                id: clearButton

                activeFocusOnTab: visible
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                height: 24
                visible: search.text !== ""
                width: 24

                Keys.onReturnPressed: search.clearSearch()
                Keys.onSpacePressed: search.clearSearch()

                Rectangle {
                    anchors.fill: parent
                    border.color: clearButton.activeFocus ? Theme.ledBlue : "transparent"
                    border.width: 2
                    color: clearMouse.containsMouse ? Theme.shellLine : "transparent"
                    radius: 12
                }

                Icon {
                    anchors.centerIn: parent
                    color: Theme.inkMuted
                    name: "x"
                    size: 14
                    stroke: 2.2
                }

                MouseArea {
                    id: clearMouse

                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true

                    onClicked: search.clearSearch()
                }
            }
        }
    }

    Shortcut {
        enabled: !Nav.capturingKeys
        sequences: [StandardKey.Find]

        onActivated: {
            Nav.view = "library";
            search.forceActiveFocus();
        }
    }

    Shortcut {
        enabled: Nav.canGoBack && !root.popupOpen && !Nav.capturingKeys
        sequences: ["Esc", StandardKey.Back]

        onActivated: Nav.back()
    }

    // The arrow keys move the focus ring the way a gamepad's stick does. A text field keeps them for its cursor.
    Shortcut {
        enabled: !root.editingText && !Nav.capturingKeys
        sequence: "Up"

        onActivated: pads.move(0, -1)
    }

    Shortcut {
        enabled: !root.editingText && !Nav.capturingKeys
        sequence: "Down"

        onActivated: pads.move(0, 1)
    }

    Shortcut {
        enabled: !root.editingText && !Nav.capturingKeys
        sequence: "Left"

        onActivated: pads.move(-1, 0)
    }

    Shortcut {
        enabled: !root.editingText && !Nav.capturingKeys
        sequence: "Right"

        onActivated: pads.move(1, 0)
    }

    // The keys that scroll a page everywhere else. A text field keeps them for moving its cursor.
    Shortcut {
        enabled: !root.editingText && !root.popupOpen && !Nav.capturingKeys
        sequence: "PgDown"

        onActivated: pads.scrollPage(1)
    }

    Shortcut {
        enabled: !root.editingText && !root.popupOpen && !Nav.capturingKeys
        sequence: "PgUp"

        onActivated: pads.scrollPage(-1)
    }

    Shortcut {
        enabled: !root.editingText && !root.popupOpen && !Nav.capturingKeys
        sequence: "Home"

        onActivated: pads.scrollToEnd(-1)
    }

    Shortcut {
        enabled: !root.editingText && !root.popupOpen && !Nav.capturingKeys
        sequence: "End"

        onActivated: pads.scrollToEnd(1)
    }

    // ------------------------------------------------------------ bottom strip
    Item {
        anchors.bottom: parent.bottom
        height: Theme.bottomStrap
        width: parent.width

        // Left: whatever Kaon is busy with, or the size of the library
        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.side + 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            visible: root.statusText !== ""

            Led {
                anchors.verticalCenter: parent.verticalCenter
                blinking: root.downloading || (Launcher.phase === Launcher.Countdown && Launcher.game !== root.game)
                color: root.downloading || Nav.notice !== "" || Launcher.phase !== Launcher.Idle ? Theme.ledGreen :
                                                                                                   Theme.ledOff

                size: 8
            }

            VText {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.inkMuted
                elide: Text.ElideRight
                font.pixelSize: 13
                text: root.statusText
                width: Math.min(implicitWidth, root.width / 2 - Theme.button - 40)
            }
        }

        // Right: secondary actions for the page
        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.side + 6
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

            VButton {
                icon: "headset"
                shellStyle: true
                small: true
                text: "Start SteamVR"
                visible: root.inGame && Steam.hasSteamVR && !root.narrow

                onClicked: root.startSteamVr()
            }

            VButton {
                shellStyle: true
                small: true
                text: "Play without VR"
                visible: root.canPlayFlat && !root.narrow

                onClicked: root.playFlat()
            }

            VButton {
                id: more

                ToolTip.delay: 500
                ToolTip.text: "More for this game"
                ToolTip.visible: hovered && !gameMenu.opened
                icon: "more"
                shellStyle: true
                small: true
                visible: root.inGame

                onClicked: gameMenu.opened ? gameMenu.close() : gameMenu.open()

                GlassMenu {
                    id: gameMenu

                    actions: [root.narrow && Steam.hasSteamVR ? {
                                                                    "icon": "headset",
                                                                    "text": "Start SteamVR",
                                                                    "run": root.startSteamVr
                                                                } : null, root.narrow && root.canPlayFlat ? {
                                                                                                                "icon": "play",
                                                                                                                "text": "Play without VR",
                                                                                                                "run": root.playFlat
                                                                                                            } : null,
                        {
                            "icon": "folder",
                            "text": "Open the game's folder",
                            "run": () => GameStatus.openFolder(root.game)
                        },
                        root.game !== null && root.game.canOpenSettings ? {
                                                                              "icon": "external",
                                                                              "text": "Steam properties",
                                                                              "run": () => GameStatus.openSteamProperties(
                                                                                               root.game)
                                                                          } : null, root.game !== null && root.game.store
                        === Game.Custom ? {
                                              "icon": "trash",
                                              "text": "Remove from Kaon",
                                              "run": root.removeGame
                                          } : null]
                    x: more.width - width
                    y: -height - 10
                }
            }

            VButton {
                icon: "refresh"
                shellStyle: true
                small: true
                text: GamesFilterModel.scanning ? "Scanning" : "Rescan"
                visible: Nav.view === "library"

                onClicked: root.rescan()
            }

            VButton {
                icon: "plus"
                shellStyle: true
                small: true
                text: "Add a game"
                visible: Nav.view === "library"

                onClicked: Nav.addGame()
            }

            VButton {
                shellStyle: true
                small: true
                text: "Cancel"
                visible: Nav.view === "addGame"

                onClicked: Nav.back()
            }
        }
    }

    // ------------------------------------------------------------ the button in the notch
    NotchButton {
        id: notch

        readonly property bool busy: root.openStep !== undefined && root.openStep.state === "busy"
        readonly property bool canSetUp: root.inGame && GameStatus.canSetUp(root.game, GameStatus.revision)

        enabled: {
            if (Nav.view === "addGame")
                return (addPage.item as AddGameView)?.valid ?? false;
            if (!root.inGame || root.launching)
                return true;
            if (root.group === "setup")
                return canSetUp || (root.openStep !== undefined && root.openStep.state !== "busy" && !!root.openStep.action);
            if (root.group === "none")
                return root.game.canLaunch;
            return true;
        }
        icon: {
            if (Nav.view === "addGame")
                return "plus";
            if (!root.inGame)
                return Steam.hasSteamVR ? "headset" : "refresh";
            if (root.group !== "setup")
                return "play";
            // The button does whatever the first open step needs, and shows which kind of thing that is
            const action = canSetUp || root.openStep === undefined ? "" : root.openStep.action;
            return action === "launchOnce" ? "play" : action === "steamSettings" ? "external" : action === "refresh"
                                                                                   ? "refresh" : "download";
        }
        label: {
            if (Nav.view === "addGame")
                return "Add game";
            if (!root.inGame)
                return Steam.hasSteamVR ? "Start SteamVR" : GamesFilterModel.scanning ? "Scanning" : "Rescan";
            if (root.launching)
                return Launcher.phase === Launcher.Countdown ? "Open now" : "Done";
            if (root.group === "setup")
                return busy ? "Working" : canSetUp || root.openStep === undefined || !root.openStep.action ? "Set up" :
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
                (addPage.item as AddGameView)?.submit();
            else if (!root.inGame) {
                if (Steam.hasSteamVR)
                    root.startSteamVr();
                else
                    root.rescan();
            } else if (root.launching)
                Launcher.phase === Launcher.Countdown ? Launcher.openModNow() : Launcher.stop();
            else if (root.group === "setup") {
                if (canSetUp)
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

    GamepadNav {
        id: pads

        notch: notch
        searchField: search
    }
}
