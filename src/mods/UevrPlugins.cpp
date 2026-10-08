#include "UevrPlugins.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSet>

#include "Game.h"

Q_LOGGING_CATEGORY(UevrPluginsLog, "uevr.plugins")

namespace
{
    QString usersDir(const Game *game)
    {
        if (!game || game->winePrefix().isEmpty())
            return {};
        return QDir{game->winePrefix()}.filePath("drive_c/users"_L1);
    }

    // The Roaming folder UEVR's SHGetSpecialFolderPath(CSIDL_APPDATA) resolves to in this prefix.
    // Proton prefixes keep it under drive_c/users/steamuser; other Wine setups under whoever owns the
    // prefix. Prefers an existing folder so an install lands where UEVR already looks.
    QString findRoaming(const QString &users)
    {
        if (users.isEmpty())
            return {};

        const QDir usersQ{users};
        if (!usersQ.exists())
            return {};

        const auto roamingFor = [&usersQ](const QString &user) { return usersQ.filePath(user + "/AppData/Roaming"_L1); };

        if (QFileInfo{roamingFor("steamuser"_L1)}.isDir())
            return QDir::cleanPath(roamingFor("steamuser"_L1));

        for (const auto &user : usersQ.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            const auto roaming = roamingFor(user);
            if (QFileInfo{roaming}.isDir())
                return QDir::cleanPath(roaming);
        }
        return {};
    }

    // Where to create the Roaming folder when UEVR has never run here yet: the existing user folder,
    // preferring steamuser, so the first install doesn't scatter an extra user.
    QString freshRoaming(const QString &users)
    {
        if (users.isEmpty())
            return {};

        const QDir usersQ{users};
        if (QFileInfo{usersQ.filePath("steamuser"_L1)}.isDir())
            return QDir::cleanPath(usersQ.filePath("steamuser/AppData/Roaming"_L1));

        const auto names = usersQ.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (!names.isEmpty())
            return QDir::cleanPath(usersQ.filePath(names.constFirst() + "/AppData/Roaming"_L1));
        return QDir::cleanPath(usersQ.filePath("steamuser/AppData/Roaming"_L1));
    }

    bool isHelperExecutable(const QString &fileName)
    {
        const auto lower = fileName.toLower();
        return lower.contains("crashreport"_L1) || lower.contains("crashsender"_L1) || lower.contains("prereq"_L1);
    }

    // UEVR names its folder from the process it injects. A Steam launch option is often the
    // small launcher (Game.exe), while the process is Game-Win64-Shipping.exe in Binaries/Win64.
    void addUnrealGameBinaries(const Game *game, const Game::LaunchOption &launcher, QSet<QString> *stems)
    {
        if (!game || game->engine() != Game::Engine::Unreal)
            return;
        const auto dir = game->windowsBinaryDir(launcher);
        if (dir.isEmpty() || !QFileInfo{dir}.isDir())
            return;

        QStringList gameExes;
        for (const auto &name : QDir{dir}.entryList({"*.exe"_L1}, QDir::Files))
        {
            if (!isHelperExecutable(name))
                gameExes << name;
        }
        const bool hasShipping = std::any_of(gameExes.cbegin(), gameExes.cend(), [](const QString &name) {
            return name.contains("Shipping"_L1, Qt::CaseInsensitive);
        });
        for (const auto &name : std::as_const(gameExes))
        {
            if (hasShipping && !name.contains("Shipping"_L1, Qt::CaseInsensitive))
                continue;
            const auto stem = QFileInfo{name}.completeBaseName();
            if (!stem.isEmpty())
                stems->insert(stem);
        }
    }

    // Shipping stems first, so a caller that writes one config file hits the process UEVR injects.
    int stemRank(const QString &stem)
    {
        if (stem.contains("-Win64-Shipping"_L1, Qt::CaseInsensitive))
            return 0;
        if (stem.contains("-Shipping"_L1, Qt::CaseInsensitive))
            return 1;
        if (stem.contains("-Win64-"_L1, Qt::CaseInsensitive))
            return 2;
        return 3;
    }

    QStringList dllNamesIn(const QString &dir)
    {
        if (!QFileInfo{dir}.isDir())
            return {};
        QStringList names;
        // List everything and filter by suffix case-insensitively, so .DLL counts too
        for (const auto &entry : QDir{dir}.entryList(QDir::Files))
            if (entry.endsWith(".dll"_L1, Qt::CaseInsensitive))
                names << entry;
        return names;
    }
} // namespace

QStringList UevrPlugins::exeStems(const Game *game)
{
    QSet<QString> stems;
    if (!game)
        return {};
    for (const auto &exe : game->executables())
    {
        if (exe.platform != Game::Platform::Windows || !QFileInfo::exists(exe.executable))
            continue;
        // completeBaseName() drops only the last extension, so Game.Shipping.exe becomes Game.Shipping,
        // the same stem UEVR takes from the running process.
        const auto stem = QFileInfo{exe.executable}.completeBaseName();
        if (!stem.isEmpty())
            stems.insert(stem);
        addUnrealGameBinaries(game, exe, &stems);
    }
    auto out = stems.values();
    std::sort(out.begin(), out.end(), [](const QString &left, const QString &right) {
        const auto rank = stemRank(left) - stemRank(right);
        if (rank != 0)
            return rank < 0;
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QStringList UevrPlugins::pluginDirsForGame(const Game *game, bool create, QString *error)
{
    if (!game || game->winePrefix().isEmpty())
    {
        if (error)
            *error = "This game has no Wine prefix."_L1;
        return {};
    }
    const auto stems = exeStems(game);
    if (stems.isEmpty())
    {
        if (error)
            *error = "%1 has no Windows executable for UEVR plugins to attach to."_L1.arg(game->name());
        return {};
    }

    QString roaming = findRoaming(usersDir(game));
    if (roaming.isEmpty())
    {
        if (!create)
            return {};
        roaming = freshRoaming(usersDir(game));
    }
    if (roaming.isEmpty())
    {
        if (error)
            *error = "Kaon couldn't find the users folder in this game's Proton prefix."_L1;
        return {};
    }

    QStringList dirs;
    for (const auto &stem : std::as_const(stems))
    {
        const auto dir = roaming + "/UnrealVRMod/"_L1 + stem + "/plugins"_L1;
        if (create)
        {
            if (!QDir{}.mkpath(dir))
            {
                qCWarning(UevrPluginsLog) << "Could not create" << dir;
                if (error)
                    *error = "Kaon couldn't write this game's Proton prefix."_L1;
                return {};
            }
        }
        else if (!QFileInfo{dir}.isDir())
            continue;
        dirs << QDir::cleanPath(dir);
    }
    return dirs;
}

QString UevrPlugins::globalPluginDir(const Game *game, bool create, QString *error)
{
    if (!game || game->winePrefix().isEmpty())
    {
        if (error)
            *error = "This game has no Wine prefix."_L1;
        return {};
    }
    QString roaming = findRoaming(usersDir(game));
    if (roaming.isEmpty())
    {
        if (!create)
            return {};
        roaming = freshRoaming(usersDir(game));
    }
    if (roaming.isEmpty())
    {
        if (error)
            *error = "Kaon couldn't find the users folder in this game's Proton prefix."_L1;
        return {};
    }
    const auto dir = QDir::cleanPath(roaming + "/UEVR/plugins"_L1);
    if (create && !QDir{}.mkpath(dir))
    {
        qCWarning(UevrPluginsLog) << "Could not create" << dir;
        if (error)
            *error = "Kaon couldn't write this game's Proton prefix."_L1;
        return {};
    }
    if (!create && !QFileInfo{dir}.isDir())
        return {};
    return dir;
}

QStringList UevrPlugins::installedPluginNames(const Game *game)
{
    QSet<QString> seenLower;
    QStringList names;
    const auto consider = [&seenLower, &names](const QString &dir) {
        for (const auto &name : dllNamesIn(dir))
        {
            if (seenLower.contains(name.toLower()))
                continue;
            seenLower.insert(name.toLower());
            names << name;
        }
    };
    for (const auto &dir : pluginDirsForGame(game, false))
        consider(dir);
    if (const auto global = globalPluginDir(game, false); !global.isEmpty())
        consider(global);
    names.sort(Qt::CaseInsensitive);
    return names;
}

QString UevrPlugins::installPlugin(Game *game, const QUrl &source)
{
    const auto local = source.isLocalFile() ? source.toLocalFile() : source.toString();
    const QFileInfo picked{local};
    if (local.isEmpty() || !picked.isFile())
        return "Kaon couldn't read that file."_L1;
    if (picked.suffix().compare("dll"_L1, Qt::CaseInsensitive) != 0)
        return "Pick a plugin DLL. \"%1\" isn't one."_L1.arg(picked.fileName());

    QString error;
    const auto dirs = pluginDirsForGame(game, true, &error);
    if (dirs.isEmpty())
        return error.isEmpty() ? "Kaon couldn't find a UEVR plugins folder in this game's prefix."_L1 : error;

    for (const auto &dir : dirs)
    {
        const auto target = dir + '/' + picked.fileName();
        // QFile::copy won't overwrite, and the old file may be the plugin being upgraded
        if (QFileInfo::exists(target) && !QFile::remove(target))
        {
            qCWarning(UevrPluginsLog) << "Could not replace" << target;
            return "Kaon couldn't replace %1. Quit the game and try again."_L1.arg(picked.fileName());
        }
        if (!QFile::copy(picked.absoluteFilePath(), target))
        {
            qCWarning(UevrPluginsLog) << "Could not copy" << picked.absoluteFilePath() << "to" << target;
            return "Kaon couldn't write this game's Proton prefix."_L1;
        }
        qCInfo(UevrPluginsLog) << "Installed UEVR plugin" << picked.fileName() << "for" << (game ? game->name() : QString{})
                               << "into" << dir;
    }
    return {};
}

QString UevrPlugins::removePlugin(Game *game, const QString &fileName)
{
    if (fileName.isEmpty() || fileName.contains('/'_L1) || fileName.contains('\\'_L1))
        return "Kaon couldn't find that plugin."_L1;

    QStringList dirs = pluginDirsForGame(game, false);
    if (const auto global = globalPluginDir(game, false); !global.isEmpty())
        dirs << global;
    if (dirs.isEmpty())
        return "Kaon couldn't find that plugin."_L1;

    bool removedAny = false;
    for (const auto &dir : std::as_const(dirs))
    {
        // The file system may spell the name differently; match case-insensitively like Windows does
        QString target;
        for (const auto &entry : QDir{dir}.entryList(QDir::Files))
        {
            if (entry.compare(fileName, Qt::CaseInsensitive) == 0)
            {
                target = dir + '/' + entry;
                break;
            }
        }
        if (target.isEmpty())
            continue;
        if (!QFile::remove(target))
        {
            qCWarning(UevrPluginsLog) << "Could not remove" << target;
            return "Kaon couldn't remove %1. Quit the game and try again."_L1.arg(fileName);
        }
        removedAny = true;
        qCInfo(UevrPluginsLog) << "Removed UEVR plugin" << fileName << "from" << dir;
    }
    return removedAny ? QString{} : "Kaon couldn't find that plugin."_L1;
}

QString UevrPlugins::manageHoldReason(const Game *game)
{
    if (!game)
        return {};
    if (!game->hasValidWine())
        return game->store() == Game::Store::Custom ?
                   "Set this game's Wine prefix before installing plugins."_L1 :
                   "Launch the game once so it has a Proton prefix for Kaon to install plugins in."_L1;
    if (exeStems(game).isEmpty())
        return "UEVR plugins attach to a game's Windows build, and this game doesn't ship one Kaon can find."_L1;
    return {};
}
