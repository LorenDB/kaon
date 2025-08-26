#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QVersionNumber>

#include "DownloadManager.h"

using namespace Qt::Literals;

UpdateChecker *UpdateChecker::s_instance = nullptr;

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject{parent}
{
    QSettings settings;
    settings.beginGroup("update"_L1);
    m_enabled = settings.value("enabled"_L1, true).toBool();
    m_ignore = settings.value("ignore"_L1).toString();

    if (m_enabled)
        checkUpdates();
}

UpdateChecker *UpdateChecker::instance()
{
    if (!s_instance)
        s_instance = new UpdateChecker;
    return s_instance;
}

UpdateChecker *UpdateChecker::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

void UpdateChecker::setEnabled(bool state)
{
    m_enabled = state;
    emit enabledChanged(state);

    QSettings settings;
    settings.beginGroup("update"_L1);
    settings.setValue("enabled"_L1, state);
}

void UpdateChecker::setIgnore(const QString &ignore)
{
    m_ignore = ignore;
    emit ignoreChanged(ignore);

    QSettings settings;
    settings.beginGroup("update"_L1);
    settings.setValue("ignore"_L1, ignore);
}

void UpdateChecker::checkUpdates()
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
                    if (!m_ignore.isEmpty() && version <= QVersionNumber::fromString(m_ignore))
                        continue;

                    emit updateAvailable(version.toString(), release["html_url"_L1].toString());
                    return;
                }
            }
        },
        [](QNetworkReply::NetworkError error, QString errorMessage) {});
}
