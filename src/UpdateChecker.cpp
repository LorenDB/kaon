#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QSettings>
#include <QStandardPaths>
#include <QVersionNumber>

#include "DownloadManager.h"
#include "GitHub.h"

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject{parent}
{
    QSettings settings;
    settings.beginGroup("update"_L1);
    m_enabled = settings.value("enabled"_L1, true).toBool();
    m_ignore = settings.value("ignore"_L1).toString();

    if (m_enabled)
        checkUpdates();

    connect(GitHub::instance(), &GitHub::tokenChanged, this, [this] { checkUpdates(false); });
    connect(GitHub::instance(), &GitHub::rateLimitLifted, this, [this] { checkUpdates(false); });
}

UpdateChecker *UpdateChecker::instance()
{
    static auto u = new UpdateChecker;
    return u;
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

void UpdateChecker::checkUpdates(bool announce)
{
    if (!announce && !m_enabled)
        return;

    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/kaon_releases.json"_L1;
    QNetworkRequest request{{"https://api.github.com/repos/LorenDB/kaon/releases"_L1}};
    request.setRawHeader("X-GitHub-Api-Version"_ba, "2022-11-28"_ba);

    DownloadManager::instance()->refreshCached(
        request,
        "Kaon release info"_L1,
        cache,
        [this, cache, announce](bool) {
            const auto saved = GitHub::readCachedList(cache);
            if (!saved.isArray())
            {
                if (announce)
                    emit checkFailed();
                return;
            }
            const auto releases = saved.array();
            const auto currentVersion = QVersionNumber::fromString(qApp->applicationVersion());
            for (const auto &release : releases)
            {
                const auto versionStr = release["tag_name"_L1].toString();
                const auto version = QVersionNumber::fromString(versionStr.right(versionStr.size() - 1));
                if (version > currentVersion)
                {
                    // Skipping a version only silences the check Kaon makes by itself
                    if (!announce && !m_ignore.isEmpty() && version <= QVersionNumber::fromString(m_ignore))
                        continue;

                    emit updateAvailable(version.toString(), release["html_url"_L1].toString());
                    return;
                }
            }
            if (announce)
                emit upToDate();
        },
        [this, announce](const QString &) {
            if (announce)
                emit checkFailed();
        });
}
