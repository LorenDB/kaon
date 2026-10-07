#include "UEVRAFW.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#include "Aptabase.h"
#include "Archive.h"
#include "Dotnet.h"
#include "DownloadManager.h"
#include "Wine.h"

Q_LOGGING_CATEGORY(UEVRAFWLog, "uevr.afw")

namespace
{
    // PureDark's AFW build adds Alternate Frame Warping as rendering method index 3
    // (Native Stereo=0, Synchronized Sequential=1, Alternating/AFR=2).
    const auto renderingMethodAfw = "3"_L1;
    const auto ghostingFixKey = "VR_GhostingFix"_L1;
    const auto renderingMethodKey = "VR_RenderingMethod"_L1;
    // Joeyhodge builds expose "Bootstrap Separate View States" as this toggle.
    const auto bootstrapKey = "VR_GhostingFixBootstrapViewStates"_L1;

    QString exeBaseName(const Game *game)
    {
        if (!game)
            return {};
        for (const auto &exe : game->executables())
        {
            if (exe.platform != Game::Platform::Windows || !QFileInfo::exists(exe.executable))
                continue;
            return QFileInfo{exe.executable}.completeBaseName();
        }
        return {};
    }

    // UEVR writes %APPDATA%/UnrealVRMod/<exe>/config.txt. Under Proton that is usually
    // drive_c/users/steamuser/AppData/Roaming; custom prefixes may use another Windows user.
    QStringList candidateConfigPaths(const Game *game)
    {
        QStringList paths;
        if (!game || game->winePrefix().isEmpty())
            return paths;
        const auto exe = exeBaseName(game);
        if (exe.isEmpty())
            return paths;

        const QDir users{QDir{game->winePrefix()}.filePath("drive_c/users"_L1)};
        // Prefer steamuser, then every other profile that already has UnrealVRMod.
        QStringList names;
        if (users.exists("steamuser"_L1))
            names << "steamuser"_L1;
        for (const auto &entry : users.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (!names.contains(entry))
                names << entry;

        for (const auto &name : names)
        {
            paths << users.filePath(name + "/AppData/Roaming/UnrealVRMod/"_L1 + exe + "/config.txt"_L1);
        }
        return paths;
    }

    QHash<QString, QString> readConfigValues(const QString &path)
    {
        QHash<QString, QString> values;
        QFile file{path};
        if (!file.open(QFile::ReadOnly))
            return values;
        const auto text = QString::fromUtf8(file.readAll());
        static const QRegularExpression line{R"(^\s*([A-Za-z0-9_]+)\s*=\s*(.*?)\s*$)"_L1};
        for (auto raw : text.split('\n'_L1))
        {
            if (raw.endsWith('\r'_L1))
                raw.chop(1);
            const auto match = line.match(raw);
            if (match.hasMatch())
                values.insert(match.captured(1), match.captured(2));
        }
        return values;
    }

    bool truthy(const QString &value)
    {
        const auto lower = value.trimmed().toLower();
        return lower == "true"_L1 || lower == "1"_L1 || lower == "yes"_L1;
    }

    // Upsert Key=value lines without disturbing comments or unrelated settings.
    bool writeConfigValues(const QString &path, const QHash<QString, QString> &wanted, QString *error)
    {
        QDir{}.mkpath(QFileInfo{path}.absolutePath());

        QString newline = "\n"_L1;
        QStringList lines;
        QSet<QString> seen;
        if (QFileInfo::exists(path))
        {
            QFile in{path};
            if (!in.open(QFile::ReadOnly))
            {
                *error = "Couldn't read %1."_L1.arg(path);
                return false;
            }
            auto text = QString::fromUtf8(in.readAll());
            if (text.contains("\r\n"_L1))
                newline = "\r\n"_L1;
            text.replace("\r\n"_L1, "\n"_L1);
            text.replace('\r'_L1, '\n'_L1);
            const bool trailing = text.endsWith('\n'_L1);
            if (trailing)
                text.chop(1);
            lines = text.isEmpty() ? QStringList{} : text.split('\n'_L1);

            static const QRegularExpression keyed{R"(^(\s*)([A-Za-z0-9_]+)(\s*=\s*)(.*)$)"_L1};
            for (auto &line : lines)
            {
                const auto match = keyed.match(line);
                if (!match.hasMatch())
                    continue;
                const auto key = match.captured(2);
                if (!wanted.contains(key))
                    continue;
                line = match.captured(1) + key + match.captured(3) + wanted.value(key);
                seen.insert(key);
            }
        }

        for (auto it = wanted.cbegin(); it != wanted.cend(); ++it)
            if (!seen.contains(it.key()))
                lines << it.key() + '=' + it.value();

        QSaveFile out{path};
        if (!out.open(QFile::WriteOnly | QFile::Truncate))
        {
            *error = "Couldn't write %1."_L1.arg(path);
            return false;
        }
        const auto body = lines.join(newline) + newline;
        if (out.write(body.toUtf8()) != body.toUtf8().size() || !out.commit())
        {
            *error = "Couldn't save %1."_L1.arg(path);
            return false;
        }
        return true;
    }

    QVariantMap hint(const QString &key, const QString &title, const QString &detail, const QString &state,
                     const QString &action = {}, const QString &actionLabel = {})
    {
        QVariantMap step{{"key"_L1, key}, {"title"_L1, title}, {"detail"_L1, detail}, {"state"_L1, state}};
        if (!action.isEmpty())
        {
            step["action"_L1] = action;
            step["actionLabel"_L1] = actionLabel;
        }
        return step;
    }
} // namespace

UEVRAFW::UEVRAFW(QObject *parent)
    : Mod{parent}
{
    // Execute downloads on the first event tick to give time for the download
    // manager to initialize
    QTimer::singleShot(0, this, [this] {
        // What was saved last time is there at once; the answer from GitHub replaces it when it differs
        parseReleaseInfoJson();
        refreshReleases();
    });
}

UEVRAFW *UEVRAFW::instance()
{
    static auto u = new UEVRAFW;
    return u;
}

UEVRAFW *UEVRAFW::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QString UEVRAFW::info() const
{
    return "DX12 only (Kaon adds -dx12). Turn on DLSS or DLAA in the game, inject as usual, then pick Alternate "
           "Frame Warping in the in-game menu and enable Ghosting Fix. Joeyhodge builds also need Bootstrap "
           "Separate View States. About 500 MB extra VRAM. Nightly covers older UE titles; joeyhodge targets UE "
           "5.5–5.8. AMD/Intel GPUs need OptiScaler (optional tools) for a DLSS path."_L1;
}

QString UEVRAFW::launchOptions() const
{
    // PureDark: AFW only supports DX12. Prefer the game's built-in DX12 path over DX11.
    return "%command% -dx12"_L1;
}

QStringList UEVRAFW::conflictingLaunchOptions() const
{
    return {"-dx11"_L1, "-d3d11"_L1};
}

const QLoggingCategory &UEVRAFW::logger() const
{
    return UEVRAFWLog();
}

QList<Mod *> UEVRAFW::dependencies() const
{
    return {Dotnet::instance()};
}

bool UEVRAFW::isInstalledForGame(const Game *) const
{
    return currentRelease() && currentRelease()->downloaded();
}

bool UEVRAFW::isJoeyhodgeRelease(const ModRelease *release) const
{
    return release && release->name().contains("joeyhodge"_L1, Qt::CaseInsensitive);
}

QString UEVRAFW::configFileForGame(const Game *game) const
{
    for (const auto &path : candidateConfigPaths(game))
        if (QFileInfo::exists(path))
            return QFileInfo{path}.absoluteFilePath();
    // Prefer the steamuser path for a first write when the prefix exists
    const auto candidates = candidateConfigPaths(game);
    return candidates.isEmpty() ? QString{} : QFileInfo{candidates.constFirst()}.absoluteFilePath();
}

bool UEVRAFW::recommendedConfigApplied(const Game *game) const
{
    const auto path = configFileForGame(game);
    if (path.isEmpty() || !QFileInfo::exists(path))
        return false;
    const auto values = readConfigValues(path);
    if (values.value(renderingMethodKey) != renderingMethodAfw || !truthy(values.value(ghostingFixKey)))
        return false;
    if (isJoeyhodgeRelease(currentRelease()) && !truthy(values.value(bootstrapKey)))
        return false;
    return true;
}

bool UEVRAFW::applyRecommendedConfig(Game *game, QString *error)
{
    QString localError;
    auto *err = error ? error : &localError;
    if (!game)
    {
        *err = "No game selected."_L1;
        return false;
    }
    if (game->winePrefix().isEmpty() || !QFileInfo::exists(game->winePrefix() + "/system.reg"_L1))
    {
        *err = "Launch the game once so it has a Proton prefix, then try again."_L1;
        return false;
    }
    if (exeBaseName(game).isEmpty())
    {
        *err = "Kaon couldn't find a Windows executable for %1."_L1.arg(game->name());
        return false;
    }

    QHash<QString, QString> wanted{{renderingMethodKey, renderingMethodAfw}, {ghostingFixKey, "true"_L1}};
    if (isJoeyhodgeRelease(currentRelease()))
        wanted.insert(bootstrapKey, "true"_L1);

    const auto path = configFileForGame(game);
    if (path.isEmpty())
    {
        *err = "Kaon couldn't find where UEVR stores settings for this game."_L1;
        return false;
    }
    if (!writeConfigValues(path, wanted, err))
        return false;

    qCInfo(UEVRAFWLog) << "Wrote AFW recommended config to" << path;
    return true;
}

bool UEVRAFW::runHintAction(Game *game, const QString &action)
{
    if (action != "applyAfwConfig"_L1)
        return false;
    QString error;
    if (!applyRecommendedConfig(game, &error))
    {
        qCWarning(UEVRAFWLog) << "AFW config apply failed:" << error;
        return true; // handled, even on failure — GameStatus surfaces via noticed/actionFailed
    }
    return true;
}

QVariantList UEVRAFW::softHints(const Game *game) const
{
    QVariantList out;
    if (!game)
        return out;

    const bool applied = recommendedConfigApplied(game);
    const bool joey = isJoeyhodgeRelease(currentRelease());
    QString detail =
        "After inject, open the UEVR menu (Insert): set Rendering Method to Alternate Frame Warping and enable "
        "Ghosting Fix"_L1;
    if (joey)
        detail += ", plus Bootstrap Separate View States"_L1;
    detail += ". Also turn on DLSS or DLAA in the game's graphics settings before injecting."_L1;
    if (applied)
        out << hint("afwSetup"_L1,
                    "AFW in-game setup"_L1,
                    "Recommended config is set (AFW + Ghosting Fix"_L1 +
                        (joey ? " + Bootstrap"_L1 : QString{}) +
                        "). Still enable DLSS/DLAA in the game itself."_L1,
                    "ok"_L1);
    else
        out << hint("afwSetup"_L1,
                    "AFW in-game setup"_L1,
                    detail,
                    "warn"_L1,
                    "applyAfwConfig"_L1,
                    "Write recommended config"_L1);

    out << hint("afwVram"_L1,
                "AFW VRAM"_L1,
                "Alternate Frame Warping uses about 500 MB of extra VRAM. If the game hitchs to single-digit FPS, "
                "lower resolution or disable AFW."_L1,
                "warn"_L1);
    return out;
}

void UEVRAFW::downloadRelease(ModRelease *release)
{
    if (!release || release->assets().isEmpty())
        return;

    Aptabase::instance()->track("download-"_L1 + settingsGroup(), {{"version"_L1, release->name()}});

    const auto releaseId = release->id();
    const auto asset = release->assets().constFirst();
    DownloadManager::instance()->download(
        QNetworkRequest{asset.url},
        releaseTitle(release),
        true,
        [this, releaseId, asset](const QByteArray &data) {
            const QString targetDir = path(Paths::BasePath) + '/' + QString::number(releaseId);
            if (QString error;
                !asset.matches(data, &error) || !Archive::extractFresh(data, targetDir, "UEVRInjector.exe"_L1, &error))
            {
                failDownload(error);
                return;
            }

            // AFW is inert without PureDark's plugin; refuse a zip that only has stock UEVR files.
            if (!QFileInfo::exists(targetDir + "/PDAFWPlugin.dll"_L1))
            {
                QDir{targetDir}.removeRecursively();
                failDownload("That download has no PDAFWPlugin.dll in it, so Kaon can't use it as UEVR AFW."_L1);
                return;
            }

            // A release refresh may have replaced the ModRelease this download started with.
            if (auto *downloaded = releaseFromId(releaseId))
                downloaded->setDownloaded(true);
        },
        [](const QNetworkReply::NetworkError, const QString &errorMessage) {
            qCWarning(UEVRAFWLog) << "Download UEVR AFW failed:" << errorMessage;
        });
}

void UEVRAFW::deleteRelease(ModRelease *release)
{
    if (!release || !release->downloaded())
        return;

    QDir installDir{path(Paths::BasePath) + '/' + QString::number(release->id())};
    if (installDir.removeRecursively())
        release->setDownloaded(false);
}

void UEVRAFW::launchModImpl(Game *game)
{
    Wine::instance()->runInWine(currentRelease()->name(), game, path(Paths::CurrentInjector), true);
}

QMap<int, Game::LaunchOption> UEVRAFW::acceptableInstallCandidates(const Game *game) const
{
    auto options = Mod::acceptableInstallCandidates(game);
    options.removeIf(
        [](const std::pair<int, Game::LaunchOption> &exe) { return exe.second.platform != Game::Platform::Windows; });
    return options;
}

QString UEVRAFW::path(const Paths p) const
{
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + '/' + settingsGroup();
    switch (p)
    {
    case Paths::BasePath:
        return base;
    case Paths::CurrentInjector:
        return base + '/' + QString::number(currentRelease()->id()) + "/UEVRInjector.exe"_L1;
    case Paths::CachedReleasesJSON:
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/%1_releases.json"_L1.arg(settingsGroup());
    default:
        return {};
    }
}

void UEVRAFW::refreshReleases()
{
    QNetworkRequest req{{"https://api.github.com/repos/PureDark/UEVR/releases?per_page=100"_L1}};
    req.setRawHeader("X-GitHub-Api-Version"_ba, "2022-11-28"_ba);

    DownloadManager::instance()->refreshCached(
        req,
        "UEVR AFW release information"_L1,
        path(Paths::CachedReleasesJSON),
        [this](bool changed) {
            if (changed || m_releases.isEmpty())
                parseReleaseInfoJson();
        },
        [](const QString &errorMessage) { qCInfo(UEVRAFWLog) << "Error while fetching releases:" << errorMessage; });
}

void UEVRAFW::parseReleaseInfoJson()
{
    QFile cachedReleases{path(Paths::CachedReleasesJSON)};
    if (!cachedReleases.open(QFile::ReadOnly))
        return;

    const auto releases = QJsonDocument::fromJson(cachedReleases.readAll());
    if (!releases.isArray())
    {
        qCWarning(UEVRAFWLog) << "Cached UEVR AFW release information is invalid";
        return;
    }

    const int currentId = currentRelease() ? currentRelease()->id() : m_lastCurrentReleaseId;

    // Each GitHub release ships two injector builds. Neither is a Kaon nightly: "nightly" here means the
    // build is based on praydog's UEVR nightly, and "joeyhodge" is the UE 5.5-5.8 fork. They are listed as
    // separate versions so a profile can ask for one of them. Asset ids are unique, unlike the shared release id.
    QList<ModRelease *> parsed;
    auto appendVariant = [this, &parsed](const QJsonValue &release, const QString &prefix, const QString &variant,
                                         const QString &variantLabel) {
        auto releaseName = release["name"_L1].toString();
        if (releaseName.isEmpty())
            releaseName = release["tag_name"_L1].toString();
        const auto published = QDateTime::fromString(release["published_at"_L1].toString(), Qt::ISODate);
        // Identical timestamps sort unstably. Keep the nightly-based build above the joeyhodge build.
        const auto timestamp = variant == "nightly"_L1 ? published : published.addMSecs(-1);

        for (const auto &asset : release["assets"_L1].toArray())
        {
            const auto assetName = asset["name"_L1].toString();
            if (!assetName.startsWith(prefix, Qt::CaseInsensitive) || !assetName.endsWith(".zip"_L1, Qt::CaseInsensitive))
                continue;

            const auto id = asset["id"_L1].toInt();
            if (id <= 0)
                continue;

            const auto dir = path(Paths::BasePath) + '/' + QString::number(id);
            const auto downloaded = QFileInfo::exists(dir + "/UEVRInjector.exe"_L1) &&
                                    QFileInfo::exists(dir + "/PDAFWPlugin.dll"_L1);

            QList<ModRelease::Asset> assets;
            assets.push_back({
                .id = id,
                .name = assetName,
                .url = {asset["browser_download_url"_L1].toString()},
                .timestamp = QDateTime::fromString(asset["updated_at"_L1].toString(), Qt::ISODate),
                .size = asset["size"_L1].toInt(),
                .digest = asset["digest"_L1].toString(),
            });

            parsed.push_back(new ModRelease{
                id, releaseName + " · "_L1 + variantLabel, timestamp, false, false, downloaded, assets, this});
        }
    };

    for (const auto &release : releases.array())
    {
        appendVariant(release, "UEVR-nightly_AFW_"_L1, "nightly"_L1, "nightly (UE ≤5.4)"_L1);
        appendVariant(release, "UEVR-joeyhodge_AFW_"_L1, "joeyhodge"_L1, "joeyhodge (UE 5.5–5.8)"_L1);
    }

    if (parsed.isEmpty())
    {
        qCWarning(UEVRAFWLog) << "No UEVR AFW releases found";
        return;
    }

    beginResetModel();
    for (const auto release : std::as_const(m_releases))
        release->deleteLater();
    m_releases = parsed;
    endResetModel();

    if (currentId != 0 && !releaseFromId(currentId))
        qCWarning(UEVRAFWLog) << "Saved UEVR AFW release" << currentId << "is no longer available";
    setCurrentRelease(releaseFromId(currentId) ? currentId : m_releases.constFirst()->id());
}
