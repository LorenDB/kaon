#include "Store.h"

#include <QSettings>
#include <QTimer>

#include "GamesFilterModel.h"

Store::Store(QObject *parent)
    : QAbstractListModel{parent}
{
    connect(this, &Store::rowsInserted, this, &Store::countChanged);
    connect(this, &Store::rowsRemoved, this, &Store::countChanged);
    connect(this, &Store::modelReset, this, &Store::countChanged);

    // Don't you just love how constructors can't call virtual functions?
    QTimer::singleShot(0, this, [this] {
        QSettings settings;
        if (settings.value("autoscan"_L1, true).toBool())
            scanStore();
    });

    GamesFilterModel::instance()->registerStore(this);
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
