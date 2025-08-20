#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QVersionNumber>

#include "DownloadManager.h"

using namespace Qt::Literals;

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject{parent}
{
    DownloadManager::instance()->download(
                QNetworkRequest{{"https://api.github.com/repos/LorenDB/kaon/releases"_L1}},
                "Kaon release info"_L1,
                false,
                [this](QByteArray data) {
        auto releases = QJsonDocument::fromJson(data).array();
        const auto currentVersion = QVersionNumber::fromString(qApp->applicationVersion());
        for (const auto &release : std::as_const(releases))
        {
            const auto versionStr = release["tag_name"_L1].toString();
            const auto version = QVersionNumber::fromString(versionStr.right(versionStr.size() - 1));
            if (version > currentVersion)
            {
                emit updateAvailable(version.toString(), release["html_url"_L1].toString());
                return;
            }
        }
    },
    [](QNetworkReply::NetworkError error, QString errorMessage) {});
}
