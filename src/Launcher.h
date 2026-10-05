#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QTimer>

#include "Game.h"
#include "Mod.h"

// Runs "Play in VR": starts the game, counts down, then opens the mod. It lives outside any page, so the countdown keeps
// going while you look at other games.
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

public:
    enum Phase
    {
        Idle,
        // The game was started and the mod opens when the countdown ends
        Countdown,
        // A launchable mod (UEVR) was opened; the user injects from its window
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

private:
    explicit Launcher(QObject *parent = nullptr);

    void setPhase(Phase phase);

    QPointer<Game> m_game;
    QPointer<Mod> m_mod;
    Phase m_phase{Idle};
    int m_remaining{0};
    int m_total{0};
    QTimer m_tick;
    QTimer m_dismiss;
};
