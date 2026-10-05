#include "Mod.h"

#include <QFileInfo>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QSettings>
#include <QTimer>

#include "Aptabase.h"
#include "GameExecutablePickerModel.h"
#include "GamesFilterModel.h"
#include "ModsFilterModel.h"

ModRelease::ModRelease(
    int id, QString name, QDateTime timestamp, bool nightly, bool downloaded, QList<Asset> assets, QObject *parent)
    : QObject{parent},
      m_id{id},
      m_name{name},
      m_timestamp{timestamp},
      m_nightly{nightly},
      m_downloaded{downloaded},
      m_assets{assets}
{}

void ModRelease::setDownloaded(bool state)
{
    m_downloaded = state;
    emit downloadedChanged(state);
    if (auto mod = qobject_cast<Mod *>(parent()))
        emit mod->releaseDownloadedChanged(this);
}

qint64 ModRelease::size() const
{
    qint64 total = 0;
    for (const auto &asset : m_assets)
        total += asset.size;
    return total;
}

Mod::Mod(QObject *parent)
    : QAbstractListModel{parent}
{
    // These singletons live for the whole process and have no parent. QML assumes it owns a QObject returned from an
    // invokable, and the garbage collector deletes a parentless one once the UI drops it. preferredMod() hands mods to
    // QML that way; the next group() then crashes in vrMods() on the freed object.
    QJSEngine::setObjectOwnership(this, QJSEngine::CppOwnership);

    // This HAS to be called later, or else the vtable won't have been built and therefore calling any virtual functions from
    // the mods model will crash
    QTimer::singleShot(0, this, [this] { ModsFilterModel::registerMod(this); });

    // Execute downloads on the first event tick to give time for the download
    // manager to initialize
    QTimer::singleShot(0, this, [this] {
        QSettings settings;
        settings.beginGroup(settingsGroup());
        if (int id = settings.value("currentRelease"_L1, 0).toInt(); id != 0)
        {
            m_lastCurrentReleaseId = id;
            setCurrentRelease(id);
        }
    });
}

int Mod::launchDelay() const
{
    QSettings settings;
    settings.beginGroup(settingsGroup());
    return settings.value("launchDelay"_L1, 30).toInt();
}

void Mod::setLaunchDelay(int seconds)
{
    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.setValue("launchDelay"_L1, seconds);
    emit launchDelayChanged();
}

int Mod::downloadedCount() const
{
    const auto list = releases();
    return std::count_if(list.cbegin(), list.cend(), [](const auto r) { return r->downloaded(); });
}

bool Mod::hasNightlies() const
{
    const auto list = releases();
    return std::any_of(list.cbegin(), list.cend(), [](const auto r) { return r->nightly(); });
}

bool Mod::isBusyForGame(const Game *game) const
{
    return m_busyGames.contains(game);
}

void Mod::setBusyForGame(const Game *game, bool busy)
{
    if (busy == m_busyGames.contains(game))
        return;
    if (busy)
        m_busyGames.insert(game);
    else
        m_busyGames.remove(game);
    emit busyChanged();
}

bool Mod::dependenciesSatisfied(const Game *game) const
{
    for (const auto d : dependencies())
    {
        if (!d->isInstalledForGame(game))
            return false;
    }
    return true;
}

QString Mod::missingDependencies(const Game *game) const
{
    QStringList deps;
    for (const auto d : dependencies())
        if (!d->isInstalledForGame(game))
            deps << "- "_L1 + d->displayName();
    return deps.join('\n');
}

ModRelease *Mod::currentRelease() const
{
    return m_currentRelease;
}

ModRelease *Mod::releaseFromId(const int id) const
{
    for (const auto release : releases())
        if (release->id() == id)
            return release;
    return nullptr;
}

ModRelease *Mod::releaseInstalledForGame(const Game *game)
{
    if (!game)
        return nullptr;

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->settingsId());
    return releaseFromId(settings.value("installedVersion"_L1).toInt());
}

int Mod::rowCount(const QModelIndex &parent) const
{
    return releases().count();
}

QVariant Mod::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= releases().size())
        return {};
    const auto &item = releases().at(index.row());
    switch (role)
    {
    case Roles::Id:
        return item->id();
    case Qt::DisplayRole:
    case Roles::Name:
        return item->name();
    case Roles::Timestamp:
        return item->timestamp();
    case Roles::Downloaded:
        return item->downloaded();
    }

    return {};
}

QHash<int, QByteArray> Mod::roleNames() const
{
    return {{Roles::Id, "id"_ba},
            {Roles::Name, "name"_ba},
            {Roles::Timestamp, "timestamp"_ba},
            {Roles::Downloaded, "downloaded"_ba}};
}

void Mod::setCurrentRelease(const int id)
{
    auto newVersion =
        std::find_if(releases().constBegin(), releases().constEnd(), [id](const auto &r) { return r->id() == id; });
    if (newVersion == releases().constEnd())
    {
        qCWarning(logger()) << "Attempted to activate nonexistent %1"_L1.arg(displayName());
        Aptabase::instance()->track("nonexistent-mod-activation-bug",
                                    {{"mod"_L1, displayName()}, {"id"_L1, QString::number(id)}});
        return;
    }

    m_currentRelease = releaseFromId(id);
    emit currentReleaseChanged(m_currentRelease);

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.setValue("currentRelease"_L1, id);
}

void Mod::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    if (!QFileInfo::exists(exe.executable))
    {
        Aptabase::instance()->track("nonexistent-executable-mod-install-bug",
                                    {{"executable"_L1, exe.executable}, {"game"_L1, game->name()}});
        fail("Kaon couldn't find %1. Rescan your libraries and try again."_L1.arg(exe.executable));
        return;
    }

    Aptabase::instance()->track("install-"_L1 + settingsGroup(),
                                {{"version"_L1, currentRelease()->name()}, {"game"_L1, game->name()}});

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->settingsId());
    settings.setValue("installedVersion"_L1, m_currentRelease->id());

    emit installedInGameChanged(game);
}

QMap<int, Game::LaunchOption> Mod::acceptableInstallCandidates(const Game *game) const
{
    if (!compatibleEngines().testFlag(game->engine()))
        return {};
    auto copy = game->executables();
    copy.removeIf([](const std::pair<int, Game::LaunchOption> &exe) { return !QFileInfo::exists(exe.second.executable); });
    return copy;
}

ModReleaseFilter::ModReleaseFilter(QObject *parent)
{
    setSortRole(Mod::Roles::Timestamp);
    setDynamicSortFilter(true);
    sort(0);
}

int ModReleaseFilter::indexFromRelease(ModRelease *release) const
{
    if (!release)
        return -1;

    for (int i = 0; i < sourceModel()->rowCount(); ++i)
    {
        auto sourceIndex = sourceModel()->index(i, 0);
        if (sourceModel()->data(sourceIndex, Mod::Roles::Id).toInt() == release->id())
        {
            QModelIndex proxyIndex = mapFromSource(sourceIndex);
            if (proxyIndex.isValid())
                return proxyIndex.row();
        }
    }

    return -1;
}

void ModReleaseFilter::setMod(Mod *mod)
{
    m_mod = mod;
    setSourceModel(mod);
    emit modChanged(mod);
    if (!m_mod)
        return;

    QSettings settings;
    settings.beginGroup(m_mod->settingsGroup());
    setShowNightlies(settings.value("showNightlies"_L1, false).toBool());
}

void ModReleaseFilter::setShowNightlies(bool state)
{
    beginFilterChange();
    m_showNightlies = state;
    endFilterChange();

    emit showNightliesChanged(state);

    if (m_mod)
    {
        QSettings settings;
        settings.beginGroup(m_mod->settingsGroup());
        settings.setValue("showNightlies"_L1, m_showNightlies);
    }
}

bool ModReleaseFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (!m_mod)
        return false;

    const auto release =
        m_mod->releaseFromId(sourceModel()->data(sourceModel()->index(row, 0, parent), Mod::Roles::Id).toInt());

    if (!release || release->assets().isEmpty())
        return false;
    if (!m_showNightlies && release->nightly())
        return false;

    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
}

bool ModReleaseFilter::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    return left.data(Mod::Roles::Timestamp).toDateTime() > right.data(Mod::Roles::Timestamp).toDateTime();
}

void Mod::fail(const QString &message)
{
    qCWarning(logger()).noquote() << message;
    emit installFailed(message);
}

void Mod::launchMod(Game *game)
{
    if (!game || !currentRelease())
        return;

    Aptabase::instance()->track("launch-%1"_L1.arg(settingsGroup()),
                                {{"version"_L1, currentRelease()->name()}, {"game"_L1, game->name()}});
    launchModImpl(game);
}

void Mod::installMod(Game *game)
{
    auto exes = acceptableInstallCandidates(game).values();
    QSet<QString> seen;
    exes.removeIf([&seen](const auto &exe) {
        if (seen.contains(exe.executable))
            return true;
        else
        {
            seen.insert(exe.executable);
            return false;
        }
    });
    // What goes into the prefix is installed the same way whichever executable it is installed for
    if (installsIntoPrefix() && exes.size() > 1)
        exes.resize(1);

    switch (exes.size())
    {
    case 0:
        Aptabase::instance()->track("no-executable-mod-install-bug", {{"mod"_L1, displayName()}, {"game"_L1, game->name()}});
        fail("%1 has no executable that %2 can be installed for."_L1.arg(game->name(), displayName()));
        break;
    case 1:
        Aptabase::instance()->track("install-%1"_L1.arg(settingsGroup()),
                                    {{"version"_L1, currentRelease()->name()}, {"game"_L1, game->name()}});
        installModImpl(game, exes.first());
        break;
    default:
    {
        // The dialog can still be open when a rescan deletes this Game. Install into whichever object replaced it.
        const auto store = static_cast<int>(game->store());
        const auto id = game->id();
        auto m =
            new GameExecutablePickerModel{this, game, [this, store, id](const Game::LaunchOption &exe) {
                                              if (auto *current = GamesFilterModel::instance()->gameByIdentity(store, id))
                                                  installModImpl(current, exe);
                                          }};
        emit requestChooseLaunchOption(m);
        break;
    }
    }
}

void Mod::uninstallMod(Game *game)
{
    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.beginGroup(game->settingsId());
    settings.remove("installedVersion"_L1);

    emit installedInGameChanged(game);
}
