#include "OptiScaler.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QPointer>
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

    bool replaceFile(const QString &from, const QString &to)
    {
        const auto staging = to + ".kaon-new"_L1;
        QFile::remove(staging);
        if (!QFile::copy(from, staging))
            return false;
        if (QFileInfo::exists(to) && !QFile::remove(to))
        {
            QFile::remove(staging);
            return false;
        }
        if (!QFile::rename(staging, to))
        {
            QFile::remove(staging);
            return false;
        }
        return true;
    }

    // Copy a tree from extracted/ into dest/, recording every file written. Skips OptiScaler.dll (placed as dxgi.dll
    // separately) and leaves an existing OptiScaler.ini alone.
    bool copyTree(const QString &fromDir, const QString &toDir, QStringList *written, QString *error)
    {
        QDir source{fromDir};
        if (!QDir{}.mkpath(toDir))
        {
            *error = "Couldn't create %1."_L1.arg(toDir);
            return false;
        }

        for (const auto &entry : source.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            if (!copyTree(entry.absoluteFilePath(), toDir + '/' + entry.fileName(), written, error))
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
            if (!replaceFile(entry.absoluteFilePath(), dest))
            {
                *error = "Couldn't place %1. Quit the game and try again."_L1.arg(name);
                return false;
            }
            *written << QFileInfo{dest}.absoluteFilePath();
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
    const auto gameId = game->id();
    const auto releaseId = release->id();
    const auto releaseName = release->name();
    QSettings snapshot;
    snapshot.beginGroup(settingsGroup());
    snapshot.beginGroup(settingsId);
    const auto snapshotRecorded = snapshot.value("installedDll"_L1).toString();
    const auto recordedPath = snapshotRecorded.isEmpty() ? QString{} : QFileInfo{snapshotRecorded}.absoluteFilePath();
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
                             settingsId,
                             gameName,
                             store,
                             gameId,
                             releaseId,
                             releaseName] {
        const auto done =
            [this, guard, busyKey, store, gameId, settingsId, gameName, releaseId, releaseName, exe, dllDest, iniDest](
                bool ok, const QString &error, const QStringList &written, bool createdIni) {
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
                                      createdIni);
                    },
                    Qt::QueuedConnection);
            };

        QTemporaryDir extracted;
        if (!extracted.isValid())
        {
            done(false, "Couldn't create a temporary folder for OptiScaler."_L1, {}, false);
            return;
        }
        if (QString error; !Archive::extract(archive, extracted.path(), &error))
        {
            done(false, error, {}, false);
            return;
        }

        const auto sourceDll = extracted.filePath("OptiScaler.dll"_L1);
        if (!QFileInfo::exists(sourceDll))
        {
            done(false, "This OptiScaler download is missing OptiScaler.dll."_L1, {}, false);
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
                 false);
            return;
        }
        if (QFileInfo::exists(dllDest) && !QFileInfo::exists(iniDest) && recordedPath != dllDest)
        {
            done(false,
                 "%1 already has a dxgi.dll that isn't OptiScaler. Remove that file and try again."_L1.arg(exeName),
                 {},
                 false);
            return;
        }

        QStringList written;
        // copyTree records files as it goes. A later failure has to take those back, or they sit
        // in the game with nothing in settings for uninstall to find.
        const auto discardWritten = [&written, &dir]() {
            QStringList parents;
            for (const auto &file : written)
            {
                QFile::remove(file);
                const auto parent = QFileInfo{file}.absolutePath();
                if (parent != dir && parent.startsWith(dir + '/'_L1) && !parents.contains(parent))
                    parents << parent;
            }
            std::sort(parents.begin(), parents.end(), [](const auto &left, const auto &right) {
                return left.count('/'_L1) > right.count('/'_L1);
            });
            for (const auto &parent : std::as_const(parents))
                if (QDir parentDir{parent}; parentDir.exists() && parentDir.isEmpty())
                    parentDir.rmdir(parent);
            written.clear();
        };

        QString error;
        if (!copyTree(extracted.path(), dir, &written, &error))
        {
            discardWritten();
            done(false, error, {}, false);
            return;
        }

        const bool createdIni = !QFileInfo::exists(iniDest);
        if (!replaceFile(sourceDll, dllDest))
        {
            discardWritten();
            done(false, "Couldn't place dxgi.dll next to %1. Quit the game and try again."_L1.arg(exeName), {}, false);
            return;
        }
        written << dllDest;
        done(true, {}, written, createdIni && QFileInfo::exists(iniDest));
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
        if (file.isEmpty() || !QFileInfo::exists(file))
            continue;
        if (!QFile::remove(file))
        {
            failUninstall("Couldn't remove %1. Quit the game and try again."_L1.arg(QFileInfo{file}.fileName()));
            return;
        }
    }

    // Drop empty folders we created under the binary dir (e.g. D3D12_Optiscaler, Licenses).
    // Stop at the game directory. Walking further would rmdir an empty ancestor outside it.
    QStringList dirs;
    const auto gameDir = QDir::cleanPath(game->installDir());
    for (const auto &file : files)
    {
        for (auto parent = QFileInfo{file}.absolutePath();
             !gameDir.isEmpty() && parent.startsWith(gameDir + '/'_L1) && !dirs.contains(parent);
             parent = QFileInfo{parent}.absolutePath())
            dirs << parent;
    }
    std::sort(dirs.begin(), dirs.end(), [](const auto &l, const auto &r) { return l.count('/') > r.count('/'); });
    for (const auto &dir : std::as_const(dirs))
        if (QDir d{dir}; d.exists() && d.isEmpty())
            d.rmdir(dir);

    settings.remove("installedDll"_L1);
    settings.remove("createdIni"_L1);
    settings.remove("installedFiles"_L1);
    Mod::uninstallMod(game);
}

OptiScaler::OptiScaler(QObject *parent)
    : GitHubMod{parent}
{}
