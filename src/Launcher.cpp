#include "Launcher.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QSettings>

#include "GameStatus.h"
#include "UevrPlugins.h"
#include "WinePrefix.h"

Q_LOGGING_CATEGORY(LauncherLog, "launcher")

namespace
{
    // How often to look for the game's binary, and how long before giving up and counting down anyway
    const auto pollIntervalMs = 500;
    const auto maxPollAttempts = 240; // 120 seconds

    // Whether one of the Windows binaries UEVR would inject into is running. Falls back to any Wine
    // process in the prefix when the game has no Windows executable Kaon can name.
    bool gameBinaryRunning(Game *game)
    {
        if (!game)
            return false;

        const auto stems = UevrPlugins::exeStems(game);
        if (stems.isEmpty())
            return WinePrefix::inUse(game);

        QList<QByteArray> needles;
        for (const auto &stem : stems)
            needles << (stem + ".exe"_L1).toLower().toUtf8();

        const auto entries = QDir{"/proc"_L1}.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &pid : entries)
        {
            bool numeric = false;
            pid.toLongLong(&numeric);
            if (!numeric)
                continue;

            QFile cmdline{"/proc/"_L1 + pid + "/cmdline"_L1};
            if (!cmdline.open(QIODevice::ReadOnly))
                continue;
            const auto raw = cmdline.readAll().toLower();
            if (raw.isEmpty())
                continue;

            for (const auto &needle : std::as_const(needles))
            {
                if (raw.contains(needle))
                    return true;
            }

            // Short process names are cut to 15 characters, so a truncated game binary still
            // starts with what comm holds. Short names like "wine" never start a game stem.
            QFile comm{"/proc/"_L1 + pid + "/comm"_L1};
            if (comm.open(QIODevice::ReadOnly))
            {
                const auto shortName = comm.readAll().trimmed().toLower();
                if (shortName.size() >= 4)
                {
                    for (const auto &stem : stems)
                    {
                        if (stem.toLower().toUtf8().startsWith(shortName))
                            return true;
                    }
                }
            }
        }
        return false;
    }
} // namespace

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

    m_poll.setInterval(pollIntervalMs);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        if (!m_game || m_phase != WaitingForGame)
        {
            m_poll.stop();
            return;
        }
        if (gameBinaryRunning(m_game))
        {
            qCInfo(LauncherLog) << "Saw" << m_game->name() << "running";
            onGameReady();
            return;
        }
        if (++m_pollAttempts >= maxPollAttempts)
        {
            qCWarning(LauncherLog) << "Timed out waiting for" << m_game->name() << "to start; counting down anyway";
            startCountdown();
        }
    });

    // The final message stays up long enough to read, then the launch view closes by itself
    m_dismiss.setSingleShot(true);
    connect(&m_dismiss, &QTimer::timeout, this, &Launcher::stop);
}

void Launcher::play(Game *game)
{
    if (!game || m_phase == Countdown || m_phase == WaitingForGame)
        return;

    const auto status = GameStatus::instance();
    const auto group = status->group(game);
    if (group != "ready"_L1 && group != "native"_L1)
        return;

    m_game = game;
    m_mod = group == "native"_L1 ? nullptr : status->preferredMod(game);
    m_dismiss.stop();
    m_tick.stop();
    m_poll.stop();

    // Installed mods and native VR only need the game itself
    if (!m_mod || m_mod->type() == Mod::Type::Installable)
    {
        game->launch();
        setPhase(GameStarting);
        m_dismiss.start(8000);
        return;
    }

    m_total = m_mod->launchDelay();
    m_remaining = m_total;
    emit remainingChanged();

    // Kaon can't start games from every store; then the player starts it by hand and Kaon still
    // waits for its binary rather than opening the mod right away
    if (game->canLaunch())
        game->launch();

    // The game may already be running from an earlier start
    if (gameBinaryRunning(m_game))
    {
        onGameReady();
        return;
    }

    m_pollAttempts = 0;
    setPhase(WaitingForGame);
    m_poll.start();
}

void Launcher::openModNow()
{
    m_tick.stop();
    m_poll.stop();
    if (!m_game || !m_mod)
    {
        stop();
        return;
    }

    m_remaining = 0;
    emit remainingChanged();
    // Without auto-attach this is a plain open, e.g. skipping the countdown by hand
    if (autoAttach())
        m_mod->autoLaunch(m_game);
    else
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
        if (game->store() == m_game->store() && game->settingsId() == m_game->settingsId())
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
    m_poll.stop();
    m_dismiss.stop();
    m_remaining = 0;
    emit remainingChanged();
    setPhase(Idle);
    m_game.clear();
    m_mod.clear();
    emit stateChanged();
}

void Launcher::startCountdown()
{
    m_poll.stop();
    if (!m_game || !m_mod)
    {
        stop();
        return;
    }

    m_total = m_mod->launchDelay();
    m_remaining = m_total;
    emit remainingChanged();
    setPhase(Countdown);
    m_tick.start();
}

void Launcher::onGameReady()
{
    m_poll.stop();
    if (!m_game || !m_mod)
    {
        stop();
        return;
    }

    // --attach waits for the game by itself, so there is nothing to count down to
    if (autoAttach())
        openModNow();
    else
        startCountdown();
}

void Launcher::setPhase(Phase phase)
{
    m_phase = phase;
    emit stateChanged();
}

bool Launcher::autoAttach() const
{
    QSettings settings;
    settings.beginGroup("launcher"_L1);
    return settings.value("autoAttach"_L1, true).toBool();
}

void Launcher::setAutoAttach(bool attach)
{
    QSettings settings;
    settings.beginGroup("launcher"_L1);
    settings.setValue("autoAttach"_L1, attach);
    emit autoAttachChanged();
}
