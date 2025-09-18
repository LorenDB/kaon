#include "GameExecutablePickerModel.h"

#include <QTimer>

GameExecutablePickerModel::GameExecutablePickerModel(Mod *mod, Game *game, std::function<void(Game::LaunchOption)> callback)
    : QAbstractListModel{mod},
      m_mod{mod},
      m_game{game},
      m_callback{callback}
{
    m_availableLaunchOptions = m_mod->acceptableInstallCandidates(game);
}

int GameExecutablePickerModel::rowCount(const QModelIndex &parent) const
{
    return m_availableLaunchOptions.size();
}

QVariant GameExecutablePickerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_availableLaunchOptions.size())
        return {};

    const auto key = m_availableLaunchOptions.keys()[index.row()];
    const auto &exe = m_availableLaunchOptions[key];
    switch (role)
    {
    case Qt::DisplayRole:
    {
        return "%1 %2 (%3)"_L1.arg(QMetaEnum::fromType<Game::Platform>().valueToKey(static_cast<quint64>(exe.platform)),
                                   QMetaEnum::fromType<Game::Architecture>().valueToKey(static_cast<quint64>(exe.arch)),
                                   exe.executable.split('/').last());
    }
    default:
        break;
    }

    return {};
}

QHash<int, QByteArray> GameExecutablePickerModel::roleNames() const
{
    return {{Qt::DisplayRole, "text"_ba}};
}

void GameExecutablePickerModel::select(int index)
{
    m_callback(m_availableLaunchOptions[m_availableLaunchOptions.keys()[index]]);
}

void GameExecutablePickerModel::destroySelf()
{
    // Unfortunately we can't just deleteLater() as it causes a brief UI glitch. Therefore, we'll delete it in a few seconds.
    QTimer::singleShot(5000, this, &GameExecutablePickerModel::deleteLater);
}
