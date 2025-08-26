#include "Store.h"

#include <QSettings>
#include <QTimer>

using namespace Qt::Literals;

Store::Store(QObject *parent)
    : QAbstractListModel{parent}
{
    connect(this, &Store::rowsInserted, this, &Store::countChanged);
    connect(this, &Store::rowsRemoved, this, &Store::countChanged);
    connect(this, &Store::modelReset, this, &Store::countChanged);

    // We need to finish creating this object before scanning the store. Otherwise the Game constructor may call instance()
    // on the subclass, but since we haven't finished creating this object, s_instance won't have been set, which leads to a
    // brief loop of Store objects being created.
    QTimer::singleShot(0, this, [this] {
        QSettings settings;
        if (settings.value("autoscan"_L1, true).toBool())
            scanStore();
    });
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
