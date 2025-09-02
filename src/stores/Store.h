#pragma once

#include <QAbstractListModel>

#include "Game.h"

class Store : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString storeRoot READ storeRoot CONSTANT FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Roles
    {
        GameObject = Qt::UserRole + 1,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const final;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const final;
    QHash<int, QByteArray> roleNames() const final;

    QList<Game *> games() const { return m_games; }

    virtual QString storeRoot() const = 0;
    Q_INVOKABLE virtual void scanStore() = 0;
    int count() const;

signals:
    void countChanged();

protected:
    explicit Store(QObject *parent = nullptr);

    QList<Game *> m_games;
};
