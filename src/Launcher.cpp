#include "Launcher.h"

#include "GameStatus.h"

Launcher *Launcher::instance()
{
    static auto l = new Launcher;
    return l;
}

Launcher::Launcher(QObject *parent)
    : QObject{parent}
{
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (--m_remaining <= 0)
            openModNow();
        else
            emit remainingChanged();
    });

    // The final message stays up long enough to read, then the launch view closes by itself
    m_dismiss.setSingleShot(true);
    connect(&m_dismiss, &QTimer::timeout, this, &Launcher::stop);
}

void Launcher::play(Game *game)
{
    if (!game || m_phase == Countdown)
        return;

    const auto status = GameStatus::instance();
    const auto group = status->group(game);
    if (group != "ready"_L1 && group != "native"_L1)
        return;

    m_game = game;
    m_mod = group == "native"_L1 ? nullptr : status->preferredMod(game);
    m_dismiss.stop();

    // Installed mods and native VR only need the game itself
    if (!m_mod || m_mod->type() == Mod::Type::Installable)
    {
        game->launch();
        setPhase(GameStarting);
        m_dismiss.start(8000);
        return;
    }

    // Kaon can't start games from every store; then the player starts it and the mod opens right away
    if (!game->canLaunch())
    {
        openModNow();
        return;
    }

    game->launch();
    m_total = m_mod->launchDelay();
    m_remaining = m_total;
    emit remainingChanged();
    setPhase(Countdown);
    m_tick.start();
}

void Launcher::openModNow()
{
    m_tick.stop();
    if (!m_game || !m_mod)
    {
        stop();
        return;
    }

    m_remaining = 0;
    emit remainingChanged();
    m_mod->launchMod(m_game);
    setPhase(ModOpen);
    m_dismiss.start(20000);
}

bool Launcher::retargetGame(const QList<Game *> &previous, const QList<Game *> &current)
{
    if (!m_game || !previous.contains(m_game))
        return false;

    for (auto *game : current)
    {
        if (game->store() == m_game->store() && game->id() == m_game->id())
        {
            if (game == m_game)
                return false;
            m_game = game;
            return true;
        }
    }

    stop();
    return false;
}

void Launcher::announceGame()
{
    emit stateChanged();
}

void Launcher::stop()
{
    m_tick.stop();
    m_dismiss.stop();
    m_remaining = 0;
    emit remainingChanged();
    setPhase(Idle);
    m_game.clear();
    m_mod.clear();
    emit stateChanged();
}

void Launcher::setPhase(Phase phase)
{
    m_phase = phase;
    emit stateChanged();
}
