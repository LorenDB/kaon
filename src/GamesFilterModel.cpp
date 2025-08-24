#include "GamesFilterModel.h"

#include <QSettings>

#include "Aptabase.h"
#include "CustomGames.h"
#include "Heroic.h"
#include "Itch.h"
#include "Steam.h"

using namespace Qt::Literals;

GamesFilterModel::GamesFilterModel(QObject *parent)
    : QSortFilterProxyModel{parent},
      m_models{new QConcatenateTablesProxyModel{this}}
{
    m_models->addSourceModel(Steam::instance());
    m_models->addSourceModel(Itch::instance());
    m_models->addSourceModel(Heroic::instance());
    m_models->addSourceModel(CustomGames::instance());
    setSourceModel(m_models);

    m_engineFilter.setFlag(Game::Engine::Unreal);

    m_typeFilter.setFlag(Game::AppType::Game);
    m_typeFilter.setFlag(Game::AppType::Demo);

    m_storeFilter.setFlag(Game::Store::Steam);
    m_storeFilter.setFlag(Game::Store::Itch);
    m_storeFilter.setFlag(Game::Store::Heroic);
    m_storeFilter.setFlag(Game::Store::Custom);

    connect(this, &GamesFilterModel::engineFilterChanged, this, &GamesFilterModel::invalidateFilter);
    connect(this, &GamesFilterModel::typeFilterChanged, this, &GamesFilterModel::invalidateFilter);
    connect(this, &GamesFilterModel::storeFilterChanged, this, &GamesFilterModel::invalidateFilter);
    connect(this, &GamesFilterModel::searchChanged, this, &GamesFilterModel::invalidateFilter);
    connect(this, &GamesFilterModel::sortTypeChanged, this, &GamesFilterModel::invalidate);

    setDynamicSortFilter(true);
    sort(0);

    QSettings settings;
    settings.beginGroup("GamesFilterModel"_L1);

    // TODO: remove me after a while
    QSettings migration;
    migration.beginGroup("Steam"_L1);
    const auto oldSetting = migration.value("viewType"_L1, ViewType::Grid).value<ViewType>();

    m_viewType = settings.value("viewType"_L1, oldSetting).value<ViewType>();
    m_sortType = settings.value("sortType"_L1, SortType::LastPlayed).value<SortType>();
}

void GamesFilterModel::setSearch(const QString &search)
{
    m_search = search;
    emit searchChanged();
}

void GamesFilterModel::setViewType(ViewType viewType)
{
    m_viewType = viewType;
    emit viewTypeChanged(m_viewType);

    QSettings settings;
    settings.beginGroup("GamesFilterModel"_L1);
    settings.setValue("viewType"_L1, m_viewType);
}

void GamesFilterModel::setSortType(SortType sortType)
{
    m_sortType = sortType;
    emit sortTypeChanged(m_sortType);

    QSettings settings;
    settings.beginGroup("GamesFilterModel"_L1);
    settings.setValue("sortType"_L1, m_sortType);
}

bool GamesFilterModel::isEngineFilterSet(Game::Engine engine)
{
    return m_engineFilter.testFlag(engine);
}

bool GamesFilterModel::isTypeFilterSet(Game::AppType type)
{
    return m_typeFilter.testFlag(type);
}

bool GamesFilterModel::isStoreFilterSet(Game::Store store)
{
    return m_storeFilter.testFlag(store);
}

void GamesFilterModel::setEngineFilter(Game::Engine engine, bool state)
{
    m_engineFilter.setFlag(engine, state);
    emit engineFilterChanged();
}

void GamesFilterModel::setTypeFilter(Game::AppType type, bool state)
{
    m_typeFilter.setFlag(type, state);
    emit typeFilterChanged();
}

void GamesFilterModel::setStoreFilter(Game::Store store, bool state)
{
    m_storeFilter.setFlag(store, state);
    emit storeFilterChanged();
}

bool GamesFilterModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const auto g = sourceModel()->data(sourceModel()->index(row, 0, parent), Steam::Roles::GameObject).value<Game *>();
    if (!g || !g->isValid())
        return false;
    if (!m_engineFilter.testFlag(g->engine()))
        return false;
    if (!m_typeFilter.testFlag(g->type()))
        return false;
    if (!m_storeFilter.testFlag(g->store()))
        return false;
    if (!m_search.isEmpty() && !g->name().contains(m_search, Qt::CaseInsensitive))
        return false;

    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
}

bool GamesFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const auto leftGame = sourceModel()->data(left, Steam::Roles::GameObject).value<Game *>();
    const auto rightGame = sourceModel()->data(right, Steam::Roles::GameObject).value<Game *>();

    switch (m_sortType)
    {
    case SortType::Alphabetical:
        return leftGame->name() < rightGame->name();
    case SortType::LastPlayed:
        return leftGame->lastPlayed() > rightGame->lastPlayed();
    default:
        Aptabase::instance()->track("invalid-sort-type-bug", {{"sortType", m_sortType}});
        return leftGame->id() < rightGame->id();
    }
}
