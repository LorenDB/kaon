#include "Store.h"

using namespace Qt::Literals;

Store::Store(QObject *parent)
    : QAbstractListModel{parent}
{
    connect(this, &Store::rowsInserted, this, &Store::countChanged);
    connect(this, &Store::rowsRemoved, this, &Store::countChanged);
    connect(this, &Store::modelReset, this, &Store::countChanged);
}

int Store::rowCount(const QModelIndex &parent) const
{
    return m_games.count();
}

QVariant Store::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size())
        return {};
    const auto &item = m_games.at(index.row());
    switch (role)
    {
    case Qt::DisplayRole:
        return item->name();
    case Roles::GameObject:
        return QVariant::fromValue(item);
    }

    return {};
}

QHash<int, QByteArray> Store::roleNames() const
{
    return {{Roles::GameObject, "game"_ba}};
}

int Store::count() const
{
    return m_games.count();
}
