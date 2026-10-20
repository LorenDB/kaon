#include "OptiScaler.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QtConcurrentRun>

#include "Aptabase.h"
#include "Archive.h"
#include "GamesFilterModel.h"

Q_LOGGING_CATEGORY(OptiScalerLog, "optiscaler")

namespace
{
    QMap<int, Game::LaunchOption> windowsCandidates(const Game *game)
    {
        QMap<int, Game::LaunchOption> options;
        if (!game || game->noWindowsSupport())
            return options;

        for (auto it = game->executables().cbegin(); it != game->executables().cend(); ++it)
        {
            const auto &exe = it.value();
            if (exe.platform != Game::Platform::Windows || !QFileInfo::exists(exe.executable))
                continue;
            // OptiScaler is 64-bit only.
            if (exe.arch != Game::Architecture::x64)
                continue;
            options.insert(it.key(), exe);
        }
        return options;
    }

    QString backupPath(const QString &dest)
    {
        return dest + ".kaon-bak"_L1;
    }

    QString rollbackPath(const QString &dest)
    {
        return dest + ".kaon-rollback"_L1;
    }

    // The archive ships support DLLs (libxess, FidelityFX) that a game may already have in this folder.
    // A .kaon-bak beside the file is that original. A .kaon-rollback is only the previous OptiScaler copy,
    // kept until this install finishes so a later failure can put it back.
    bool placeFile(const QString &from,
                   const QString &to,
                   const QSet<QString> &alreadyOurs,
                   QStringList *written,
                   QStringList *backedUp)
    {
        const auto dest = QFileInfo{to}.absoluteFilePath();
        const auto staging = dest + ".kaon-new"_L1;
        const auto backup = backupPath(dest);
        const auto rollback = rollbackPath(dest);
        QFile::remove(staging);
        QFile::remove(rollback);
        if (!QFile::copy(from, staging))
            return false;

        const bool existed = QFileInfo::exists(dest);
        // A backup from an earlier install is the game's file. Replacing our own copy must not overwrite it.
        const bool keepOriginal = existed && !alreadyOurs.contains(dest) && !QFileInfo::exists(backup);
        const auto saved = keepOriginal ? backup : rollback;
        if (existed && !QFile::copy(dest, saved))
        {
            QFile::remove(staging);
            return false;
        }
        if (existed && !QFile::remove(dest))
        {
            QFile::remove(staging);
            QFile::remove(saved);
            return false;
        }
        if (!QFile::rename(staging, dest))
        {
            QFile::remove(staging);
            if (existed)
                QFile::rename(saved, dest);
            return false;
        }

        *written << dest;
        if (keepOriginal)
            *backedUp << dest;
        return true;
    }

    // Put back whatever placeFile moved aside. A failed remove leaves the backup in place.
    void undoPlace(const QString &dest, const QStringList &backedUp)
    {
        const auto backup = backupPath(dest);
        const auto rollback = rollbackPath(dest);
        const bool freshBackup = backedUp.contains(dest);
        if (QFileInfo::exists(dest) && !QFile::remove(dest))
            return;
        if (freshBackup)
            QFile::rename(backup, dest);
        else if (QFileInfo::exists(rollback))
            QFile::rename(rollback, dest);
    }

    // Uninstall: the game's file comes back when we saved one. Otherwise the path is one we created.
    bool restoreOrRemove(const QString &dest)
    {
        const auto file = QFileInfo{dest}.absoluteFilePath();
        const auto backup = backupPath(file);
        QFile::remove(rollbackPath(file));
        if (!QFileInfo::exists(backup))
            return !QFileInfo::exists(file) || QFile::remove(file);

        const auto staging = file + ".kaon-restore"_L1;
        QFile::remove(staging);
        if (!QFile::copy(backup, staging))
            return false;
        if (QFileInfo::exists(file) && !QFile::remove(file))
        {
            QFile::remove(staging);
            return false;
        }
        if (!QFile::rename(staging, file))
        {
            QFile::remove(staging);
            return false;
        }
        QFile::remove(backup);
        return true;
    }

    void removeEmptyParents(const QStringList &files, const QString &stopAt)
    {
        QStringList parents;
        const auto gameDir = QDir::cleanPath(stopAt);
        for (const auto &file : files)
        {
            for (auto parent = QFileInfo{file}.absolutePath();
                 !gameDir.isEmpty() && parent.startsWith(gameDir + '/'_L1) && !parents.contains(parent);
                 parent = QFileInfo{parent}.absolutePath())
                parents << parent;
        }
        std::sort(parents.begin(), parents.end(), [](const auto &left, const auto &right) {
            return left.count('/'_L1) > right.count('/'_L1);
        });
        for (const auto &parent : std::as_const(parents))
            if (QDir parentDir{parent}; parentDir.exists() && parentDir.isEmpty())
                parentDir.rmdir(parent);
    }

    // Copy a tree from extracted/ into dest/, recording every file written. Skips OptiScaler.dll (placed as dxgi.dll
    // separately) and leaves an existing OptiScaler.ini alone.
    bool copyTree(const QString &fromDir,
                  const QString &toDir,
                  const QSet<QString> &alreadyOurs,
                  QStringList *written,
                  QStringList *backedUp,
                  QString *error)
    {
        QDir source{fromDir};
        if (!QDir{}.mkpath(toDir))
        {
            *error = "Couldn't create %1."_L1.arg(toDir);
            return false;
        }

        for (const auto &entry : source.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            if (!copyTree(entry.absoluteFilePath(), toDir + '/' + entry.fileName(), alreadyOurs, written, backedUp, error))
                return false;
        }
        for (const auto &entry : source.entryInfoList(QDir::Files))
        {
            const auto name = entry.fileName();
            if (name.compare("OptiScaler.dll"_L1, Qt::CaseInsensitive) == 0)
                continue;
            const auto dest = toDir + '/' + name;
            if (name.compare("OptiScaler.ini"_L1, Qt::CaseInsensitive) == 0 && QFileInfo::exists(dest))
                continue;
            if (!placeFile(entry.absoluteFilePath(), dest, alreadyOurs, written, backedUp))
            {
                *error = "Couldn't place %1. Quit the game and try again."_L1.arg(name);
                return false;
            }
        }
        return true;
    }
} // namespace

OptiScaler *OptiScaler::instance()
{
    static auto opti = new OptiScaler;
    return opti;
}

OptiScaler *OptiScaler::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QString OptiScaler::info() const
{
    return "Works on its own or with UEVR AFW when the GPU has no native DLSS. Installs as dxgi.dll next to the game "
           "(Unreal: Binaries/Win64) and sets WINEDLLOVERRIDES. Cannot be on at the same time as VR Performance "
           "Toolkit, which also uses dxgi.dll. Overlay: Insert (Alt+Insert on some layouts); Page Up/Down for "
           "stats. Games that need a different hook name (winmm, version, ...) still need a manual rename."_L1;
}

QString OptiScaler::launchOptions() const
{
    // Proton must load the game's dxgi.dll instead of the one it put in system32.
    return "WINEDLLOVERRIDES=\"dxgi=n,b\" %command%"_L1;
}

const QLoggingCategory &OptiScaler::logger() const
{
    return OptiScalerLog();
}

QString OptiScaler::configFileForGame(const Game *game) const
{
    if (!game)
        return {};

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->settingsId());
    const auto recorded = settings.value("installedDll"_L1).toString();
    if (!recorded.isEmpty())
    {
        const auto ini = QFileInfo{recorded}.absolutePath() + "/OptiScaler.ini"_L1;
        if (QFileInfo::exists(ini))
            return QFileInfo{ini}.absoluteFilePath();
    }

    for (const auto &exe : acceptableInstallCandidates(game))
    {
        const auto dir = game->windowsBinaryDir(exe);
        const auto ini = dir + "/OptiScaler.ini"_L1;
        if (QFileInfo::exists(dir + "/dxgi.dll"_L1) && QFileInfo::exists(ini))
            return QFileInfo{ini}.absoluteFilePath();
    }
    return {};
}

bool OptiScaler::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->settingsId());
    const auto recorded = settings.value("installedDll"_L1).toString();
    if (!recorded.isEmpty() && QFileInfo::exists(recorded))
        return true;

    const auto exes = acceptableInstallCandidates(game);
    return std::any_of(exes.cbegin(), exes.cend(), [game](const auto &exe) {
        const auto dir = game->windowsBinaryDir(exe);
        return QFileInfo::exists(dir + "/dxgi.dll"_L1) && QFileInfo::exists(dir + "/OptiScaler.ini"_L1);
    });
}

QMap<int, Game::LaunchOption> OptiScaler::acceptableInstallCandidates(const Game *game) const
{
    return windowsCandidates(game);
}

QList<Game::LaunchOption> OptiScaler::preferredInstallCandidates(const Game *game,
                                                                 const QList<Game::LaunchOption> &all) const
{
    QList<Game::LaunchOption> distinct;
    QStringList dirs;
    for (const auto &exe : all)
    {
        if (const auto dir = game->windowsBinaryDir(exe); !dirs.contains(dir))
        {
            dirs << dir;
            distinct << exe;
        }
    }
    return distinct;
}

bool OptiScaler::isThisFileTheActualModDownload(const QString &file) const
{
    // Official releases are Optiscaler_….7z. Ignore source archives and odd extras.
    return file.endsWith(".7z"_L1, Qt::CaseInsensitive) && file.contains("Optiscaler"_L1, Qt::CaseInsensitive);
}

void OptiScaler::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    if (!game)
        return;
    // The extract plus copy below blocks for around a second. It runs off the UI thread; the busy flag keeps the
    // switch from firing again and shows "Installing" meanwhile.
    if (isBusyForGame(game))
        return;
    if (!QFileInfo::exists(exe.executable))
    {
        fail("Kaon couldn't find %1. Rescan your libraries and try again."_L1.arg(exe.executable));
        return;
    }

    const auto release = currentRelease();
    if (!release || release->assets().isEmpty())
    {
        fail("OptiScaler has no download yet."_L1);
        return;
    }
    const auto asset = chooseAssetToInstall(game, exe);
    if (asset.id < 0)
    {
        fail("This version of OptiScaler has no download that fits %1."_L1.arg(game->name()));
        return;
    }
    const auto archive = pathForRelease(release, asset);
    if (!QFileInfo::exists(archive))
    {
        fail("Download OptiScaler before turning it on."_L1);
        return;
    }

    // Everything the worker needs is copied out here: the Game object lives on the UI thread and may be replaced by
    // a rescan while the copy runs. Settings stay on this thread too; QSettings objects must not cross threads.
    const auto dir = game->windowsBinaryDir(exe);
    const auto dllDest = QFileInfo{dir + "/dxgi.dll"_L1}.absoluteFilePath();
    const auto iniDest = QFileInfo{dir + "/OptiScaler.ini"_L1}.absoluteFilePath();
    const auto exeName = QFileInfo{exe.executable}.fileName();
    const auto settingsId = game->settingsId();
    const auto gameName = game->name();
    const auto store = static_cast<int>(game->store());
    const auto gameId = settingsId;
    const auto releaseId = release->id();
    const auto releaseName = release->name();
    QSettings snapshot;
    snapshot.beginGroup(settingsGroup());
    snapshot.beginGroup(settingsId);
    const auto snapshotRecorded = snapshot.value("installedDll"_L1).toString();
    const auto recordedPath = snapshotRecorded.isEmpty() ? QString{} : QFileInfo{snapshotRecorded}.absoluteFilePath();
    QStringList priorInstalled;
    for (const auto &file : snapshot.value("installedFiles"_L1).toStringList())
        if (!file.isEmpty())
            priorInstalled << QFileInfo{file}.absoluteFilePath();
    QStringList priorBackedUp;
    for (const auto &file : snapshot.value("backedUpFiles"_L1).toStringList())
        if (!file.isEmpty())
            priorBackedUp << QFileInfo{file}.absoluteFilePath();
    const QPointer<Game> guard{game};
    const Game *busyKey = game;
    setBusyForGame(game, true);

    (void)QtConcurrent::run([this,
                             guard,
                             busyKey,
                             exe,
                             archive,
                             dir,
                             dllDest,
                             iniDest,
                             exeName,
                             recordedPath,
                             priorInstalled,
                             priorBackedUp,
                             settingsId,
                             gameName,
                             store,
                             gameId,
                             releaseId,
                             releaseName] {
        const auto done =
            [this, guard, busyKey, store, gameId, settingsId, gameName, releaseId, releaseName, exe, dllDest, iniDest](
                bool ok, const QString &error, const QStringList &written, const QStringList &backedUp, bool createdIni) {
                QMetaObject::invokeMethod(
                    this,
                    [this,
                     guard,
                     busyKey,
                     store,
                     gameId,
                     settingsId,
                     gameName,
                     releaseId,
                     releaseName,
                     exe,
                     dllDest,
                     iniDest,
                     ok,
                     error,
                     written,
                     backedUp,
                     createdIni] {
                        finishInstall(guard,
                                      busyKey,
                                      store,
                                      gameId,
                                      settingsId,
                                      gameName,
                                      releaseId,
                                      releaseName,
                                      exe,
                                      dllDest,
                                      iniDest,
                                      ok,
                                      error,
                                      written,
                                      backedUp,
                                      createdIni);
                    },
                    Qt::QueuedConnection);
            };

        QTemporaryDir extracted;
        if (!extracted.isValid())
        {
            done(false, "Couldn't create a temporary folder for OptiScaler."_L1, {}, {}, false);
            return;
        }
        if (QString error; !Archive::extract(archive, extracted.path(), &error))
        {
            done(false, error, {}, {}, false);
            return;
        }

        const auto sourceDll = extracted.filePath("OptiScaler.dll"_L1);
        if (!QFileInfo::exists(sourceDll))
        {
            done(false, "This OptiScaler download is missing OptiScaler.dll."_L1, {}, {}, false);
            return;
        }

        // VR Performance Toolkit also installs a dxgi.dll. They cannot share the slot. The recorded path was read
        // on the UI thread before this worker started.
        if (QFileInfo::exists(dir + "/vrperfkit.yml"_L1) && QFileInfo::exists(dllDest) && recordedPath != dllDest)
        {
            done(false,
                 "%1 already has VR Performance Toolkit (also a dxgi.dll). Turn that off for this game first."_L1.arg(
                     exeName),
                 {},
                 {},
                 false);
            return;
        }
        if (QFileInfo::exists(dllDest) && !QFileInfo::exists(iniDest) && recordedPath != dllDest)
        {
            done(false,
                 "%1 already has a dxgi.dll that isn't OptiScaler. Remove that file and try again."_L1.arg(exeName),
                 {},
                 {},
                 false);
            return;
        }

        const QSet<QString> alreadyOurs{priorInstalled.cbegin(), priorInstalled.cend()};
        QStringList written;
        QStringList backedUp;
        // copyTree records files as it goes. A later failure has to put back anything it replaced, or the
        // game's DLL is gone with nothing in settings for uninstall to find.
        const auto discardWritten = [&written, &backedUp, &dir]() {
            for (auto it = written.crbegin(); it != written.crend(); ++it)
                undoPlace(*it, backedUp);
            removeEmptyParents(written, dir);
            written.clear();
            backedUp.clear();
        };

        QString error;
        if (!copyTree(extracted.path(), dir, alreadyOurs, &written, &backedUp, &error))
        {
            discardWritten();
            done(false, error, {}, {}, false);
            return;
        }

        const bool createdIni = !QFileInfo::exists(iniDest);
        if (!placeFile(sourceDll, dllDest, alreadyOurs, &written, &backedUp))
        {
            discardWritten();
            done(false, "Couldn't place dxgi.dll next to %1. Quit the game and try again."_L1.arg(exeName), {}, {}, false);
            return;
        }

        // An ini the user already had is not in `written` (copyTree leaves it). A DLL this version did not
        // replace is still ours, and its .kaon-bak still has the game's original. Keep both for uninstall.
        auto installed = written;
        for (const auto &old : priorInstalled)
            if (!old.isEmpty() && !installed.contains(old) && QFileInfo::exists(old))
                installed << old;
        auto keptBackups = priorBackedUp;
        for (const auto &file : backedUp)
            if (!keptBackups.contains(file))
                keptBackups << file;
        for (const auto &file : installed)
            QFile::remove(rollbackPath(file));

        done(true, {}, installed, keptBackups, createdIni && QFileInfo::exists(iniDest));
    });
}

void OptiScaler::finishInstall(const QPointer<Game> &guard,
                               const Game *busyKey,
                               int store,
                               const QString &gameId,
                               const QString &settingsId,
                               const QString &gameName,
                               int releaseId,
                               const QString &releaseName,
                               const Game::LaunchOption &exe,
                               const QString &dllDest,
                               const QString &iniDest,
                               bool ok,
                               const QString &error,
                               const QStringList &written,
                               const QStringList &backedUp,
                               bool createdIni)
{
    Q_UNUSED(exe);
    // The key is only ever hashed, never dereferenced, so clearing it after a rescan deleted the Game is safe.
    setBusyForGame(busyKey, false);
    if (!ok)
    {
        fail(error);
        return;
    }

    // A rescan may have replaced the Game mid-install. Settings are keyed by id, so they land in the right place
    // either way.
    Game *target = guard ? guard.data() : GamesFilterModel::instance()->gameByIdentity(store, gameId);

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(settingsId);
    if (createdIni)
        settings.setValue("createdIni"_L1, iniDest);
    settings.setValue("installedDll"_L1, dllDest);
    settings.setValue("installedFiles"_L1, written);
    settings.setValue("backedUpFiles"_L1, backedUp);
    settings.setValue("installedVersion"_L1, releaseId);

    Aptabase::instance()->track("install-"_L1 + settingsGroup(), {{"version"_L1, releaseName}, {"game"_L1, gameName}});

    emit installedInGameChanged(target);
}

void OptiScaler::uninstallMod(Game *game)
{
    if (!game)
        return;
    if (isBusyForGame(game))
    {
        failUninstall("OptiScaler is still installing for %1. Try again once it finishes."_L1.arg(game->name()));
        return;
    }

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->settingsId());
    const auto recorded = settings.value("installedDll"_L1).toString();
    const auto createdIni = settings.value("createdIni"_L1).toString();
    auto files = settings.value("installedFiles"_L1).toStringList();

    if (!recorded.isEmpty() && !files.contains(recorded))
        files << recorded;
    if (!createdIni.isEmpty() && !files.contains(createdIni))
        files << createdIni;

    if (files.isEmpty())
    {
        for (const auto &exe : acceptableInstallCandidates(game))
        {
            const auto dir = game->windowsBinaryDir(exe);
            const auto dll = dir + "/dxgi.dll"_L1;
            const auto ini = dir + "/OptiScaler.ini"_L1;
            if (!QFileInfo::exists(dll) || !QFileInfo::exists(ini))
                continue;
            files << dll << ini;
        }
    }

    for (const auto &file : files)
    {
        if (file.isEmpty())
            continue;
        // A file the game already had is named beside it as *.kaon-bak. Put that back instead of deleting it.
        if (!restoreOrRemove(file))
        {
            failUninstall("Couldn't remove %1. Quit the game and try again."_L1.arg(QFileInfo{file}.fileName()));
            return;
        }
    }

    // Drop empty folders we created under the binary dir (e.g. D3D12_Optiscaler, Licenses).
    // Stop at the game directory. Walking further would rmdir an empty ancestor outside it.
    removeEmptyParents(files, game->installDir());

    settings.remove("installedDll"_L1);
    settings.remove("createdIni"_L1);
    settings.remove("installedFiles"_L1);
    settings.remove("backedUpFiles"_L1);
    Mod::uninstallMod(game);
}

OptiScaler::OptiScaler(QObject *parent)
    : GitHubMod{parent}
{}
