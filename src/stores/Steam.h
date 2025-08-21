#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QSortFilterProxyModel>

#include "Game.h"

class Steam : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString steamRoot READ steamRoot CONSTANT FINAL)

public:
    static Steam *instance();
    static Steam *create(QQmlEngine *qml, QJSEngine *js);

    enum Roles
    {
        GameObject = Qt::UserRole + 1,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString steamRoot() const { return m_steamRoot; }

private:
    explicit Steam(QObject *parent = nullptr);
    static Steam *s_instance;

    void scanSteam();

    QString m_steamRoot;
    QList<Game *> m_games;
};
