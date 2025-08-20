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
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged FINAL)
    Q_PROPERTY(ViewType viewType READ viewType WRITE setViewType NOTIFY viewTypeChanged FINAL)

public:
    explicit GamesFilterModel(QObject *parent = nullptr);

    enum ViewType
    {
        Grid,
        List,
    };
    Q_ENUM(ViewType)

    Game::Engines engineFilter() const { return m_engineFilter; }
    Game::AppTypes typeFilter() const { return m_typeFilter; }
    QString search() const { return m_search; }
    ViewType viewType() const { return m_viewType; }

    void setSearch(const QString &search);
    void setViewType(ViewType viewType);

    Q_INVOKABLE bool isEngineFilterSet(Game::Engine engine);
    Q_INVOKABLE void setEngineFilter(Game::Engine engine, bool state);
    Q_INVOKABLE bool isTypeFilterSet(Game::AppType type);
    Q_INVOKABLE void setTypeFilter(Game::AppType type, bool state);

signals:
    void engineFilterChanged();
    void typeFilterChanged();
    void searchChanged();
    void viewTypeChanged(ViewType viewType);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    QConcatenateTablesProxyModel *m_models;

    Game::Engines m_engineFilter;
    Game::AppTypes m_typeFilter;
    QString m_search;
    ViewType m_viewType;
};
