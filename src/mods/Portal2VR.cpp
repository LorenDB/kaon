#include "Portal2VR.h"

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>

Q_LOGGING_CATEGORY(P2VRLog, "portal2vr")

namespace
{
    // The player's VR settings with any setting a newer release added. Unpacking a release over an install would
    // otherwise put every setting back to its default.
    QByteArray keepSettings(const QByteArray &previous, const QByteArray &shipped)
    {
        static const QRegularExpression setting{R"(^([A-Za-z0-9_]+)=)"_L1};
        const auto text = QString::fromUtf8(previous);

        QByteArray missing;
        const QByteArray newline = previous.contains("\r\n") ? "\r\n"_ba : "\n"_ba;
        for (auto line : QString::fromUtf8(shipped).split('\n'_L1))
        {
            if (line.endsWith('\r'_L1))
                line.chop(1);
            const auto match = setting.match(line);
            if (!match.hasMatch())
                continue;
            // The mod reads names with their exact capitalization
            const QRegularExpression present{"(?m)^"_L1 + QRegularExpression::escape(match.captured(1)) + '='_L1};
            if (!present.match(text).hasMatch())
                missing += line.toUtf8() + newline;
        }

        auto merged = previous;
        if (!missing.isEmpty() && !merged.endsWith('\n'))
            merged += newline;
        return merged + missing;
    }
} // namespace

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
    return "Start SteamVR before the game. It needs launch options set in Steam, which make Proton load the mod's "
           "d3d9.dll. See [GitHub](https://github.com/Gistix/portal2vr?tab=readme-ov-file#how-to-use)."_L1;
}

QString Portal2VR::launchOptions() const
{
    // The shipped d3d9.dll is a patched DXVK build, so Proton has to load that file instead of its own. Remaining
    // flags are the mod's required settings from
    // https://github.com/Gistix/portal2vr?tab=readme-ov-file#how-to-use
    return "WINEDLLOVERRIDES=\"d3d9=n,b\" %command% -insecure -window -novid +mat_motion_blur_percent_of_screen_max 0 "
           "+mat_queue_mode 0 +mat_vsync 0 +mat_antialias 0 +mat_grain_scale_override 0 -width 1280 -height 720"_L1;
}

QStringList Portal2VR::conflictingLaunchOptions() const
{
    // -vulkan makes the game load the DXVK it ships with (dxvk_d3d9.dll) and never open the mod's d3d9.dll. The mod
    // also wants a window, not fullscreen.
    return {"-vulkan"_L1, "-fullscreen"_L1, "-full"_L1};
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
    const auto configPath = modInstallDirForGame(game, exe) + "/VR/config.txt"_L1;
    QByteArray settingsBefore;
    if (QFile config{configPath}; config.open(QIODevice::ReadOnly))
        settingsBefore = config.readAll();

    if (!unpackInto(game, exe))
        return;

    if (!settingsBefore.isEmpty())
    {
        QFile config{configPath};
        const auto shipped = config.open(QIODevice::ReadOnly) ? config.readAll() : QByteArray{};
        config.close();
        if (QSaveFile out{configPath};
            !out.open(QIODevice::WriteOnly) || out.write(keepSettings(settingsBefore, shipped)) < 0 || !out.commit())
            qCWarning(P2VRLog) << "Could not put the VR settings back in" << configPath;
    }

    // Portal Stories: Mel needs special configuration to work
    if (game->id() == "317400"_L1)
    {
        // Applying fixes shown here: https://steamcommunity.com/sharedfiles/filedetails/?id=3037963726
        QFile config{configPath};
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

    Mod::installModImpl(game, exe);
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
