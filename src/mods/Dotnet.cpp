#include "Dotnet.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

#include "DownloadManager.h"
#include "Wine.h"

using namespace Qt::Literals;

Q_LOGGING_CATEGORY(DotNetLog, "dotnet")

Dotnet *Dotnet::s_instance = nullptr;

Dotnet::Dotnet(QObject *parent)
    : Mod{parent},
      m_dotnetInstallerCache{QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                             "/windowsdesktop-runtime-6.0.36-win-x64.exe"_L1}
{
    if (s_instance != nullptr)
        throw std::runtime_error{"Attempted to double initialize .NET"};
    else
        s_instance = this;
}

Dotnet *Dotnet::instance()
{
    return s_instance;
}

bool Dotnet::checkGameCompatibility(const Game *game) const
{
    if (game->noWindowsSupport())
        return false;
    return Mod::checkGameCompatibility(game);
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

bool Dotnet::dotnetDownloadInProgress() const
{
    return m_dotnetDownloadInProgress;
}

void Dotnet::downloadDotnetDesktopRuntime(Game *game)
{
    if (game)
    {
        connect(
            this,
            &Dotnet::hasDotnetCachedChanged,
            this,
            [this, game](bool isCached) {
                if (isCached)
                    installDotnetDesktopRuntime(game);
            },
            Qt::SingleShotConnection);
    }

    QUrl url{
        "https://builds.dotnet.microsoft.com/dotnet/WindowsDesktop/6.0.36/windowsdesktop-runtime-6.0.36-win-x64.exe"_L1};

    m_dotnetDownloadInProgress = true;
    emit dotnetDownloadInProgressChanged(true);
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
            emit dotnetDownloadFailed();
        },
        [this] {
            m_dotnetDownloadInProgress = false;
            emit dotnetDownloadInProgressChanged(false);
            emit hasDotnetCachedChanged(hasDotnetCached());
        });
}

QList<ModRelease *> Dotnet::releases() const
{
    static ModRelease *m =
            new ModRelease{0, ".NET Desktop Runtime 6.0.36"_L1, QDateTime{{2024, 11, 12}, {0, 0, 0}}, false, false, {}};
    return {m};
}

void Dotnet::installDotnetDesktopRuntime(Game *game)
{
    if (!hasDotnetCached())
    {
        emit promptDotnetDownload(game);
        return;
    }

    Wine::instance()->runInWine(".NET Desktop Runtime installer"_L1, game, {m_dotnetInstallerCache}, {}, [game] {
        if (game)
            emit game->dotnetInstalledChanged();
    });
}
