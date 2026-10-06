pragma Singleton

import QtQuick

import dev.lorendb.kaon

// Which page the visor shows, the open game, and short notices for the status strip.
QtObject {
    id: nav

    // Where "Add a game" was opened from, which is where leaving it goes back to
    property string addGameFrom: "library"
    // Pages that are a step into another page, and so have a way back
    readonly property bool canGoBack: view === "game" || view === "addGame"
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
            if (nav.view === "game")
                nav.view = "library";
        }

        target: GamesFilterModel
    }
    property string view: "library" // library | game | mods | settings | addGame

    signal confirmRequested(string title, string text, string actionLabel, var onConfirm)

    function addGame() {
        addGameFrom = view === "settings" ? "settings" : "library";
        view = "addGame";
    }

    function back() {
        if (view === "addGame")
            view = addGameFrom;
        else if (view === "game")
            view = "library";
    }

    function confirm(title, text, actionLabel, onConfirm) {
        confirmRequested(title, text, actionLabel, onConfirm);
    }

    function goLibrary() {
        view = "library";
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

    // Every menu and dialog reports its visible property here as it changes
    function popupShown(shown) {
        openPopups = Math.max(0, openPopups + (shown ? 1 : -1));
    }
}
