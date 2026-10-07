#include "GameStatus.h"

#include <algorithm>
#include <iterator>

#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QLocale>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include "CustomGames.h"
#include "DownloadManager.h"
#include "GamesFilterModel.h"
#include "Heroic.h"
#include "Itch.h"
#include "LaunchOptions.h"
#include "ModsFilterModel.h"
#include "Steam.h"
#include "UEVRAFW.h"
#include "UnrealVrPlugins.h"
#include "WinePrefix.h"

Q_LOGGING_CATEGORY(GameStatusLog, "gamestatus")

namespace
{
    QVariantMap makeStep(const QString &key,
                         const QString &title,
                         const QString &detail,
                         const QString &state,
                         const QString &action = {},
                         const QString &actionLabel = {})
    {
        QVariantMap step{{"key"_L1, key}, {"title"_L1, title}, {"detail"_L1, detail}, {"state"_L1, state}};
        if (!action.isEmpty())
        {
            step["action"_L1] = action;
            step["actionLabel"_L1] = actionLabel;
        }
        return step;
    }

    QString launcherName(const Game *game)
    {
        switch (game->store())
        {
        case Game::Store::Steam:
            return game->flatpakAppId().isEmpty() ? "Steam"_L1 : "Flatpak Steam"_L1;
        case Game::Store::Heroic:
            return game->flatpakAppId().isEmpty() ? "Heroic"_L1 : "Flatpak Heroic"_L1;
        case Game::Store::Itch:
            return "Itch"_L1;
        default:
            return "its launcher"_L1;
        }
    }

    QString engineName(Game::Engine engine)
    {
        switch (engine)
        {
        case Game::Engine::Unreal:
            return "Unreal Engine"_L1;
        case Game::Engine::Unity:
            return "Unity"_L1;
        case Game::Engine::Godot:
            return "Godot"_L1;
        case Game::Engine::Source:
            return "Source"_L1;
        default:
            return {};
        }
    }

    QString formatSize(qint64 bytes)
    {
        return QLocale{}.formattedDataSize(bytes, 0, QLocale::DataSizeSIFormat);
    }

    // The Proton or Wine build a game runs with, named the way a person would. Proton keeps its Wine binary at
    // <tool>/files/bin/wine, <tool>/files/bin-arm64/wine or <tool>/dist/bin/wine; anything else is shown as a path.
    QString compatToolName(const Game *game)
    {
        QDir dir = QFileInfo{game->wineBinary()}.dir();
        if ((dir.dirName() == "bin"_L1 || dir.dirName() == "bin-arm64"_L1) && dir.cdUp() &&
            (dir.dirName() == "files"_L1 || dir.dirName() == "dist"_L1) && dir.cdUp())
            return dir.dirName();
        return game->wineBinary();
    }

    QString gameKey(const Game *game)
    {
        return QString::fromLatin1(QMetaEnum::fromType<Game::Store>().valueToKey(static_cast<quint64>(game->store()))) +
               '/' + game->settingsId();
    }
} // namespace

GameStatus *GameStatus::instance()
{
    static auto s = new GameStatus;
    return s;
}

GameStatus::GameStatus(QObject *parent)
    : QObject{parent}
{
    auto dm = DownloadManager::instance();
    connect(dm, &DownloadManager::downloadingChanged, this, [this, dm] {
        // Anything still pending once the queue is empty failed without reporting it
        if (!dm->downloading())
        {
            m_pendingDownloads.clear();
            m_installAfterDownload.clear();
        }
        invalidate();
    });
    connect(dm, &DownloadManager::currentDownloadNameChanged, this, [this, dm] {
        // Cover art and release lists come in by the dozen, and no status depends on them
        if (!dm->background())
            invalidate();
    });
    connect(dm, &DownloadManager::downloadFailed, this, [this](const QString &name) {
        for (auto it = m_pendingDownloads.begin(); it != m_pendingDownloads.end();)
        {
            if (it.value() != name)
            {
                ++it;
                continue;
            }
            // Whatever was waiting to be installed from it has nothing to install
            if (const auto mod = modForKey(it.key().section('/'_L1, 0, 0)))
                m_installAfterDownload.remove(mod);
            it = m_pendingDownloads.erase(it);
        }
        invalidate();
    });

    connect(GamesFilterModel::instance(), &GamesFilterModel::gamesChanged, this, &GameStatus::invalidate);
    connect(Steam::instance(), &Steam::launchOptionsChanged, this, &GameStatus::invalidate);

    // Whether a game is running decides what may be changed in its prefix, and nothing announces a game starting or
    // quitting. Looking is cheap, and only happens while Kaon is the window in use.
    auto games = new QTimer{this};
    games->setInterval(2000);
    connect(games, &QTimer::timeout, this, [this] {
        if (const auto running = WinePrefix::prefixesInUse(); running != m_prefixesInUse)
        {
            m_prefixesInUse = running;
            invalidate();
        }
    });

    // Prefixes and installs can change while Kaon is in the background, e.g. when a game is launched from Steam
    if (qGuiApp)
    {
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this, games](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive)
            {
                games->stop();
                return;
            }
            m_prefixesInUse = WinePrefix::prefixesInUse();
            games->start();
            invalidate();
        });
        if (qGuiApp->applicationState() == Qt::ApplicationActive)
            games->start();
    }
}

void GameStatus::watch(Mod *mod)
{
    connect(mod, &Mod::installedInGameChanged, this, &GameStatus::invalidate);
    connect(mod, &Mod::currentReleaseChanged, this, &GameStatus::invalidate);
    connect(mod, &QAbstractItemModel::modelReset, this, &GameStatus::invalidate);
    connect(mod, &Mod::busyChanged, this, &GameStatus::invalidate);
    connect(mod, &Mod::requestChooseLaunchOption, this, &GameStatus::chooseExecutable);
    connect(mod, &Mod::installFailed, this, [this, mod](const QString &message) {
        emit actionFailed("%1 didn't install"_L1.arg(mod->displayName()), message);
    });
    connect(mod, &Mod::uninstallFailed, this, [this, mod](const QString &message) {
        emit actionFailed("%1 is still in the game"_L1.arg(mod->displayName()), message);
    });
    connect(mod, &Mod::downloadFailed, this, [this, mod](const QString &message) {
        // Nothing more will come of this download, and nothing may be installed from it
        const auto prefix = mod->settingsGroup() + '/';
        m_pendingDownloads.removeIf([&prefix](const auto &entry) { return entry.key().startsWith(prefix); });
        m_installAfterDownload.remove(mod);
        emit actionFailed("%1 didn't download"_L1.arg(mod->displayName()), message);
        invalidate();
    });
    connect(mod, &Mod::releaseDownloadedChanged, this, [this, mod](ModRelease *release) {
        m_pendingDownloads.remove(downloadKey(mod, release));
        if (release->downloaded() && release == mod->currentRelease() && m_installAfterDownload.contains(mod))
        {
            if (QPointer<Game> game = m_installAfterDownload.take(mod))
                mod->installMod(game);
        }
        invalidate();
    });
    invalidate();
}

void GameStatus::invalidate()
{
    m_stepsCache.clear();
    if (m_changePending)
        return;

    // Coalesce bursts of signals (a rescan touches every game) into one change notification
    m_changePending = true;
    QTimer::singleShot(0, this, [this] {
        m_changePending = false;
        ++m_revision;
        // Every binding that shows a status runs again here, for every game on screen and for the library's groups
        QElapsedTimer timer;
        timer.start();
        emit changed();
        if (timer.elapsed() > 100)
            qCDebug(GameStatusLog) << "Refreshing every status took" << timer.elapsed() << "ms";
        advanceSetUp();
    });
}

QString GameStatus::group(Game *game, int) const
{
    if (!game)
        return "none"_L1;
    if (game->supportsVr())
        return "native"_L1;
    if (!preferredMod(game))
        return "none"_L1;

    bool pending = false;
    for (const auto &value : steps(game))
    {
        const auto state = value.toMap().value("state"_L1).toString();
        if (state == "block"_L1)
            return "none"_L1;
        if (state == "todo"_L1 || state == "busy"_L1 || state == "wait"_L1)
            pending = true;
    }
    return pending ? "setup"_L1 : "ready"_L1;
}

QVariantList GameStatus::steps(Game *game, int) const
{
    if (!game)
        return {};
    if (const auto it = m_stepsCache.constFind(game); it != m_stepsCache.cend())
        return *it;

    QVariantList out;

    if (game->noWindowsSupport())
        out << makeStep("platform"_L1,
                        "Linux-only build"_L1,
                        "VR mods attach to a game's Windows build, and this game doesn't ship one."_L1,
                        "block"_L1);
    else if (game->hasLinuxBuild() && game->runsWindowsBuild())
        out << makeStep("platform"_L1, "Runs through Proton"_L1, game->windowsBuildReason(), "ok"_L1);
    else if (game->hasLinuxBuild())
    {
        // Steam's settings say which build it starts, so there this is known and nothing after it can work until it is
        // dealt with. For other launchers it is a guess, and stays a warning.
        const bool steam = game->store() == Game::Store::Steam;
        auto step = makeStep("platform"_L1,
                             "Native Linux build"_L1,
                             steam ? "Steam starts this game's Linux build, and the mod can only attach to the Windows one. "
                                     "Pick a Proton version under Compatibility in the game's Steam properties, then "
                                     "rescan."_L1 :
                                     "Run the Windows build through Wine or Proton so the mod can attach."_L1,
                             steam ? "todo"_L1 : "warn"_L1);
        if (game->canOpenSettings())
        {
            step["action"_L1] = "steamSettings"_L1;
            step["actionLabel"_L1] = "Steam properties"_L1;
        }
        if (steam)
        {
            step["secondaryAction"_L1] = "rescan"_L1;
            step["secondaryLabel"_L1] = "Rescan"_L1;
        }
        out << step;
    }

    if (!game->noWindowsSupport())
    {
        if (game->hasValidWine())
            out << makeStep("proton"_L1,
                            game->store() == Game::Store::Custom ? "Wine prefix"_L1 : "Proton prefix"_L1,
                            compatToolName(game),
                            "ok"_L1);
        else if (game->store() == Game::Store::Custom)
            out << makeStep("proton"_L1,
                            "Wine prefix not found"_L1,
                            "The Wine binary or prefix set for this game doesn't exist."_L1,
                            "todo"_L1);
        else
        {
            // A prefix that Wine has set up can be there without the Proton build that made it, e.g. once Steam has
            // removed that version
            const bool prefixOnly = QFileInfo::exists(game->winePrefix() + "/system.reg"_L1);
            const auto detail = prefixOnly ? "This game has a prefix, but Kaon can't find the Proton build it belongs to. "
                                             "Launch the game once from %1, then rescan."_L1 :
                                             "Launch the game once from %1 so it creates one, then rescan."_L1;
            auto step = makeStep("proton"_L1,
                                 prefixOnly ? "Proton build not found"_L1 : "No Proton prefix yet"_L1,
                                 detail.arg(launcherName(game)),
                                 "todo"_L1);
            if (game->canLaunch())
            {
                step["action"_L1] = "launchOnce"_L1;
                step["actionLabel"_L1] = "Launch once"_L1;
            }
            step["secondaryAction"_L1] = "rescan"_L1;
            step["secondaryLabel"_L1] = "Rescan"_L1;
            out << step;
        }
    }

    if (game->hasAnticheat())
        out << makeStep("anticheat"_L1,
                        "Anticheat detected"_L1,
                        "Online modes may ban modded games. Stay offline to be safe."_L1,
                        "warn"_L1);
    else
        out << makeStep("anticheat"_L1, "No anticheat"_L1, "Nothing found in the game folder"_L1, "ok"_L1);

    if (const auto mod = preferredMod(game))
    {
        if (mod->conflictsWithBundledVrPlugins())
        {
            if (const auto present = UnrealVrPlugins::present(game); !present.isEmpty())
                out << makeStep("vrPlugins"_L1,
                                "Built-in VR plugins"_L1,
                                "This game ships %1, which %2 warns will cause issues. Disabling renames the folders, and "
                                "they can be put back."_L1.arg(QLocale{}.createSeparatedList(present), mod->displayName()),
                                "warn"_L1,
                                "disableVrPlugins"_L1,
                                "Disable"_L1);
            else if (const auto disabled = UnrealVrPlugins::disabled(game); !disabled.isEmpty())
            {
                auto step = makeStep("vrPlugins"_L1,
                                     "Built-in VR plugins disabled"_L1,
                                     "%1 renamed in the game folder"_L1.arg(QLocale{}.createSeparatedList(disabled)),
                                     "ok"_L1);
                step["secondaryAction"_L1] = "restoreVrPlugins"_L1;
                step["secondaryLabel"_L1] = "Restore"_L1;
                out << step;
            }
        }

        for (const auto dep : mod->dependencies())
        {
            const auto key = dep->settingsGroup();
            const auto release = dep->currentRelease();
            if (dep->isBusyForGame(game))
                out << makeStep(key, dep->displayName(), "Installing"_L1, "busy"_L1);
            else if (dep->isInstalledForGame(game))
            {
                auto step = makeStep(key,
                                     dep->displayName(),
                                     dep->installsIntoPrefix() ? "Installed in this game's prefix"_L1 : "Installed"_L1,
                                     "ok"_L1);
                step["secondaryAction"_L1] = dep->hasRepairOption() ? "repair"_L1 : "uninstall"_L1;
                step["secondaryLabel"_L1] = dep->hasRepairOption() ? "Repair or remove"_L1 : "Uninstall"_L1;
                out << step;
            }
            else if (release && isDownloading(dep, release))
                out << makeStep(key, dep->displayName(), "Downloading"_L1, "busy"_L1);
            else if (const auto hold = dep->installHoldReason(game); !hold.isEmpty())
                out << makeStep(key, dep->displayName(), hold, "wait"_L1);
            else if (!release)
                out << makeStep(key,
                                dep->displayName(),
                                "Looking for releases. Check your connection if this doesn't change."_L1,
                                "wait"_L1,
                                "refresh"_L1,
                                "Check again"_L1);
            else
            {
                QString detail = dep->installsIntoPrefix() ? "Not installed in this game's prefix"_L1 : "Not installed"_L1;
                if (release && !release->downloaded() && release->size() > 0)
                    detail += ". Downloads %1 first."_L1.arg(formatSize(release->size()));
                out << makeStep(key, dep->displayName(), detail, "todo"_L1, "install"_L1, "Install"_L1);
            }
        }

        const auto key = mod->settingsGroup();
        const auto release = mod->currentRelease();
        const bool installable = mod->type() == Mod::Type::Installable;
        if (!release)
            out << makeStep(key,
                            mod->displayName(),
                            "Looking for releases. Check your connection if this doesn't change."_L1,
                            "wait"_L1,
                            "refresh"_L1,
                            "Check again"_L1);
        else if (installable && mod->isInstalledForGame(game))
        {
            const auto installed = mod->releaseInstalledForGame(game);
            auto step = makeStep(key,
                                 installed ? mod->releaseTitle(installed) : mod->displayName(),
                                 "Installed in the game folder"_L1,
                                 "ok"_L1);
            step["secondaryAction"_L1] = "uninstall"_L1;
            step["secondaryLabel"_L1] = "Uninstall"_L1;
            out << step;
        }
        else if (mod->isBusyForGame(game))
            out << makeStep(key, mod->releaseTitle(release), "Installing"_L1, "busy"_L1);
        else if (isDownloading(mod, release))
            out << makeStep(key, mod->releaseTitle(release), "Downloading"_L1, "busy"_L1);
        else if (const auto hold = mod->installHoldReason(game); installable && !hold.isEmpty())
        {
            if (release->downloaded())
                out << makeStep(key, mod->releaseTitle(release), hold, "wait"_L1);
            else
                out << makeStep(key, mod->releaseTitle(release), hold, "todo"_L1, "download"_L1, "Download"_L1);
        }
        else if (!release->downloaded())
        {
            QString detail = installable ? "Not installed in this game"_L1 : "Not downloaded yet"_L1;
            if (release->size() > 0)
                detail += installable ? ". Downloads %1 first."_L1.arg(formatSize(release->size())) :
                                        " (%1)"_L1.arg(formatSize(release->size()));
            out << makeStep(key,
                            mod->releaseTitle(release),
                            detail,
                            "todo"_L1,
                            installable ? "install"_L1 : "download"_L1,
                            installable ? "Install"_L1 : "Download"_L1);
        }
        else if (installable)
            out << makeStep(key,
                            mod->releaseTitle(release),
                            "Downloaded, not installed in this game"_L1,
                            "todo"_L1,
                            "install"_L1,
                            "Install"_L1);
        else
            out << makeStep(key, mod->releaseTitle(release), "Downloaded"_L1, "ok"_L1);
    }

    if (const auto step = launchOptionsStep(game); !step.isEmpty())
        out << step;

    // Soft guidance from the preferred mod (e.g. AFW in-game toggles Kaon can't verify itself).
    if (const auto mod = preferredMod(game))
        for (const auto &hint : mod->softHints(game))
            out << hint;

    m_stepsCache.insert(game, out);
    return out;
}

QList<Mod *> GameStatus::modsWithLaunchOptions(Game *game) const
{
    QList<Mod *> mods;
    // A game with its own VR mode isn't played with a mod, even where one would fit its engine. And until a mod is in
    // the game, there is nothing for launch options to load.
    if (const auto mod = game->supportsVr() ? nullptr : preferredMod(game);
        mod && (mod->type() == Mod::Type::Launchable || mod->isInstalledForGame(game)))
        mods << mod;
    for (const auto mod : extraMods(game))
        if (mod->isInstalledForGame(game))
            mods << mod;
    mods.removeIf([](Mod *mod) { return mod->launchOptions().isEmpty(); });
    return mods;
}

QVariantMap GameStatus::launchOptionsStep(Game *game) const
{
    // A game added by hand is started by Kaon, which passes these along itself
    if (game->store() == Game::Store::Custom)
        return {};

    const auto mods = modsWithLaunchOptions(game);
    if (mods.isEmpty())
        return {};

    QStringList parts, conflicts, names;
    for (const auto mod : mods)
    {
        parts << mod->launchOptions();
        conflicts << mod->conflictingLaunchOptions();
        names << mod->displayName();
    }
    const auto required = LaunchOptions::merge(parts);

    // Steam keeps what the player typed in a file Kaon can read. Other launchers don't.
    const auto current = Steam::instance()->launchOptions(game);
    if (current && LaunchOptions::covers(*current, required, conflicts))
        return makeStep("launchOptions"_L1, "Launch options"_L1, "Set in the game's Steam properties"_L1, "ok"_L1);

    const auto orElse = "%1 won't load without them."_L1.arg(QLocale{}.createSeparatedList(names));
    QString detail;
    QString text = required;
    if (!current)
        detail = "Add these to the game's launch options in %1. %2"_L1.arg(launcherName(game), orElse);
    else if (current->trimmed().isEmpty())
        detail = "Paste these into the launch options in the game's Steam properties. %1"_L1.arg(orElse);
    else if (!LaunchOptions::isSimple(*current))
        detail = "This game's launch options in Steam run a script, which Kaon won't rewrite. Add these to them by "
                 "hand. %1"_L1.arg(orElse);
    else
    {
        text = LaunchOptions::combined(*current, required, conflicts);
        const auto dropped = LaunchOptions::conflictsIn(*current, conflicts);
        detail = dropped.isEmpty() ?
                     "Replace the launch options in the game's Steam properties with these. They keep what is there now. "
                     "%1"_L1.arg(orElse) :
                     "Replace the launch options in the game's Steam properties with these. They keep what is there now, "
                     "except %1, which gets in the mod's way. %2"_L1.arg(QLocale{}.createSeparatedList(dropped), orElse);
    }

    // A warning, not a step in the way: Steam writes its settings down a while after they change, and Kaon can't see
    // other launchers' at all
    auto step = makeStep("launchOptions"_L1, "Launch options"_L1, detail, "warn"_L1);
    step["copyText"_L1] = text;
    if (game->canOpenSettings())
    {
        step["secondaryAction"_L1] = "steamSettings"_L1;
        step["secondaryLabel"_L1] = "Steam properties"_L1;
    }
    return step;
}

QString GameStatus::summary(Game *game, int) const
{
    if (!game)
        return {};

    const auto g = group(game);
    if (g == "native"_L1)
        return game->vrOnly() ? "VR only"_L1 : "Built-in VR mode"_L1;
    if (g == "none"_L1)
    {
        if (game->noWindowsSupport())
            return "No Windows build"_L1;
        const auto engine = engineName(game->engine());
        return engine.isEmpty() ? "Engine not recognized"_L1 : "No %1 mod yet"_L1.arg(engine);
    }

    const auto mod = preferredMod(game);
    if (g == "ready"_L1)
    {
        // A tool that needs them and doesn't have them is not what keeps the game from VR
        if (!mod->launchOptions().isEmpty() && findStep(game, "launchOptions"_L1).value("state"_L1).toString() == "warn"_L1)
            return "Set launch options"_L1;
        return mod->type() == Mod::Type::Launchable ? "Ready with %1"_L1.arg(mod->displayName()) :
                                                      "%1 installed"_L1.arg(mod->displayName());
    }

    for (const auto &value : steps(game))
    {
        const auto step = value.toMap();
        const auto state = step.value("state"_L1).toString();
        const auto key = step.value("key"_L1).toString();
        if (state == "ok"_L1 || state == "warn"_L1)
            continue;
        if (key == "platform"_L1)
            return "Needs Proton forced in Steam"_L1;
        if (key == "proton"_L1)
            return game->store() == Game::Store::Custom ? "Wine prefix not found"_L1 :
                                                          "Launch once in %1"_L1.arg(launcherName(game));
        const auto stepMod = modForKey(key);
        const auto name = stepMod ? stepMod->displayName() : step.value("title"_L1).toString();
        if (state == "busy"_L1)
            return step.value("detail"_L1).toString() == "Downloading"_L1 ? "Downloading %1"_L1.arg(name) :
                                                                            "Installing %1"_L1.arg(name);
        if (step.value("action"_L1).toString() == "download"_L1)
            return "Needs %1 download"_L1.arg(name);
        return "Needs %1"_L1.arg(name);
    }
    return {};
}

QList<Mod *> GameStatus::vrMods(Game *game, int) const
{
    QList<Mod *> list;
    if (!game)
        return list;
    for (const auto mod : ModsFilterModel::allMods())
        if (mod->providesVr() && mod->isCompatibleWith(game))
            list << mod;
    std::sort(list.begin(), list.end(), [](Mod *a, Mod *b) { return a->displayName() < b->displayName(); });
    return list;
}

Mod *GameStatus::preferredMod(Game *game, int) const
{
    const auto mods = vrMods(game);
    if (mods.isEmpty())
        return nullptr;

    QSettings settings;
    settings.beginGroup("preferredMod"_L1);
    const auto wanted = settings.value(gameKey(game)).toString();
    for (const auto mod : mods)
        if (mod->settingsGroup() == wanted)
            return mod;
    return mods.first();
}

void GameStatus::setPreferredMod(Game *game, Mod *mod)
{
    if (!game || !mod)
        return;
    QSettings settings;
    settings.beginGroup("preferredMod"_L1);
    settings.setValue(gameKey(game), mod->settingsGroup());
    invalidate();
}

QList<Mod *> GameStatus::extraMods(Game *game) const
{
    QList<Mod *> list;
    if (!game)
        return list;
    const auto preferred = preferredMod(game);
    const auto needed = preferred ? preferred->dependencies() : QList<Mod *>{};
    for (const auto mod : ModsFilterModel::allMods())
        if (!mod->providesVr() && !needed.contains(mod) && mod->isCompatibleWith(game))
            list << mod;
    return list;
}

QVariantList GameStatus::tools(Game *game, int) const
{
    auto mods = extraMods(game);
    std::sort(mods.begin(), mods.end(), [](Mod *a, Mod *b) { return a->displayName() < b->displayName(); });

    QVariantList rows;
    for (const auto mod : std::as_const(mods))
    {
        const auto release = mod->currentRelease();
        const bool on = mod->isInstalledForGame(game);
        const bool working = mod->isBusyForGame(game) || (release && isDownloading(mod, release));
        const auto hold = mod->installHoldReason(game);

        QString detail;
        if (!hold.isEmpty())
            detail = hold;
        else if (working)
            detail = mod->isBusyForGame(game) ? "Installing"_L1 : "Downloading"_L1;
        else if (on)
            detail = mod->installedNote();
        else if (!release)
            detail = "Looking for a download"_L1;

        // "Quit the game" goes stale while the page stays open, so that one stays clickable and is checked again
        // when the switch is used
        const bool held = !hold.isEmpty() && !WinePrefix::inUse(game);
        rows << QVariantMap{{"mod"_L1, QVariant::fromValue(mod)},
                            {"on"_L1, on},
                            {"working"_L1, working},
                            {"enabled"_L1, !held && !working && (on || release)},
                            {"detail"_L1, detail}};
    }
    return rows;
}

void GameStatus::toggleTool(Game *game, Mod *mod)
{
    if (!game || !mod || mod->isBusyForGame(game))
        return;

    if (const auto hold = mod->installHoldReason(game); !hold.isEmpty())
        emit noticed(hold);
    else if (mod->isInstalledForGame(game))
        mod->uninstallMod(game);
    else if (!mod->currentRelease())
        emit noticed("Still looking for a %1 download"_L1.arg(mod->displayName()));
    else
        installForGame(game, mod);
    invalidate();
}

bool GameStatus::isInstalled(Mod *mod, Game *game, int) const
{
    return mod && game && mod->isInstalledForGame(game);
}

ModRelease *GameStatus::installedRelease(Mod *mod, Game *game, int) const
{
    return mod && game ? mod->releaseInstalledForGame(game) : nullptr;
}

bool GameStatus::usesLaunchDelay(Game *game, int) const
{
    if (!game || !game->canLaunch())
        return false;
    const auto mod = preferredMod(game);
    return mod && mod->type() == Mod::Type::Launchable;
}

QString GameStatus::launchOptions(const Game *game) const
{
    if (!game)
        return {};

    QStringList parts;
    // Nothing here changes the game. QML hands games over without const, and so the functions it calls take them that way.
    for (const auto mod : modsWithLaunchOptions(const_cast<Game *>(game)))
        parts << mod->launchOptions();
    return LaunchOptions::merge(parts);
}

void GameStatus::openSteamProperties(Game *game)
{
    if (!game || !game->canOpenSettings())
        return;
    const auto url = "steam://gameproperties/"_L1 + game->id();
    if (game->flatpakAppId().isEmpty())
        QDesktopServices::openUrl(QUrl{url});
    else if (!QProcess::startDetached("flatpak"_L1, {"run"_L1, game->flatpakAppId(), url}))
        qCWarning(GameStatusLog) << "Could not open Flatpak Steam properties for" << game->id();
}

void GameStatus::openFolder(Game *game)
{
    // Built here and not in QML: a folder name may hold characters that mean something else in a URL
    if (game && !game->installDir().isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(game->installDir()));
}

QList<Mod *> GameStatus::allMods() const
{
    auto list = ModsFilterModel::allMods();
    // VR mods first, then what they depend on
    std::stable_sort(list.begin(), list.end(), [](Mod *a, Mod *b) {
        if (a->providesVr() != b->providesVr())
            return a->providesVr();
        return a->displayName() < b->displayName();
    });
    return list;
}

void GameStatus::runStep(Game *game, const QString &key, bool secondary)
{
    if (!game)
        return;

    const auto step = findStep(game, key);
    const auto action = step.value(secondary ? "secondaryAction"_L1 : "action"_L1).toString();

    if (action == "launchOnce"_L1)
        game->launch();
    else if (action == "steamSettings"_L1)
        openSteamProperties(game);
    else if (action == "rescan"_L1)
        rescanLibraries();
    else if (action == "disableVrPlugins"_L1 || action == "restoreVrPlugins"_L1)
    {
        const bool disabling = action == "disableVrPlugins"_L1;
        if (!(disabling ? UnrealVrPlugins::disable(game) : UnrealVrPlugins::restore(game)))
            emit actionFailed(disabling ? "Couldn't disable the VR plugins"_L1 : "Couldn't restore the VR plugins"_L1,
                              "Kaon couldn't rename a folder in %1's Engine/Binaries/ThirdParty. Its log has the details: "
                              "~/.cache/LorenDB/Kaon/kaon.log"_L1.arg(game->name()));
    }
    else if (action == "applyAfwConfig"_L1)
    {
        QString error;
        if (!UEVRAFW::instance()->applyRecommendedConfig(game, &error))
            emit actionFailed("Couldn't write AFW config"_L1, error);
        else
            emit noticed("Wrote AFW rendering method, Ghosting Fix"_L1
                         + (UEVRAFW::instance()->isJoeyhodgeRelease(UEVRAFW::instance()->currentRelease())
                                ? ", and Bootstrap"_L1
                                : QString{})
                         + " into UEVR's config for this game."_L1);
    }
    else if (const auto mod = modForKey(key))
    {
        if (action == "refresh"_L1)
            mod->refreshReleases();
        else if (action == "download"_L1)
            download(mod, mod->currentRelease());
        else if (action == "install"_L1)
        {
            if (const auto release = mod->currentRelease(); release && !release->downloaded())
            {
                m_installAfterDownload.insert(mod, game);
                download(mod, release);
            }
            else
                mod->installMod(game);
        }
        else if (action == "uninstall"_L1 || action == "repair"_L1)
            mod->uninstallMod(game);
    }

    invalidate();
}

bool GameStatus::canSetUp(Game *game, int) const
{
    for (const auto &value : steps(game))
    {
        const auto step = value.toMap();
        const auto state = step.value("state"_L1).toString();
        if (state == "ok"_L1 || state == "warn"_L1)
            continue;
        const auto action = step.value("action"_L1).toString();
        return state == "todo"_L1 && (action == "download"_L1 || action == "install"_L1);
    }
    return false;
}

void GameStatus::setUp(Game *game)
{
    m_setUpGame = game;
    m_setUpLastKey.clear();
    advanceSetUp();
}

void GameStatus::advanceSetUp()
{
    if (!m_setUpGame)
        return;

    for (const auto &value : steps(m_setUpGame))
    {
        const auto step = value.toMap();
        const auto state = step.value("state"_L1).toString();
        const auto key = step.value("key"_L1).toString();
        if (state == "ok"_L1 || state == "warn"_L1)
            continue;
        if (state == "busy"_L1)
            return; // check again once it finishes

        const auto action = step.value("action"_L1).toString();
        // A step that's still open after we ran it failed or needs the user; stop there instead of looping
        if (state == "todo"_L1 && (action == "download"_L1 || action == "install"_L1) && key != m_setUpLastKey)
        {
            m_setUpLastKey = key;
            runStep(m_setUpGame, key);
            return;
        }
        break;
    }

    m_setUpGame.clear();
    m_setUpLastKey.clear();
}

void GameStatus::installForGame(Game *game, Mod *mod)
{
    if (!game || !mod)
        return;

    const auto release = mod->currentRelease();
    if (!release)
        return;
    if (release->downloaded())
        mod->installMod(game);
    else if (!release->assets().isEmpty())
    {
        m_installAfterDownload.insert(mod, game);
        download(mod, release);
    }
}

void GameStatus::download(Mod *mod, ModRelease *release)
{
    if (!mod || !release || release->downloaded() || release->assets().isEmpty())
        return;
    m_pendingDownloads.insert(downloadKey(mod, release), mod->releaseTitle(release));
    mod->downloadRelease(release);
    invalidate();
}

bool GameStatus::isDownloading(Mod *mod, ModRelease *release, int) const
{
    return mod && release && !release->downloaded() && m_pendingDownloads.contains(downloadKey(mod, release));
}

void GameStatus::retargetGames(const QList<Game *> &previous, const QList<Game *> &current)
{
    const auto mapGame = [&](Game *old) -> Game * {
        if (!old || !previous.contains(old))
            return old;
        for (auto *game : current)
            if (game->store() == old->store() && game->id() == old->id())
                return game;
        return nullptr;
    };

    if (m_setUpGame)
    {
        m_setUpGame = mapGame(m_setUpGame);
        if (!m_setUpGame)
            m_setUpLastKey.clear();
    }

    QHash<Mod *, QPointer<Game>> pending;
    for (auto it = m_installAfterDownload.cbegin(); it != m_installAfterDownload.cend(); ++it)
        if (auto *next = mapGame(it.value()))
            pending.insert(it.key(), next);
    m_installAfterDownload = pending;
}

void GameStatus::rescanLibraries()
{
    for (Store *store :
         std::initializer_list<Store *>{Steam::instance(), Heroic::instance(), Itch::instance(), CustomGames::instance()})
        store->scanStore();
}

Mod *GameStatus::modForKey(const QString &key) const
{
    for (const auto mod : ModsFilterModel::allMods())
        if (mod->settingsGroup() == key)
            return mod;
    return nullptr;
}

QVariantMap GameStatus::findStep(Game *game, const QString &key) const
{
    for (const auto &value : steps(game))
        if (const auto step = value.toMap(); step.value("key"_L1).toString() == key)
            return step;
    return {};
}

QString GameStatus::downloadKey(Mod *mod, ModRelease *release)
{
    return mod->settingsGroup() + '/' + QString::number(release->id());
}
