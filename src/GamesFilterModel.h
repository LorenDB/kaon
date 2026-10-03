#pragma once

#include <QConcatenateTablesProxyModel>
#include <QQmlEngine>
#include <QSortFilterProxyModel>

#include "Game.h"
#include "Store.h"

class GamesFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Game::Engines engineFilter READ engineFilter NOTIFY engineFilterChanged FINAL)
    Q_PROPERTY(Game::AppTypes typeFilter READ typeFilter NOTIFY typeFilterChanged FINAL)
    Q_PROPERTY(Game::Features featureFilter READ featureFilter NOTIFY typeFilterChanged FINAL)
    Q_PROPERTY(Game::Stores storeFilter READ storeFilter NOTIFY storeFilterChanged FINAL)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged FINAL)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged FINAL)

    Q_PROPERTY(SortType sortType READ sortType WRITE setSortType NOTIFY sortTypeChanged FINAL)
    Q_PROPERTY(
        FilterType featureFilterType READ featureFilterType WRITE setFeatureFilterType NOTIFY featureFilterTypeChanged FINAL)

public:
    static GamesFilterModel *instance();
    static GamesFilterModel *create(QQmlEngine *, QJSEngine *);

    void registerStore(Store *store);

    enum SortType
    {
        LastPlayed,
        Alphabetical,
    };
    Q_ENUM(SortType)

    enum class FilterType
    {
        HasAnyFilter,
        HasAllFilters,
    };
    Q_ENUM(FilterType)

    Game::Engines engineFilter() const { return m_engineFilter; }
    Game::AppTypes typeFilter() const { return m_typeFilter; }
    Game::Features featureFilter() const { return m_featureFilter; }
    Game::Stores storeFilter() const { return m_storeFilter; }
    QString search() const { return m_search; }
    bool scanning() const { return m_scanning; }

    SortType sortType() const { return m_sortType; }
    FilterType featureFilterType() const { return m_featureFilterType; }

    void setSearch(const QString &search);
    void setSortType(SortType sortType);
    void setFeatureFilterType(FilterType type);

    Q_INVOKABLE bool isEngineFilterSet(Game::Engine engine);
    Q_INVOKABLE bool isTypeFilterSet(Game::AppType type);
    Q_INVOKABLE bool isFeatureFilterSet(Game::Feature feature);
    Q_INVOKABLE bool isStoreFilterSet(Game::Store store);

    Q_INVOKABLE void setEngineFilter(Game::Engine engine, bool state);
    Q_INVOKABLE void setTypeFilter(Game::AppType type, bool state);
    Q_INVOKABLE void setFeatureFilter(Game::Feature feature, bool state);
    Q_INVOKABLE void setStoreFilter(Game::Store store, bool state);

    // The games that pass the filters, in display order
    Q_INVOKABLE QList<Game *> games() const;

signals:
    void engineFilterChanged();
    void typeFilterChanged();
    void featureFilterChanged();
    void storeFilterChanged();
    void searchChanged();
    void scanningChanged();
    // Emitted whenever games() would return something different
    void gamesChanged();

    void sortTypeChanged(GamesFilterModel::SortType sortType);
    void featureFilterTypeChanged(GamesFilterModel::FilterType type);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    explicit GamesFilterModel(QObject *parent = nullptr);
    ~GamesFilterModel() = default;

    void updateScanning();

    QConcatenateTablesProxyModel *m_models;
    QList<Store *> m_stores;
    bool m_scanning = false;

    Game::Engines m_engineFilter;
    Game::AppTypes m_typeFilter;
    Game::Features m_featureFilter;
    Game::Stores m_storeFilter;
    QString m_search;
    SortType m_sortType;

    FilterType m_featureFilterType = FilterType::HasAnyFilter;
};
