#include "BepInExConfigManager.h"

#include <algorithm>

#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>

#include "Bepinex.h"

Q_LOGGING_CATEGORY(BepinexConfigMgrLog, "bepinex-config-manager")

BepInExConfigManager *BepInExConfigManager::instance()
{
    static auto b = new BepInExConfigManager;
    return b;
}

BepInExConfigManager *BepInExConfigManager::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

const QLoggingCategory &BepInExConfigManager::logger() const
{
    return BepinexConfigMgrLog();
}

QList<Mod *> BepInExConfigManager::dependencies() const
{
    return {Bepinex::instance()};
}

bool BepInExConfigManager::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    const auto exes = acceptableInstallCandidates(game);
    return std::any_of(exes.cbegin(), exes.cend(), [this, game](const auto &exe) {
        return QFileInfo::exists(modInstallDirForGame(game, exe) + "/plugins/BepInExConfigManager.Mono.dll"_L1);
    });
}

QMap<int, Game::LaunchOption> BepInExConfigManager::acceptableInstallCandidates(const Game *game) const
{
    return Bepinex::instance()->acceptableInstallCandidates(game);
}

bool BepInExConfigManager::isThisFileTheActualModDownload(const QString &file) const
{
    return file.contains("mono"_L1, Qt::CaseInsensitive);
}

QString BepInExConfigManager::modInstallDirForGame(const Game *game, const Game::LaunchOption &executable) const
{
    return GitHubZipExtractorMod::modInstallDirForGame(game, executable) + "/BepInEx"_L1;
}

ModRelease::Asset BepInExConfigManager::chooseAssetToInstall(const Game *game, const Game::LaunchOption &exe) const
{
    // Releases ship one zip per scripting backend (e.g. BepInExConfigManager.Mono.zip). UUVR only supports Mono
    // games, so pick the Mono asset explicitly instead of relying on asset order.
    const auto assets = currentRelease()->assets();
    const auto it = std::find_if(assets.cbegin(), assets.cend(), [](const ModRelease::Asset &asset) {
        return asset.name.contains("mono"_L1, Qt::CaseInsensitive) && !asset.name.contains("il2cpp"_L1, Qt::CaseInsensitive);
    });
    if (it != assets.cend())
        return *it;
    return {};
}

BepInExConfigManager::BepInExConfigManager(QObject *parent)
    : GitHubZipExtractorMod{parent}
{}
