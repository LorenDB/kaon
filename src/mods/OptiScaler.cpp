#include "OptiScaler.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSettings>
#include <QTemporaryDir>

#include "Archive.h"

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

QList<Game::LaunchOption> OptiScaler::preferredInstallCandidates(const Game *game, const QList<Game::LaunchOption> &all) const
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

    QTemporaryDir extracted;
    if (!extracted.isValid())
    {
        fail("Couldn't create a temporary folder for OptiScaler."_L1);
        return;
    }
    if (QString error; !Archive::extract(archive, extracted.path(), &error))
    {
        fail(error);
        return;
    }

    const auto sourceDll = extracted.filePath("OptiScaler.dll"_L1);
    if (!QFileInfo::exists(sourceDll))
    {
        fail("This OptiScaler download is missing OptiScaler.dll."_L1);
        return;
    }

    const auto dir = game->windowsBinaryDir(exe);
    const auto dllDest = QFileInfo{dir + "/dxgi.dll"_L1}.absoluteFilePath();
    const auto iniDest = QFileInfo{dir + "/OptiScaler.ini"_L1}.absoluteFilePath();
    const auto exeName = QFileInfo{exe.executable}.fileName();

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->settingsId());
    const auto recorded = settings.value("installedDll"_L1).toString();
    const auto recordedPath = recorded.isEmpty() ? QString{} : QFileInfo{recorded}.absoluteFilePath();

    // VR Performance Toolkit also installs a dxgi.dll. They cannot share the slot.
    if (QFileInfo::exists(dir + "/vrperfkit.yml"_L1) && QFileInfo::exists(dllDest) && recordedPath != dllDest)
    {
        fail("%1 already has VR Performance Toolkit (also a dxgi.dll). Turn that off for this game first."_L1.arg(exeName));
        return;
    }
    if (QFileInfo::exists(dllDest) && !QFileInfo::exists(iniDest) && recordedPath != dllDest)
    {
        fail("%1 already has a dxgi.dll that isn't OptiScaler. Remove that file and try again."_L1.arg(exeName));
        return;
    }

    QStringList written;
    QString error;
    if (!copyTree(extracted.path(), dir, &written, &error))
    {
        fail(error);
        return;
    }

    const bool createdIni = !QFileInfo::exists(iniDest);
    if (!replaceFile(sourceDll, dllDest))
    {
        fail("Couldn't place dxgi.dll next to %1. Quit the game and try again."_L1.arg(exeName));
        return;
    }
    written << dllDest;
    if (createdIni && QFileInfo::exists(iniDest))
        settings.setValue("createdIni"_L1, iniDest);

    settings.setValue("installedDll"_L1, dllDest);
    settings.setValue("installedFiles"_L1, written);

    Mod::installModImpl(game, exe);
}

void OptiScaler::uninstallMod(Game *game)
{
    if (!game)
        return;

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

    // Drop empty folders we created under the binary dir (e.g. D3D12_Optiscaler, Licenses)
    QStringList dirs;
    for (const auto &file : files)
    {
        for (auto dir = QFileInfo{file}.absolutePath(); !dir.isEmpty() && !dirs.contains(dir);
             dir = QFileInfo{dir}.absolutePath())
            dirs << dir;
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
