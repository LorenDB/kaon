#include "GamesFilterModel.h"

#include <QSettings>

#include "Aptabase.h"
#include "GameStatus.h"
#include "Launcher.h"

GamesFilterModel::GamesFilterModel(QObject *parent)
    : QSortFilterProxyModel{parent},
      m_models{new QConcatenateTablesProxyModel{this}}
{
    setSourceModel(m_models);

    // Every engine is shown. The library groups games by what Kaon can do for them instead of hiding engines.
    m_engineFilter.setFlag(Game::Engine::UnknownEngine);
    m_engineFilter.setFlag(Game::Engine::Unreal);
    m_engineFilter.setFlag(Game::Engine::Unity);
    m_engineFilter.setFlag(Game::Engine::Godot);
    m_engineFilter.setFlag(Game::Engine::Source);

    m_typeFilter.setFlag(Game::AppType::Game);
    m_typeFilter.setFlag(Game::AppType::Demo);

    m_featureFilter.setFlag(Game::Feature::Flatscreen);
    m_featureFilter.setFlag(Game::Feature::VR);
    m_featureFilter.setFlag(Game::Feature::Anticheat);

    m_storeFilter.setFlag(Game::Store::Steam);
    m_storeFilter.setFlag(Game::Store::Itch);
    m_storeFilter.setFlag(Game::Store::Heroic);
    m_storeFilter.setFlag(Game::Store::Custom);

    QSettings settings;
    settings.beginGroup("GamesFilterModel"_L1);
    m_sortType = settings.value("sortType"_L1, SortType::LastPlayed).value<SortType>();
    if (settings.contains("search"_L1))
        m_search = settings.value("search"_L1).toString();
    if (settings.contains("engineFilter"_L1))
        m_engineFilter = Game::Engines::fromInt(settings.value("engineFilter"_L1).toInt());
    if (settings.contains("typeFilter"_L1))
        m_typeFilter = Game::AppTypes::fromInt(settings.value("typeFilter"_L1).toInt());
    if (settings.contains("featureFilter"_L1))
        m_featureFilter = Game::Features::fromInt(settings.value("featureFilter"_L1).toInt());
    if (settings.contains("storeFilter"_L1))
        m_storeFilter = Game::Stores::fromInt(settings.value("storeFilter"_L1).toInt());
    if (settings.contains("featureFilterType"_L1))
        m_featureFilterType = static_cast<FilterType>(settings.value("featureFilterType"_L1).toInt());

    // Sort after loading the persisted sort type. Sorting before that would leave Alphabetical stuck on Recent.
    setDynamicSortFilter(true);
    sort(0);

    connect(this, &QAbstractItemModel::rowsInserted, this, &GamesFilterModel::gamesChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &GamesFilterModel::gamesChanged);
    connect(this, &QAbstractItemModel::rowsMoved, this, &GamesFilterModel::gamesChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &GamesFilterModel::gamesChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &GamesFilterModel::gamesChanged);
}

GamesFilterModel *GamesFilterModel::instance()
{
    static auto g = new GamesFilterModel;
    return g;
}

GamesFilterModel *GamesFilterModel::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

void GamesFilterModel::registerStore(Store *store)
{
    m_models->addSourceModel(store);
    m_stores.push_back(store);
    connect(store, &Store::scanningChanged, this, &GamesFilterModel::updateScanning);
    updateScanning();
}

void GamesFilterModel::updateScanning()
{
    auto scanning = false;
    for (const auto *store : std::as_const(m_stores))
        scanning = scanning || store->scanning();
    if (scanning == m_scanning)
        return;
    m_scanning = scanning;
    emit scanningChanged();
}

void GamesFilterModel::setSearch(const QString &search)
{
    if (m_search == search)
        return;
    beginFilterChange();
    m_search = search;
    emit searchChanged();
    endFilterChange();
    saveFilters();
}

void GamesFilterModel::setSortType(SortType sortType)
{
    if (m_sortType == sortType)
        return;
    m_sortType = sortType;
    emit sortTypeChanged(m_sortType);
    // begin/endFilterChange only refilters. Changing how lessThan orders rows needs a full invalidate.
    invalidate();
    saveFilters();
}

void GamesFilterModel::setFeatureFilterType(FilterType type)
{
    if (m_featureFilterType == type)
        return;
    beginFilterChange();
    m_featureFilterType = type;
    emit featureFilterTypeChanged(type);
    endFilterChange();
    saveFilters();
}

bool GamesFilterModel::isEngineFilterSet(Game::Engine engine, int)
{
    return m_engineFilter.testFlag(engine);
}

bool GamesFilterModel::isTypeFilterSet(Game::AppType type, int)
{
    return m_typeFilter.testFlag(type);
}

bool GamesFilterModel::isFeatureFilterSet(Game::Feature feature, int)
{
    return m_featureFilter.testFlag(feature);
}

bool GamesFilterModel::isStoreFilterSet(Game::Store store, int)
{
    return m_storeFilter.testFlag(store);
}

void GamesFilterModel::setEngineFilter(Game::Engine engine, bool state)
{
    if (m_engineFilter.testFlag(engine) == state)
        return;
    beginFilterChange();
    m_engineFilter.setFlag(engine, state);
    emit engineFilterChanged();
    ++m_filterRevision;
    emit filterRevisionChanged();
    endFilterChange();
    saveFilters();
}

void GamesFilterModel::setTypeFilter(Game::AppType type, bool state)
{
    if (m_typeFilter.testFlag(type) == state)
        return;
    beginFilterChange();
    m_typeFilter.setFlag(type, state);
    emit typeFilterChanged();
    ++m_filterRevision;
    emit filterRevisionChanged();
    endFilterChange();
    saveFilters();
}

void GamesFilterModel::setFeatureFilter(Game::Feature feature, bool state)
{
    if (m_featureFilter.testFlag(feature) == state)
        return;
    beginFilterChange();
    m_featureFilter.setFlag(feature, state);
    emit featureFilterChanged();
    ++m_filterRevision;
    emit filterRevisionChanged();
    endFilterChange();
    saveFilters();
}

void GamesFilterModel::setStoreFilter(Game::Store store, bool state)
{
    if (m_storeFilter.testFlag(store) == state)
        return;
    beginFilterChange();
    m_storeFilter.setFlag(store, state);
    emit storeFilterChanged();
    ++m_filterRevision;
    emit filterRevisionChanged();
    endFilterChange();
    saveFilters();
}

QList<Game *> GamesFilterModel::games() const
{
    QList<Game *> list;
    list.reserve(rowCount());
    for (int i = 0; i < rowCount(); ++i)
        if (auto g = data(index(i, 0), Store::Roles::GameObject).value<Game *>())
            list.push_back(g);
    return list;
}

Game *GamesFilterModel::gameByIdentity(int store, const QString &id) const
{
    if (id.isEmpty())
        return nullptr;
    const auto which = static_cast<Game::Store>(store);
    for (const auto *source : m_stores)
        for (auto *game : source->games())
            if (game && game->store() == which && game->id() == id)
                return game;
    return nullptr;
}

bool GamesFilterModel::retargetBeforeReset(const QList<Game *> &previous, const QList<Game *> &current)
{
    GameStatus::instance()->retargetGames(previous, current);
    return Launcher::instance()->retargetGame(previous, current);
}

void GamesFilterModel::retargetAfterReset(bool announce)
{
    if (announce)
        Launcher::instance()->announceGame();
}

void GamesFilterModel::saveFilters() const
{
    QSettings settings;
    settings.beginGroup("GamesFilterModel"_L1);
    settings.setValue("search"_L1, m_search);
    settings.setValue("sortType"_L1, m_sortType);
    settings.setValue("engineFilter"_L1, static_cast<int>(m_engineFilter));
    settings.setValue("typeFilter"_L1, static_cast<int>(m_typeFilter));
    settings.setValue("featureFilter"_L1, static_cast<int>(m_featureFilter));
    settings.setValue("storeFilter"_L1, static_cast<int>(m_storeFilter));
    settings.setValue("featureFilterType"_L1, static_cast<int>(m_featureFilterType));
}

bool GamesFilterModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const auto g = sourceModel()->data(sourceModel()->index(row, 0, parent), Store::Roles::GameObject).value<Game *>();
    if (!g || !g->isValid())
        return false;
    if (!m_engineFilter.testFlag(g->engine()))
        return false;
    if (!m_typeFilter.testFlag(g->type()))
        return false;

    if (m_featureFilterType == FilterType::HasAllFilters)
    {
        if (!g->features().testFlags(m_featureFilter))
            return false;
    }
    else if (m_featureFilterType == FilterType::HasAnyFilter)
    {
        if (!g->features().testAnyFlags(m_featureFilter))
            return false;
    }

    if (!m_storeFilter.testFlag(g->store()))
        return false;
    if (!m_search.isEmpty() && !g->name().contains(m_search, Qt::CaseInsensitive))
        return false;

    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
}

bool GamesFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const auto leftGame = sourceModel()->data(left, Store::Roles::GameObject).value<Game *>();
    const auto rightGame = sourceModel()->data(right, Store::Roles::GameObject).value<Game *>();

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
