#include "Mod.h"

#include <QLoggingCategory>
#include <QSettings>
#include <QTimer>

#include "Aptabase.h"
#include "ModsFilterModel.h"

ModRelease::ModRelease(
    int id, QString name, QDateTime timestamp, bool nightly, bool installed, QUrl downloadUrl, QObject *parent)
    : QObject{parent},
      m_id{id},
      m_name{name},
      m_timestamp{timestamp},
      m_nightly{nightly},
      m_installed{installed},
      m_downloadUrl{downloadUrl}
{}

void ModRelease::setInstalled(bool state)
{
    m_installed = state;
    emit installedChanged(state);
}

Mod::Mod(QObject *parent)
    : QAbstractListModel{parent}
{
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

bool Mod::checkGameCompatibility(const Game *game) const
{
    return compatibleEngines().testFlag(game->engine());
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
    case Roles::Installed:
        return item->installed();
    }

    return {};
}

QHash<int, QByteArray> Mod::roleNames() const
{
    return {{Roles::Id, "id"_ba},
            {Roles::Name, "name"_ba},
            {Roles::Timestamp, "timestamp"_ba},
            {Roles::Installed, "installed"_ba}};
}

void Mod::setCurrentRelease(const int id)
{
    auto newVersion =
        std::find_if(releases().constBegin(), releases().constEnd(), [id](const auto &r) { return r->id() == id; });
    if (newVersion == releases().constEnd())
    {
        qCWarning(logger()) << "Attempted to activate nonexistent %1"_L1.arg(displayName());
        Aptabase::instance()->track("nonexistent-mod-activation-bug");
        return;
    }

    m_currentRelease = releaseFromId(id);
    emit currentReleaseChanged(m_currentRelease);

    QSettings settings;
    settings.beginGroup(settingsGroup());
    settings.setValue("currentRelease"_L1, id);
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

    QSettings settings;
    settings.beginGroup(m_mod->settingsGroup());
    setShowNightlies(settings.value("showNightlies"_L1, false).toBool());
}

void ModReleaseFilter::setShowNightlies(bool state)
{
    m_showNightlies = state;
    invalidateRowsFilter();

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

    if (!m_showNightlies)
    {
        const auto release =
            m_mod->releaseFromId(sourceModel()->data(sourceModel()->index(row, 0, parent), Mod::Roles::Id).toInt());
        if (!release || release->nightly())
            return false;
    }
    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
}

bool ModReleaseFilter::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    return left.data(Mod::Roles::Timestamp).toDateTime() > right.data(Mod::Roles::Timestamp).toDateTime();
}
