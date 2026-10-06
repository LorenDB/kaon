#include "Portal2VR.h"

#include <QFileInfo>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(P2VRLog, "portal2vr")

Portal2VR *Portal2VR::instance()
{
    static auto p = new Portal2VR;
    return p;
}

Portal2VR *Portal2VR::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QString Portal2VR::info() const
{
    return "Start SteamVR first. Paste the launch options below into Steam so Proton loads this mod's d3d9.dll. "
           "See [GitHub](https://github.com/Gistix/portal2vr?tab=readme-ov-file#how-to-use)."_L1;
}

QString Portal2VR::launchOptions() const
{
    // The shipped d3d9.dll is a patched DXVK build, so Proton has to load that file instead of its own. Remaining
    // flags are the mod's required settings from
    // https://github.com/Gistix/portal2vr?tab=readme-ov-file#how-to-use
    return "WINEDLLOVERRIDES=\"d3d9=n,b\" %command% -insecure -window -novid +mat_motion_blur_percent_of_screen_max 0 "
           "+mat_queue_mode 0 +mat_vsync 0 +mat_antialias 0 +mat_grain_scale_override 0 -width 1280 -height 720"_L1;
}

const QLoggingCategory &Portal2VR::logger() const
{
    return P2VRLog();
}

bool Portal2VR::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    const auto exes = acceptableInstallCandidates(game);
    return std::any_of(exes.cbegin(), exes.cend(), [this, game](const auto &exe) {
        return QFileInfo::exists(modInstallDirForGame(game, exe) + "/bin/openvr_api.dll"_L1);
    });
}

void Portal2VR::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    GitHubZipExtractorMod::installModImpl(game, exe);

    // Portal Stories: Mel needs special configuration to work
    if (game->id() == "317400"_L1)
    {
        // Applying fixes shown here: https://steamcommunity.com/sharedfiles/filedetails/?id=3037963726
        QFile config{modInstallDirForGame(game, exe) + "/VR/config.txt"_L1};
        if (config.open(QIODevice::ReadOnly))
        {
            QStringList lines;
            while (!config.atEnd())
                lines << config.readLine().trimmed();
            config.close();

            static const QMap<QString, QString> replacements{
                {"ViewmodelPosCustomOffsetX=0.0", "ViewmodelPosCustomOffsetX=12"},
                {"ViewmodelPosCustomOffsetY=0.0", "ViewmodelPosCustomOffsetY=10"},
                {"ViewmodelPosCustomOffsetZ=0.0", "ViewmodelPosCustomOffsetZ=-10"},
                {"ViewmodelAngCustomOffsetX=0.0", "ViewmodelAngCustomOffsetX=-10"},
                {"ViewmodelAngCustomOffsetY=0.0", "ViewmodelAngCustomOffsetY=-18"},
                {"ViewmodelAngCustomOffsetZ=0.0", "ViewmodelAngCustomOffsetZ=0.0"},
            };

            if (config.open(QIODevice::WriteOnly))
            {
                QTextStream out{&config};
                for (const auto &line : std::as_const(lines))
                {
                    if (replacements.contains(line))
                        out << replacements[line] << '\n';
                    else
                        out << line + '\n';
                }
                config.close();
            }
        }
    }
}

QMap<int, Game::LaunchOption> Portal2VR::acceptableInstallCandidates(const Game *game) const
{
    if (game->store() != Game::Store::Steam || (game->id() != "620"_L1 && game->id() != "317400"_L1))
        return {};

    auto options = GitHubZipExtractorMod::acceptableInstallCandidates(game);
    options.removeIf([this, game](const std::pair<int, Game::LaunchOption> &exe) {
        return exe.second.platform != Game::Platform::Windows;
    });
    return options;
}

bool Portal2VR::isThisFileTheActualModDownload(const QString &file) const
{
    return file.startsWith("Portal2VR"_L1) && file.endsWith(".zip"_L1) && !file.contains("debug"_L1, Qt::CaseInsensitive);
}

Portal2VR::Portal2VR(QObject *parent)
    : GitHubZipExtractorMod{parent}
{}
