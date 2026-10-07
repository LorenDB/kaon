pragma Singleton

import QtQuick

import dev.lorendb.kaon

// Which page the visor shows, the open game, and short notices for the status strip.
QtObject {
    id: nav

    // Where "Add a game" was opened from, which is where leaving it goes back to
    property string addGameFrom: "library"
    // Pages that are a step into another page, and so have a way back
    readonly property bool canGoBack: view === "game" || view === "addGame" || view === "modConfig"
    // A hotkey field is recording a key combination, so shortcuts must leave the keys alone.
    property bool capturingKeys: false
    // Set while a navigation is allowed to drop unsaved mod settings, such as the game disappearing.
    property bool discardConfig: false
    property Game game: null

    // Rescans replace every Game object, so the open game is remembered by store and id as well
    property string gameId: ""
    property int gameStore: 0
    property string notice: ""
    readonly property Timer noticeTimer: Timer {
        interval: 4500

        onTriggered: nav.notice = ""
    }
    readonly property Connections notices: Connections {
        function onNoticed(text) {
            nav.notify(text);
        }

        target: GameStatus
    }
    // How many menus and dialogs are open. They take Escape and the scrolling keys for themselves.
    property int openPopups: 0
    // The page a dirty settings form was asked to leave for. Empty unless that question is open.
    property string pendingView: ""
    readonly property Connections rescans: Connections {
        function onGamesChanged() {
            if (nav.gameId === "")
                return;
            // Search the whole library. The filtered list hides a game the user still has open, and a rescan
            // has already swapped in the new object by the time this runs.
            const match = GamesFilterModel.gameByIdentity(nav.gameStore, nav.gameId);
            if (match) {
                if (nav.game !== match)
                    nav.game = match;
                return;
            }
            nav.game = null;
            if (nav.view === "game" || nav.view === "modConfig") {
                nav.discardConfig = true;
                nav.view = "library";
                nav.discardConfig = false;
            }
        }

        target: GamesFilterModel
    }
    // The last page that was actually settled on. Assigning view away from dirty settings bounces back.
    property string settledView: "library"
    property string view: "library" // library | game | modConfig | mods | settings | addGame

    signal confirmRequested(string title, string text, string actionLabel, var onConfirm, bool danger)

    function addGame() {
        addGameFrom = view === "settings" ? "settings" : "library";
        view = "addGame";
    }

    function back() {
        if (view === "modConfig")
            view = "game";
        else if (view === "addGame")
            view = addGameFrom;
        else if (view === "game")
            view = "library";
    }

    function confirm(title, text, actionLabel, onConfirm, danger) {
        confirmRequested(title, text, actionLabel, onConfirm, danger === true);
    }

    function goLibrary() {
        view = "library";
    }

    // Nothing is written until Save, so leaving a dirty form asks first. Cancel stays on the form.
    function guardConfigLeave() {
        if (pendingView !== "") {
            // The question is already open. Stay on the form until it is answered.
            if (view !== "modConfig") {
                discardConfig = true;
                view = "modConfig";
                discardConfig = false;
            }
            return;
        }
        if (discardConfig) {
            settledView = view;
            return;
        }
        if (settledView !== "modConfig" || view === "modConfig") {
            settledView = view;
            return;
        }
        const doc = ModConfigs.document;
        if (!doc || !doc.dirty) {
            settledView = view;
            return;
        }
        const dest = view;
        pendingView = dest;
        discardConfig = true;
        view = "modConfig";
        discardConfig = false;
        confirm("Leave these settings?", "Nothing is written until you save.", "Leave", () => {
            const next = pendingView;
            pendingView = "";
            if (settledView !== "modConfig" || next === "")
                return;
            discardConfig = true;
            view = next;
            discardConfig = false;
        });
    }

    function notify(text) {
        notice = text;
        noticeTimer.restart();
    }

    function openGame(g) {
        game = g;
        gameId = g.id;
        gameStore = g.store;
        view = "game";
    }

    function openModConfig(mod) {
        if (!game || !ModConfigs.open(mod, game)) {
            notify(ModConfigs.error !== "" ? ModConfigs.error : "Couldn't open those settings");
            return;
        }
        view = "modConfig";
    }

    // Every menu and dialog reports its visible property here as it changes
    function popupShown(shown) {
        openPopups = Math.max(0, openPopups + (shown ? 1 : -1));
    }

    onViewChanged: guardConfigLeave()
}
