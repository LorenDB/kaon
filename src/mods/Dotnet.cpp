#include "Dotnet.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

#include "DownloadManager.h"
#include "Wine.h"

Q_LOGGING_CATEGORY(DotNetLog, "dotnet")

Dotnet::Dotnet(QObject *parent)
    : Mod{parent},
      m_dotnetInstallerCache{QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                             "/windowsdesktop-runtime-6.0.36-win-x64.exe"_L1}
{
    setCurrentRelease(42);
}

Dotnet *Dotnet::instance()
{
    static auto d = new Dotnet;
    return d;
}

Dotnet *Dotnet::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

const QLoggingCategory &Dotnet::logger() const
{
    return DotNetLog();
}

bool Dotnet::isInstalledForGame(const Game *game) const
{
    if (!game || !game->hasValidWine())
        return false;
    const auto basepath = game->winePrefix() + "/drive_c/Program Files/dotnet"_L1;
    return QFileInfo::exists(basepath + "/dotnet.exe"_L1) && QFileInfo::exists(basepath + "/host/fxr/6.0.36"_L1);
}

bool Dotnet::hasDotnetCached() const
{
    return QFileInfo::exists(m_dotnetInstallerCache);
}

void Dotnet::downloadRelease(ModRelease *)
{
    QUrl url{
        "https://builds.dotnet.microsoft.com/dotnet/WindowsDesktop/6.0.36/windowsdesktop-runtime-6.0.36-win-x64.exe"_L1};

    DownloadManager::instance()->download(
        QNetworkRequest{url},
        ".NET Desktop Runtime 6.0.36"_L1,
        true,
        [this](const QByteArray &data) {
            QFile file{m_dotnetInstallerCache};
            if (file.open(QIODevice::WriteOnly))
            {
                file.write(data);
                file.close();
            }
            else
                qCWarning(DotNetLog) << "Failed to save downloaded .NET desktop runtime";
        },
        [this](const QNetworkReply::NetworkError error, const QString &errorMessage) {
            qCWarning(DotNetLog) << ".NET desktop runtime download failed:" << errorMessage;
        },
        [this] { releases().first()->setDownloaded(hasDotnetCached()); });
}

void Dotnet::deleteRelease(ModRelease *release)
{
    if (!release->downloaded())
        return;
    if (QFile{m_dotnetInstallerCache}.remove())
        release->setDownloaded(false);
}

void Dotnet::installMod(Game *game)
{
    if (!hasDotnetCached())
        return;

    Wine::instance()->runInWine(
        ".NET Desktop Runtime installer"_L1, game, m_dotnetInstallerCache, {}, [this, game] { Mod::installMod(game); });
}

void Dotnet::uninstallMod(Game *game)
{
    // Installer and uninstaller are the same
    installMod(game);
}

QMap<int, Game::LaunchOption> Dotnet::acceptableInstallCandidates(const Game *game) const
{
    auto options = Mod::acceptableInstallCandidates(game);
    options.removeIf([this, game](const std::pair<int, Game::LaunchOption> &exe) {
        return exe.second.platform != Game::LaunchOption::Platform::Windows;
    });
    return options;
}

QList<ModRelease *> Dotnet::releases() const
{
    static QList<ModRelease *> l = {new ModRelease{
        42,
        ".NET Desktop Runtime 6.0.36"_L1,
        QDateTime{{2024, 11, 12}, {0, 0, 0}},
        false,
        hasDotnetCached(),
        {"https://builds.dotnet.microsoft.com/dotnet/WindowsDesktop/6.0.36/windowsdesktop-runtime-6.0.36-win-x64.exe"_L1}}};
    return l;
}
