#include "UnrealVrPlugins.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

#include "Game.h"

Q_LOGGING_CATEGORY(VrPluginsLog, "vrplugins")

namespace
{
    // The folders UEVR's frontend warns about: m_discouragedPlugins in its MainWindow.xaml.cs
    const QStringList plugins{"OpenVR"_L1, "OpenXR"_L1, "Oculus"_L1};
    const auto disabledSuffix = ".disabled"_L1;

    // Normally a game has one of these, right below its install directory
    QStringList thirdPartyDirs(const Game *game)
    {
        QStringList dirs;
        for (const auto &root : game->layoutRoots())
        {
            const auto dir = Game::resolveWindowsPath(root, "Engine/Binaries/ThirdParty"_L1);
            if (QFileInfo{dir}.isDir() && !dirs.contains(dir))
                dirs << dir;
        }
        return dirs;
    }

    QStringList found(const Game *game, const QLatin1StringView suffix)
    {
        QStringList names;
        for (const auto &dir : thirdPartyDirs(game))
            for (const auto &plugin : plugins)
                if (QFileInfo{Game::resolveWindowsPath(dir, plugin + suffix)}.isDir() && !names.contains(plugin))
                    names << plugin;
        return names;
    }

    bool rename(const QString &from, const QString &to)
    {
        if (!QDir{}.rename(from, to))
        {
            qCWarning(VrPluginsLog) << "Could not rename" << from << "to" << to;
            return false;
        }
        qCInfo(VrPluginsLog) << "Renamed" << from << "to" << to;
        return true;
    }
} // namespace

QStringList UnrealVrPlugins::present(const Game *game)
{
    return found(game, {});
}

QStringList UnrealVrPlugins::disabled(const Game *game)
{
    return found(game, disabledSuffix);
}

bool UnrealVrPlugins::disable(const Game *game)
{
    bool ok = true;
    for (const auto &dir : thirdPartyDirs(game))
    {
        for (const auto &plugin : plugins)
        {
            const auto folder = Game::resolveWindowsPath(dir, plugin);
            if (!QFileInfo{folder}.isDir())
                continue;

            // Steam puts a plugin back when it verifies or updates the game. A copy renamed before that is the same files
            // again, and it is in the way now.
            const auto renamed = folder + disabledSuffix;
            if (QFileInfo::exists(renamed) && !QDir{renamed}.removeRecursively())
            {
                qCWarning(VrPluginsLog) << "Could not remove" << renamed;
                ok = false;
                continue;
            }
            ok = rename(folder, renamed) && ok;
        }
    }
    return ok;
}

bool UnrealVrPlugins::restore(const Game *game)
{
    bool ok = true;
    for (const auto &dir : thirdPartyDirs(game))
    {
        for (const auto &plugin : plugins)
        {
            const auto renamed = Game::resolveWindowsPath(dir, plugin + disabledSuffix);
            const auto folder = renamed.chopped(disabledSuffix.size());
            if (QFileInfo{renamed}.isDir() && !QFileInfo::exists(folder))
                ok = rename(renamed, folder) && ok;
        }
    }
    return ok;
}
