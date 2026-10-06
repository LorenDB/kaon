#include "GameExecutablePickerModel.h"

#include <QDir>
#include <QTimer>

GameExecutablePickerModel::GameExecutablePickerModel(Mod *mod,
                                                     Game *game,
                                                     const QList<Game::LaunchOption> &options,
                                                     std::function<void(Game::LaunchOption)> callback)
    : QAbstractListModel{mod},
      m_mod{mod},
      m_game{game},
      m_availableLaunchOptions{options},
      m_callback{callback}
{
    // Worked out now: a rescan can delete the game while the dialog is still open
    const QDir root{game->installDir()};
    for (const auto &exe : options)
    {
        // Two launch options often share a file name and differ only in the folder
        auto path = root.relativeFilePath(exe.executable);
        if (path.startsWith("../"_L1))
            path = exe.executable;

        const auto bits = exe.arch == Game::Architecture::x64 ? "64-bit "_L1 :
                          exe.arch == Game::Architecture::x86 ? "32-bit "_L1 :
                                                                ""_L1;
        const auto platform = exe.platform == Game::Platform::Windows ? "Windows"_L1 :
                              exe.platform == Game::Platform::Linux   ? "Linux"_L1 :
                                                                        "macOS"_L1;
        m_labels << path + u" · "_s + bits + platform;
    }
}

int GameExecutablePickerModel::rowCount(const QModelIndex &parent) const
{
    return m_availableLaunchOptions.size();
}

QVariant GameExecutablePickerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_availableLaunchOptions.size())
        return {};

    if (role == Qt::DisplayRole)
        return m_labels.at(index.row());
    return {};
}

QHash<int, QByteArray> GameExecutablePickerModel::roleNames() const
{
    return {{Qt::DisplayRole, "text"_ba}};
}

void GameExecutablePickerModel::select(int index)
{
    if (index >= 0 && index < m_availableLaunchOptions.size())
        m_callback(m_availableLaunchOptions.at(index));
}

void GameExecutablePickerModel::destroySelf()
{
    // Unfortunately we can't just deleteLater() as it causes a brief UI glitch. Therefore, we'll delete it in a few seconds.
    QTimer::singleShot(5000, this, &GameExecutablePickerModel::deleteLater);
}
