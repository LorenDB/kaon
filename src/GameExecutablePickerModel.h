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
    // options are the executables to choose between, already without the ones that would make no difference
    explicit GameExecutablePickerModel(Mod *mod,
                                       Game *game,
                                       const QList<Game::LaunchOption> &options,
                                       std::function<void(Game::LaunchOption)> callback);

    int rowCount(const QModelIndex &parent = QModelIndex()) const final;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const final;
    QHash<int, QByteArray> roleNames() const final;

    Q_INVOKABLE void select(int index);
    Q_INVOKABLE void destroySelf();

private:
    Mod *m_mod;
    Game *m_game;
    QList<Game::LaunchOption> m_availableLaunchOptions;
    // What each of them is called in the dialog
    QStringList m_labels;

    std::function<void(Game::LaunchOption)> m_callback;
};
