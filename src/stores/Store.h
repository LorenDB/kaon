#pragma once

#include <QAbstractListModel>

#include "Game.h"

class Store : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString storeRoot READ storeRoot CONSTANT FINAL)

public:
    enum Roles
    {
        GameObject = Qt::UserRole + 1,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const final;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const final;
    QHash<int, QByteArray> roleNames() const final;

    virtual QString storeRoot() const = 0;

protected:
    explicit Store(QObject *parent = nullptr);

    virtual void scanStore() = 0;

    QList<Game *> m_games;
};
