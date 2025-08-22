#pragma once

#include <QConcatenateTablesProxyModel>
#include <QQmlEngine>
#include <QSortFilterProxyModel>

#include "Game.h"

class GamesFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Game::Engines engineFilter READ engineFilter NOTIFY engineFilterChanged FINAL)
    Q_PROPERTY(Game::AppTypes typeFilter READ typeFilter NOTIFY typeFilterChanged FINAL)
    Q_PROPERTY(Game::Stores storeFilter READ storeFilter NOTIFY storeFilterChanged FINAL)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged FINAL)
    Q_PROPERTY(ViewType viewType READ viewType WRITE setViewType NOTIFY viewTypeChanged FINAL)
    Q_PROPERTY(SortType sortType READ sortType WRITE setSortType NOTIFY sortTypeChanged FINAL)

public:
    explicit GamesFilterModel(QObject *parent = nullptr);

    enum ViewType
    {
        Grid,
        List,
    };
    Q_ENUM(ViewType)

    enum SortType
    {
        LastPlayed,
        Alphabetical,
    };
    Q_ENUM(SortType)

    Game::Engines engineFilter() const { return m_engineFilter; }
    Game::AppTypes typeFilter() const { return m_typeFilter; }
    Game::Stores storeFilter() const { return m_storeFilter; }
    QString search() const { return m_search; }
    ViewType viewType() const { return m_viewType; }
    SortType sortType() const { return m_sortType; }

    void setSearch(const QString &search);
    void setViewType(ViewType viewType);
    void setSortType(SortType sortType);

    Q_INVOKABLE bool isEngineFilterSet(Game::Engine engine);
    Q_INVOKABLE bool isTypeFilterSet(Game::AppType type);
    Q_INVOKABLE bool isStoreFilterSet(Game::Store store);

    Q_INVOKABLE void setEngineFilter(Game::Engine engine, bool state);
    Q_INVOKABLE void setTypeFilter(Game::AppType type, bool state);
    Q_INVOKABLE void setStoreFilter(Game::Store store, bool state);

signals:
    void engineFilterChanged();
    void typeFilterChanged();
    void storeFilterChanged();
    void searchChanged();
    void viewTypeChanged(GamesFilterModel::ViewType viewType);
    void sortTypeChanged(GamesFilterModel::SortType sortType);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QConcatenateTablesProxyModel *m_models;

    Game::Engines m_engineFilter;
    Game::AppTypes m_typeFilter;
    Game::Stores m_storeFilter;
    QString m_search;
    ViewType m_viewType;
    SortType m_sortType;
};
