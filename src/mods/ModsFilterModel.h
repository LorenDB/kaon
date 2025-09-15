#pragma once

#include <QQmlEngine>
#include <QSortFilterProxyModel>

#include "Game.h"
#include "Mod.h"

class ModsFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Game *game READ game WRITE setGame NOTIFY gameChanged FINAL)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged FINAL)
    Q_PROPERTY(bool showIncompatible MEMBER m_showIncompatible NOTIFY showIncompatibleChanged FINAL)

public:
    explicit ModsFilterModel(QObject *parent = nullptr);
    static void registerMod(Mod *mod);

    Game *game() const { return m_game; }
    QString search() const { return m_search; }

    void setGame(Game *game);
    void setSearch(const QString &search);

signals:
    void gameChanged(Game *game);
    void searchChanged();
    void showIncompatibleChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    Game *m_game = nullptr;
    QString m_search;
    bool m_showIncompatible = false;
};
