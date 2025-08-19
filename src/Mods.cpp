#include "Mods.h"

Mods *Mods::s_instance = nullptr;

Mods::Mods(QObject *parent)
    : QAbstractListModel{parent}
{}

Mods *Mods::instance()
{
    if (s_instance == nullptr)
        s_instance = new Mods;
    return s_instance;
}

Mods *Mods::create(QQmlEngine *qml, QJSEngine *js)
{
    return instance();
}

int Mods::rowCount(const QModelIndex &parent) const
{
    return {};
}

QVariant Mods::data(const QModelIndex &index, int role) const
{
    return {};
}

QHash<int, QByteArray> Mods::roleNames() const
{
    return {};
}

ModsFilter::ModsFilter(QObject *parent)
    : QSortFilterProxyModel{parent}
{
    setSourceModel(Mods::instance());
}

void ModsFilter::setEngineFilter(Game::Engine engine)
{
    m_engineFilter = engine;
    emit engineFilterChanged(engine);
}

void ModsFilter::setShowIncompatible(bool state)
{
    m_showIncompatible = state;
    emit showIncompatibleChanged(state);
}

bool ModsFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    // TODO: if (!m_showIncompatible) return mod_supported_engine == m_engine;

    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
}
