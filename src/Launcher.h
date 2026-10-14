#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QTimer>

#include "Game.h"
#include "Mod.h"

// Runs "Play in VR": starts the game, waits for its binary, then launches the mod. With auto-attach the mod
// injects by itself right away (--attach); without it the mod opens after a countdown for manual injection.
// It lives outside any page, so the wait keeps going while you look at other games.
class Launcher : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Game *game READ game NOTIFY stateChanged FINAL)
    Q_PROPERTY(Mod *mod READ mod NOTIFY stateChanged FINAL)
    Q_PROPERTY(Phase phase READ phase NOTIFY stateChanged FINAL)
    Q_PROPERTY(int remaining READ remaining NOTIFY remainingChanged FINAL)
    Q_PROPERTY(int total READ total NOTIFY stateChanged FINAL)
    // Whether Play in VR injects the mod by itself once the game is running. When off, the mod opens after
    // the game's delay and injection stays manual. Covers every UEVR; on unless changed.
    Q_PROPERTY(bool autoAttach READ autoAttach WRITE setAutoAttach NOTIFY autoAttachChanged FINAL)

public:
    enum Phase
    {
        Idle,
        // The game was started and Kaon is waiting for its binary to show up
        WaitingForGame,
        // The game's binary is running; the mod opens for manual injection when the countdown ends
        Countdown,
        // A launchable mod (UEVR) was opened with --attach; it injects by itself
        ModOpen,
        // The game was started with an installed mod, or it has its own VR mode
        GameStarting,
    };
    Q_ENUM(Phase)

    static Launcher *instance();
    static Launcher *create(QQmlEngine *, QJSEngine *) { return instance(); }

    Game *game() const { return m_game; }
    Mod *mod() const { return m_mod; }
    Phase phase() const { return m_phase; }
    int remaining() const { return m_remaining; }
    int total() const { return m_total; }
    bool autoAttach() const;
    void setAutoAttach(bool attach);

    Q_INVOKABLE void play(Game *game);
    Q_INVOKABLE void openModNow();
    Q_INVOKABLE void stop();

    // Point an in-progress launch at the replacement object. True means the pointer changed and announceGame()
    // still has to run, after QML has caught up. A removed game stops the launch here.
    bool retargetGame(const QList<Game *> &previous, const QList<Game *> &current);
    void announceGame();

signals:
    void stateChanged();
    void remainingChanged();
    void autoAttachChanged();

private:
    explicit Launcher(QObject *parent = nullptr);

    void setPhase(Phase phase);
    void startCountdown();
    // The game's binary is up: inject now with auto-attach, or count down to a manual open
    void onGameReady();

    QPointer<Game> m_game;
    QPointer<Mod> m_mod;
    Phase m_phase{Idle};
    int m_remaining{0};
    int m_total{0};
    QTimer m_tick;
    QTimer m_poll;
    int m_pollAttempts{0};
    QTimer m_dismiss;
};
