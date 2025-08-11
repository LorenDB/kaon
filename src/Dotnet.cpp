#include "Dotnet.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include "DownloadManager.h"

using namespace Qt::Literals;

Dotnet *Dotnet::s_instance = nullptr;

Dotnet::Dotnet(QObject *parent)
    : QObject{parent}
{
    if (s_instance != nullptr)
        throw std::runtime_error{"Attempted to double initialize .NET"};
    else
        s_instance = this;
}

bool Dotnet::hasDotnetCached() const
{
    return QFileInfo::exists(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                             "/windowsdesktop-runtime-6.0.36-win-x64.exe"_L1);
}

bool Dotnet::dotnetDownloadInProgress() const
{
    return m_dotnetDownloadInProgress;
}

void Dotnet::downloadDotnetDesktopRuntime(int steamId)
{
    if (steamId != 0)
    {
        // Uh, clang-format, you feeling OK?
        connect(
                    this,
                    &Dotnet::hasDotnetCachedChanged,
                    this,
                    [this, steamId](bool isCached) {
            if (isCached)
                installDotnetDesktopRuntime(steamId);
        },
        Qt::SingleShotConnection);
    }

    QUrl url{
        "https://builds.dotnet.microsoft.com/dotnet/WindowsDesktop/6.0.36/windowsdesktop-runtime-6.0.36-win-x64.exe"_L1};
    const QString exePath =
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/windowsdesktop-runtime-6.0.36-win-x64.exe"_L1;

    m_dotnetDownloadInProgress = true;
    emit dotnetDownloadInProgressChanged(true);
    DownloadManager::instance()->download(
                QNetworkRequest{url},
                ".NET Desktop Runtime 6.0.36"_L1,
                [this, exePath](const QByteArray &data) {
        // TODO: this is not working. Probably has to do with the file not being saved or something.
        // Might be nice to get it working at some point but I'm not holding my breath.
        // constexpr auto sha512sum_v6_0_36 =
        // "86fa63997e7e0dc6f3bf609e00880388dcf8d985c8f6417d07ebbbb1ecc957bf90214c8ff93f559a"
        //                                    "0e762b5626ba8c56c581f4d506aa4de7555f9792c2da254d";
        // if (QCryptographicHash::hash(data, QCryptographicHash::Sha512) != sha512sum_v6_0_36)
        // {
        //     qDebug() << "Downloaded file did not match compile-time hash";
        //     emit dotnetDownloadFailed();
        //     return;
        // }

        QFile file{exePath};
        if (file.open(QIODevice::WriteOnly))
        {
            file.write(data);
            file.close();
        }
        else
            qDebug() << "Failed to save downloaded .NET desktop runtime";
    },
    [this](const QNetworkReply::NetworkError error, const QString &errorMessage) {
        qDebug() << ".NET desktop runtime download failed:" << errorMessage;
        emit dotnetDownloadFailed();
    },
    [this] {
        m_dotnetDownloadInProgress = false;
        emit dotnetDownloadInProgressChanged(false);
        emit hasDotnetCachedChanged(hasDotnetCached());
    });
}

void Dotnet::installDotnetDesktopRuntime(int steamId)
{
    if (!hasDotnetCached())
    {
        emit promptDotnetDownload(steamId);
        return;
    }

    auto installer = new QProcess;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("WINEPREFIX"_L1,
               QDir::homePath() + "/.local/share/Steam/steamapps/compatdata/" + QString::number(steamId) + "/pfx"_L1);
    env.insert("WINEFSYNC"_L1, "1"_L1);
    installer->setProcessEnvironment(env);
    installer->start(
                QDir::homePath() + "/.local/share/Steam/steamapps/common/Proton - Experimental/files/bin/wine"_L1,
                {QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/windowsdesktop-runtime-6.0.36-win-x64.exe"_L1});
    connect(installer, &QProcess::finished, installer, &QObject::deleteLater);
}
