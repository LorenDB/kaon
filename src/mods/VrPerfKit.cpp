#include "VrPerfKit.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>

Q_LOGGING_CATEGORY(VrPerfKitLog, "vrperfkit")

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
            // The zip has a 32-bit and a 64-bit dxgi.dll. An unread machine type would get the wrong one.
            if (exe.arch != Game::Architecture::x86 && exe.arch != Game::Architecture::x64)
                continue;
            options.insert(it.key(), exe);
        }
        return options;
    }

    // Copy onto a staging name first so a failure leaves the game's current file in place.
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
} // namespace

VrPerfKit *VrPerfKit::instance()
{
    static auto kit = new VrPerfKit;
    return kit;
}

VrPerfKit *VrPerfKit::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QString VrPerfKit::info() const
{
    return "Direct3D 11 only. It hooks OpenVR and Oculus, not OpenXR. Paste the launch options so Proton loads its "
           "dxgi.dll. Fixed foveated rendering is on in vrperfkit.yml and needs an NVIDIA RTX or GTX 16-series GPU. Edit "
           "that file next to the game. See [GitHub](https://github.com/fholger/vrperfkit)."_L1;
}

QString VrPerfKit::launchOptions() const
{
    // Proton loads the proxy beside the game instead of the dxgi.dll it put in system32.
    return "WINEDLLOVERRIDES=\"dxgi=n,b\" %command%"_L1;
}

const QLoggingCategory &VrPerfKit::logger() const
{
    return VrPerfKitLog();
}

bool VrPerfKit::isInstalledForGame(const Game *game) const
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
    return std::any_of(exes.cbegin(), exes.cend(), [](const auto &exe) {
        const auto dir = QFileInfo{exe.executable}.absolutePath();
        return QFileInfo::exists(dir + "/dxgi.dll"_L1) && QFileInfo::exists(dir + "/vrperfkit.yml"_L1);
    });
}

QMap<int, Game::LaunchOption> VrPerfKit::acceptableInstallCandidates(const Game *game) const
{
    // Engine is ignored on purpose: this is a tool, not a way to play in VR. The mods page still groups by engine.
    return windowsCandidates(game);
}

bool VrPerfKit::isThisFileTheActualModDownload(const QString &file) const
{
    return file.endsWith(".zip"_L1, Qt::CaseInsensitive) && file.startsWith("vrperfkit"_L1, Qt::CaseInsensitive);
}

void VrPerfKit::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    if (!QFileInfo::exists(exe.executable))
    {
        fail("Kaon couldn't find %1. Rescan your libraries and try again."_L1.arg(exe.executable));
        return;
    }

    const auto release = currentRelease();
    if (!release || release->assets().isEmpty())
    {
        fail("VR Performance Toolkit has no download yet."_L1);
        return;
    }
    const auto asset = chooseAssetToInstall(game, exe);
    if (asset.id < 0)
    {
        fail("This version of VR Performance Toolkit has no download that fits %1."_L1.arg(game->name()));
        return;
    }
    const auto archive = pathForRelease(release, asset);
    if (!QFileInfo::exists(archive))
    {
        fail("Download VR Performance Toolkit before turning it on."_L1);
        return;
    }

    QTemporaryDir extracted;
    if (!extracted.isValid())
    {
        fail("Couldn't create a temporary folder for VR Performance Toolkit."_L1);
        return;
    }

    QProcess unzip;
    unzip.start("unzip"_L1, {"-o"_L1, "-qq"_L1, archive, "-d"_L1, extracted.path()});
    if (!unzip.waitForStarted(10000) || !unzip.waitForFinished(180000) || unzip.exitCode() != 0)
    {
        const auto detail = QString::fromLocal8Bit(unzip.readAllStandardError()).trimmed();
        fail(detail.isEmpty() ? "Couldn't extract the VR Performance Toolkit download."_L1 : detail);
        return;
    }

    // The archive also carries a README, a license, and the other architecture. Only the matching proxy and its config go
    // next to the game.
    const auto sourceDll = extracted.filePath(exe.arch == Game::Architecture::x86 ? "x86/dxgi.dll"_L1 : "dxgi.dll"_L1);
    const auto sourceYml = extracted.filePath("vrperfkit.yml"_L1);
    if (!QFileInfo::exists(sourceDll) || !QFileInfo::exists(sourceYml))
    {
        fail("This VR Performance Toolkit download is missing dxgi.dll or vrperfkit.yml."_L1);
        return;
    }

    const auto dir = QFileInfo{exe.executable}.absolutePath();
    const auto dllDest = QFileInfo{dir + "/dxgi.dll"_L1}.absoluteFilePath();
    const auto ymlDest = QFileInfo{dir + "/vrperfkit.yml"_L1}.absoluteFilePath();
    const auto exeName = QFileInfo{exe.executable}.fileName();

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->settingsId());
    const auto recorded = settings.value("installedDll"_L1).toString();
    const auto recordedPath = recorded.isEmpty() ? QString{} : QFileInfo{recorded}.absoluteFilePath();
    // A dxgi.dll with no config belongs to something else. Replacing it would break that tool.
    if (QFileInfo::exists(dllDest) && !QFileInfo::exists(ymlDest) && recordedPath != dllDest)
    {
        fail("%1 already has a dxgi.dll that isn't VR Performance Toolkit. Remove that file and try again."_L1.arg(exeName));
        return;
    }

    if (!replaceFile(sourceDll, dllDest))
    {
        fail("Couldn't place dxgi.dll next to %1. Quit the game and try again."_L1.arg(exeName));
        return;
    }

    // Keep a yml the player already edited. The shipped file turns fixed foveated rendering on.
    if (!QFileInfo::exists(ymlDest))
    {
        if (!QFile::copy(sourceYml, ymlDest))
        {
            QFile::remove(dllDest);
            fail("Couldn't copy vrperfkit.yml next to %1."_L1.arg(exeName));
            return;
        }
        settings.setValue("createdYml"_L1, ymlDest);
    }
    settings.setValue("installedDll"_L1, dllDest);

    Mod::installModImpl(game, exe);
}

void VrPerfKit::uninstallMod(Game *game)
{
    if (!game)
        return;

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->settingsId());
    const auto recorded = settings.value("installedDll"_L1).toString();
    const auto createdYml = settings.value("createdYml"_L1).toString();

    if (!recorded.isEmpty())
    {
        if (QFileInfo::exists(recorded) && !QFile::remove(recorded))
        {
            fail("Couldn't remove dxgi.dll. Quit the game and try again."_L1);
            return;
        }
        if (!createdYml.isEmpty() && QFileInfo::exists(createdYml) && !QFile::remove(createdYml))
        {
            fail("Couldn't remove vrperfkit.yml. Quit the game and try again."_L1);
            return;
        }
    }
    else
    {
        for (const auto &exe : acceptableInstallCandidates(game))
        {
            const auto dir = QFileInfo{exe.executable}.absolutePath();
            const auto dll = dir + "/dxgi.dll"_L1;
            const auto yml = dir + "/vrperfkit.yml"_L1;
            if (!QFileInfo::exists(dll) || !QFileInfo::exists(yml))
                continue;
            if (!QFile::remove(dll) || !QFile::remove(yml))
            {
                fail("Couldn't remove VR Performance Toolkit. Quit the game and try again."_L1);
                return;
            }
        }
    }

    settings.remove("installedDll"_L1);
    settings.remove("createdYml"_L1);
    Mod::uninstallMod(game);
}

VrPerfKit::VrPerfKit(QObject *parent)
    : GitHubMod{parent}
{}
