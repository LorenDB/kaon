#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>

#include "Game.h"
#include "Mod.h"

class GameExecutablePickerModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Can only be created with data from C++ side")

    Q_PROPERTY(Game *game MEMBER m_game CONSTANT FINAL)

public:
    explicit GameExecutablePickerModel(Mod *mod, Game *game, std::function<void(Game::LaunchOption)> callback);

    int rowCount(const QModelIndex &parent = QModelIndex()) const final;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const final;
    QHash<int, QByteArray> roleNames() const final;

    Q_INVOKABLE void select(int index);
    Q_INVOKABLE void destroySelf();

private:
    Mod *m_mod;
    Game *m_game;
    QMap<int, Game::LaunchOption> m_availableLaunchOptions;

    std::function<void(Game::LaunchOption)> m_callback;
};
