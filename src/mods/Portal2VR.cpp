#include "Portal2VR.h"

#include <QFileInfo>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(P2VRLog, "portal2vr")

Portal2VR *Portal2VR::instance()
{
    static Portal2VR p2vr;
    return &p2vr;
}

Portal2VR *Portal2VR::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QString Portal2VR::info() const
{
    return "See [GitHub](https://github.com/Gistix/portal2vr?tab=readme-ov-file#how-to-use) for required Portal 2 launch options"_L1;
}

const QLoggingCategory &Portal2VR::logger() const
{
    return P2VRLog();
}

bool Portal2VR::checkGameCompatibility(const Game *game) const
{
    return game->store() == Game::Store::Steam && (game->id() == "620"_L1 || game->id() == "317400"_L1);
}

bool Portal2VR::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    return QFileInfo::exists(game->installDir() + "/bin/openvr_api.dll"_L1);
}

void Portal2VR::installMod(Game *game)
{
    GitHubZipExtractorMod::installMod(game);

    // Portal Stories: Mel needs special configuration to work
    if (game->id() == "317400"_L1)
    {
        // Applying fixes shown here: https://steamcommunity.com/sharedfiles/filedetails/?id=3037963726
        QFile config{game->installDir() + "/VR/config.txt"_L1};
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

bool Portal2VR::isThisFileTheActualModDownload(const QString &file) const
{
    return file.startsWith("Portal2VR"_L1) && file.endsWith(".zip"_L1) && !file.contains("debug"_L1, Qt::CaseInsensitive);
}

QString Portal2VR::modInstallDirForGame(Game *game) const
{
    return game->installDir();
}

Portal2VR::Portal2VR(QObject *parent)
    : GitHubZipExtractorMod{parent}
{}
