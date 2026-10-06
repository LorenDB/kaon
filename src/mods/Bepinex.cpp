#include "Bepinex.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSettings>

#include "LaunchOptions.h"
#include "Steam.h"
#include "WinePrefix.h"

Q_LOGGING_CATEGORY(BepinexLog, "bepinex")

namespace
{
    // BepInEx starts from Doorstop, which Windows builds ship as winhttp.dll next to the game. Wine has a winhttp of its
    // own and loads that one unless it is told to prefer the game's copy. BepInEx's guide sets this in winecfg:
    // https://docs.bepinex.dev/articles/advanced/proton_wine.html
    const auto doorstopDll = "winhttp"_L1;

    // Either way of telling Wine will do. Someone who set BepInEx up by hand, or with an older Kaon, did it in the game's
    // launch options in Steam.
    bool doorstopLoads(const Game *game)
    {
        if (WinePrefix::hasNativeDllOverride(game, doorstopDll))
            return true;
        const auto options = Steam::instance()->launchOptions(game);
        return options && LaunchOptions::covers(*options, "WINEDLLOVERRIDES=\"winhttp=n,b\" %command%"_L1);
    }
} // namespace

Bepinex *Bepinex::instance()
{
    static auto b = new Bepinex;
    return b;
}

Bepinex *Bepinex::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QString Bepinex::info() const
{
    return "For Windows games, Kaon also tells the game's Proton prefix to load BepInEx. A native Linux game still needs "
           "its startup script set up by hand."_L1;
}

const QLoggingCategory &Bepinex::logger() const
{
    return BepinexLog();
}

bool Bepinex::hasFilesFor(const Game *game, const Game::LaunchOption &exe) const
{
    return QFileInfo::exists(modInstallDirForGame(game, exe) + "/BepInEx/core/BepInEx.dll"_L1);
}

bool Bepinex::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    const auto exes = acceptableInstallCandidates(game);
    return std::any_of(exes.cbegin(), exes.cend(), [this, game](const auto &exe) {
        if (!hasFilesFor(game, exe))
            return false;
        // Unless Wine is told to, it never loads it, and the game starts as if nothing were installed
        return exe.platform != Game::Platform::Windows || doorstopLoads(game);
    });
}

QString Bepinex::installHoldReason(const Game *game) const
{
    if (!game)
        return {};
    const auto exes = acceptableInstallCandidates(game);
    const bool windows =
        std::any_of(exes.cbegin(), exes.cend(), [](const auto &exe) { return exe.platform == Game::Platform::Windows; });
    return windows ? WinePrefix::editHoldReason(game) : QString{};
}

void Bepinex::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    const bool windows = exe.platform == Game::Platform::Windows;
    if (windows)
    {
        // Checked before anything is unpacked, so a game is never left with BepInEx that can't load
        if (const auto hold = WinePrefix::editHoldReason(game); !hold.isEmpty())
        {
            fail(hold);
            return;
        }
    }

    if (!unpackInto(game, exe))
        return;

    const auto installDir = modInstallDirForGame(game, exe);
    if (exe.platform == Game::Platform::Linux)
    {
        QFile file{installDir + "/run_bepinex.sh"_L1};
        file.setPermissions(file.permissions() | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
        if (file.open(QIODevice::ReadWrite))
        {
            auto content = file.readAll();
            file.resize(0);
            file.write(content.replace("executable_name=\"\""_ba,
                                       "executable_name=\""_ba + exe.executable.split('/').last().toLatin1() + '"'));
            file.close();
        }
    }
    else if (windows)
    {
        ensureDoorstopSearchPath(game, exe);

        QSettings settings;
        settings.beginGroup(settingsGroup());
        settings.beginGroup(game->settingsId());
        if (!WinePrefix::hasNativeDllOverride(game, doorstopDll))
        {
            if (QString error; !WinePrefix::setNativeDllOverride(game, doorstopDll, &error))
            {
                fail(error);
                return;
            }
            // Only an override Kaon made is Kaon's to take away again
            settings.setValue("addedDllOverride"_L1, true);
        }
    }

    Mod::installModImpl(game, exe);
}

void Bepinex::uninstallMod(Game *game)
{
    if (!game)
        return;

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->settingsId());
    if (settings.value("addedDllOverride"_L1, false).toBool())
    {
        if (QString error; !WinePrefix::removeDllOverride(game, doorstopDll, &error))
        {
            failUninstall(error);
            return;
        }
        settings.remove("addedDllOverride"_L1);
    }

    GitHubZipExtractorMod::uninstallMod(game);
}

void Bepinex::ensureDoorstopSearchPath(const Game *game, const Game::LaunchOption &exe) const
{
    const auto installDir = modInstallDirForGame(game, exe);

    // Only needed when the game bundles its own MonoMod (e.g. Haste_Data/Managed/MonoMod.Utils.dll). Otherwise the
    // stock doorstop config is correct and we leave it alone.
    const QFileInfo exeInfo{exe.executable};
    const QString managedDir = exeInfo.absolutePath() + '/' + exeInfo.completeBaseName() + "_Data/Managed"_L1;
    if (!QFileInfo::exists(managedDir + "/MonoMod.Utils.dll"_L1) &&
        !QFileInfo::exists(managedDir + "/MonoMod.RuntimeDetour.dll"_L1))
        return;

    // Without BepInEx there is no config to fix, and opening one for writing would leave a stray file behind
    QFile config{installDir + "/doorstop_config.ini"_L1};
    if (!config.exists())
        return;
    if (!config.open(QIODevice::ReadWrite))
    {
        qCWarning(BepinexLog) << "Could not open doorstop_config.ini to set DLL search path override";
        return;
    }

    // Doorstop 4 (BepInEx 5.4.23 and later) spells the key dll_search_path_override, Doorstop 3 dllSearchPathOverride.
    // Lines are edited in place, without a text mode, so everything else keeps its bytes and line endings.
    static const QRegularExpression key{R"(^\s*(dll_search_path_override|dllSearchPathOverride)\s*=\s*(.*?)\s*$)"_L1};
    const QString searchPath = "BepInEx\\core"_L1;
    auto lines = QString::fromLatin1(config.readAll()).split('\n');
    for (auto &line : lines)
    {
        const bool cr = line.endsWith('\r');
        const auto match = key.match(cr ? line.chopped(1) : line);
        if (!match.hasMatch())
            continue;

        // Someone who set this to something else (unstripped_corlib, say) needed that more than this fix
        if (!match.captured(2).isEmpty())
        {
            if (match.captured(2) != searchPath)
                qCInfo(BepinexLog) << "Leaving the doorstop DLL search path override at" << match.captured(2);
            return;
        }

        line = match.captured(1) + '=' + searchPath;
        if (cr)
            line += '\r';
        config.seek(0);
        config.resize(0);
        config.write(lines.join('\n').toLatin1());
        qCInfo(BepinexLog) << "Set doorstop DLL search path override for game with bundled MonoMod";
        return;
    }

    qCWarning(BepinexLog) << "doorstop_config.ini has no DLL search path override to set";
}

QMap<int, Game::LaunchOption> Bepinex::acceptableInstallCandidates(const Game *game) const
{
    auto options = GitHubZipExtractorMod::acceptableInstallCandidates(game);
    options.removeIf([this, game](const std::pair<int, Game::LaunchOption> &exe) {
        // Filter out IL2CPP builds (I seriously doubt that there are any games with both IL2CPP and Mono builds available at
        // the same time, but who knows. People do crazy things sometimes).
        return QFileInfo::exists(modInstallDirForGame(game, exe.second) + "/GameAssembly.dll"_L1) ||
               QFileInfo::exists(modInstallDirForGame(game, exe.second) + "/GameAssembly.so"_L1);
    });
    return options;
}

bool Bepinex::isThisFileTheActualModDownload(const QString &file) const
{
    return file.startsWith("BepInEx"_L1, Qt::CaseInsensitive) &&
           ((file.contains("linux"_L1, Qt::CaseInsensitive) || file.contains("unix", Qt::CaseInsensitive)) ||
            (file.contains("win"_L1, Qt::CaseInsensitive) || file.startsWith("BepInEx_x"_L1, Qt::CaseInsensitive))) &&
           !file.contains("IL2CPP"_L1, Qt::CaseInsensitive) && !file.contains("NET."_L1, Qt::CaseInsensitive);
}

ModRelease::Asset Bepinex::chooseAssetToInstall(const Game *game, const Game::LaunchOption &exe) const
{
    auto assets = currentRelease()->assets();
    assets.removeIf([game, &exe](const ModRelease::Asset &asset) {
        if (exe.arch == Game::Architecture::x64)
            return !asset.name.contains("x64"_L1, Qt::CaseInsensitive);
        else if (exe.arch == Game::Architecture::x86)
            return !asset.name.contains("x86"_L1, Qt::CaseInsensitive);
        return true;
    });

    assets.removeIf([game, &exe](const ModRelease::Asset &asset) {
        if (exe.platform == Game::Platform::Linux)
            return !asset.name.contains("linux"_L1, Qt::CaseInsensitive) &&
                   !asset.name.contains("unix"_L1, Qt::CaseInsensitive);
        else if (exe.platform == Game::Platform::Windows)
            return !asset.name.contains("win"_L1, Qt::CaseInsensitive) &&
                   !asset.name.startsWith("BepInEx_x"_L1, Qt::CaseInsensitive);
        return true;
    });

    if (assets.size() > 0)
        return assets.constFirst();
    else
        return {};
}

Bepinex::Bepinex(QObject *parent)
    : GitHubZipExtractorMod{parent}
{}
