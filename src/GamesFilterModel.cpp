#include "GamesFilterModel.h"

#include <QSettings>

#include "Steam.h"

using namespace Qt::Literals;

GamesFilterModel::GamesFilterModel(QObject *parent)
    : QSortFilterProxyModel{parent},
      m_models{new QConcatenateTablesProxyModel{this}}
{
    m_models->addSourceModel(Steam::instance());
    setSourceModel(m_models);

    m_engineFilter.setFlag(Game::Engine::Unreal);

    m_typeFilter.setFlag(Game::AppType::Game);
    m_typeFilter.setFlag(Game::AppType::Demo);

    connect(this, &GamesFilterModel::engineFilterChanged, this, &GamesFilterModel::invalidateFilter);
    connect(this, &GamesFilterModel::typeFilterChanged, this, &GamesFilterModel::invalidateFilter);
    connect(this, &GamesFilterModel::searchChanged, this, &GamesFilterModel::invalidateFilter);

    QSettings settings;
    settings.beginGroup("GamesFilterModel"_L1);

    // TODO: remove me after a while
    QSettings migration;
    migration.beginGroup("Steam"_L1);
    const auto oldSetting = migration.value("viewType"_L1, ViewType::Grid).value<ViewType>();

    m_viewType = settings.value("viewType"_L1, oldSetting).value<ViewType>();
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

bool GamesFilterModel::isEngineFilterSet(Game::Engine engine)
{
    return m_engineFilter.testFlag(engine);
}

void GamesFilterModel::setEngineFilter(Game::Engine engine, bool state)
{
    m_engineFilter.setFlag(engine, state);
    emit engineFilterChanged();
}

bool GamesFilterModel::isTypeFilterSet(Game::AppType type)
{
    return m_typeFilter.testFlag(type);
}

void GamesFilterModel::setTypeFilter(Game::AppType type, bool state)
{
    m_typeFilter.setFlag(type, state);
    emit typeFilterChanged();
}

bool GamesFilterModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const auto g = sourceModel()->data(sourceModel()->index(row, 0, parent), Steam::Roles::GameObject).value<Game *>();
    if (!g)
        return false;
    if (!m_engineFilter.testFlag(g->engine()))
        return false;
    if (!m_typeFilter.testFlag(g->type()))
        return false;
    if (!m_search.isEmpty() && !g->name().contains(m_search, Qt::CaseInsensitive))
        return false;

    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
}
