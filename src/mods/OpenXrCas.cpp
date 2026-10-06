#include "OpenXrCas.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

#include "Archive.h"
#include "WinePrefix.h"

Q_LOGGING_CATEGORY(OpenXrCasLog, "openxr.cas")

namespace
{
    // Where the OpenXR loader looks for API layers that load by themselves. XR_API_LAYER_PATH does not enable them.
    const auto layerKey = "Software\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit"_L1;
    // The value is named after the manifest and holds 0 for "enabled"
    const auto windowsJson = "C:\\Kaon\\OpenXR-CAS\\openxr-api-layer.json"_L1;

    QString layerDirectory(const Game *game)
    {
        return QDir{game->winePrefix()}.filePath("drive_c/Kaon/OpenXR-CAS"_L1);
    }

    bool layerFilesReady(const Game *game)
    {
        const auto dir = layerDirectory(game);
        return QFileInfo::exists(dir + "/XR_APILAYER_OPENXR_SHARPENER.dll"_L1) &&
               QFileInfo::exists(dir + "/openxr-api-layer.json"_L1) && QFileInfo::exists(dir + "/shaders/CAS.hlsl"_L1);
    }

    // The layer goes into HKEY_LOCAL_MACHINE, where its own installer puts it. The loader skips the per-user list for
    // any process that runs with full rights, and under Wine every process does.
    bool registered(const Game *game)
    {
        return WinePrefix::value(game, WinePrefix::Hive::Machine, layerKey, windowsJson) == "0"_L1;
    }

    void removeLayerFiles(const Game *game)
    {
        QDir{layerDirectory(game)}.removeRecursively();
        QDir drive{QDir{game->winePrefix()}.filePath("drive_c"_L1)};
        if (QDir{drive.filePath("Kaon"_L1)}.isEmpty())
            drive.rmdir("Kaon"_L1);
    }

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
            // The release is 64-bit only.
            if (exe.arch != Game::Architecture::x64)
                continue;
            options.insert(it.key(), exe);
        }
        return options;
    }
} // namespace

OpenXrCas *OpenXrCas::instance()
{
    static auto layer = new OpenXrCas;
    return layer;
}

OpenXrCas *OpenXrCas::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QString OpenXrCas::info() const
{
    return "Each game gets its own copy in that game's Proton prefix. Direct3D 11 OpenXR only. Quit the game before "
           "turning it on or off. Sharpness is set in AppData\\Local\\XR_APILAYER_OPENXR_SHARPENER\\config.cfg in "
           "the prefix. See [GitHub](https://github.com/elliotttate/OpenXR-CAS)."_L1;
}

const QLoggingCategory &OpenXrCas::logger() const
{
    return OpenXrCasLog();
}

bool OpenXrCas::isInstalledForGame(const Game *game) const
{
    if (!game || game->winePrefix().isEmpty())
        return false;
    return layerFilesReady(game) && registered(game);
}

QString OpenXrCas::installHoldReason(const Game *game) const
{
    if (!game)
        return {};
    return WinePrefix::editHoldReason(game);
}

QMap<int, Game::LaunchOption> OpenXrCas::acceptableInstallCandidates(const Game *game) const
{
    return windowsCandidates(game);
}

bool OpenXrCas::isThisFileTheActualModDownload(const QString &file) const
{
    return file.endsWith(".zip"_L1, Qt::CaseInsensitive) && file.contains("win64"_L1, Qt::CaseInsensitive) &&
           !file.contains("pdb"_L1, Qt::CaseInsensitive) && !file.contains("symbol"_L1, Qt::CaseInsensitive);
}

void OpenXrCas::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    if (const auto hold = installHoldReason(game); !hold.isEmpty())
    {
        fail(hold);
        return;
    }

    const auto release = currentRelease();
    if (!release || release->assets().isEmpty())
    {
        fail("OpenXR CAS has no download yet."_L1);
        return;
    }
    const auto asset = chooseAssetToInstall(game, exe);
    if (asset.id < 0)
    {
        fail("This version of OpenXR CAS has no Windows x64 download."_L1);
        return;
    }
    const auto archive = pathForRelease(release, asset);
    if (!QFileInfo::exists(archive))
    {
        fail("Download OpenXR CAS before turning it on."_L1);
        return;
    }

    // Leave config.cfg alone when it's already there. The zip has no config; the code default is sharpness 0.6.
    QString error;
    if (!Archive::extract(archive,
                          layerDirectory(game),
                          {"XR_APILAYER_OPENXR_SHARPENER.dll"_L1, "openxr-api-layer.json"_L1, "shaders/*"_L1},
                          &error))
    {
        fail(error);
        return;
    }
    if (!layerFilesReady(game))
    {
        fail("The OpenXR CAS download is missing the layer or its CAS shader."_L1);
        return;
    }

    if (!WinePrefix::setDword(game, WinePrefix::Hive::Machine, layerKey, windowsJson, 0, &error))
    {
        // The files stay. Without the registry value the layer is inert, and the next try overwrites them.
        fail(error);
        return;
    }
    // Kaon's first version of this put the layer in the per-user list, where the loader never read it
    WinePrefix::remove(game, WinePrefix::Hive::User, layerKey, windowsJson, &error);

    Mod::installModImpl(game, exe);
}

void OpenXrCas::uninstallMod(Game *game)
{
    if (!game)
        return;

    if (WinePrefix::isSetUp(game))
    {
        // A live wineserver would rewrite the registry on its way out and put the layer back
        QString error;
        if (!WinePrefix::remove(game, WinePrefix::Hive::Machine, layerKey, windowsJson, &error) ||
            !WinePrefix::remove(game, WinePrefix::Hive::User, layerKey, windowsJson, &error))
        {
            failUninstall(error);
            return;
        }
    }

    if (!game->winePrefix().isEmpty())
        removeLayerFiles(game);
    Mod::uninstallMod(game);
}

OpenXrCas::OpenXrCas(QObject *parent)
    : GitHubMod{parent}
{}
