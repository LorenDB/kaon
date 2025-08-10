#include "Steam.h"

#include <QDir>

#include "vdf_parser.hpp"

using namespace Qt::Literals;

Steam::Steam(QObject *parent)
    : QAbstractListModel{parent}
{
    scanSteam();
}

int Steam::rowCount(const QModelIndex &parent) const
{
    return m_games.count();
}

QVariant Steam::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size())
        return {};
    const auto &item = m_games.at(index.row());
    switch (role)
    {
    case Qt::DisplayRole:
    case Roles::Name:
        return item.name;
    case Roles::InstallDir:
        return item.installDir;
    case Roles::CardImage:
        return item.cardImage;
    case Roles::LastPlayed:
        return item.lastPlayed;
    }

    return {};
}

QHash<int, QByteArray> Steam::roleNames() const
{
    return {{Roles::Name, "name"_ba},
        {Roles::InstallDir, "installDir"_ba},
        {Roles::CardImage, "cardImage"_ba},
        {Roles::LastPlayed, "lastPlayed"_ba}};
}

void Steam::scanSteam()
{
    beginResetModel();
    m_games.clear();

    // TODO: use more intelligent Steam location detection algorithm
    // maybe check Rai Pal or Protontricks for inspiration
    const auto basepath = QDir::homePath() + "/.local/share/Steam/steamapps"_L1;

    std::ifstream vdfFile{basepath.toStdString() + "/libraryfolders.vdf"};
    auto libraryFolders = tyti::vdf::read(vdfFile);

    for (const auto &[_, folder] : libraryFolders.childs)
    {
        for (const auto &[appId, _] : folder->childs["apps"]->attribs)
        {
            Game g;
            g.id = std::stoi(appId);

            std::ifstream acfFile{basepath.toStdString() + "/appmanifest_" + appId + ".acf"};
            auto app = tyti::vdf::read(acfFile);

            g.name = QString::fromStdString(app.attribs["name"]);
            g.installDir = QString::fromStdString(app.attribs["installdir"]);
            g.lastPlayed = QDateTime::fromSecsSinceEpoch(std::stoi(app.attribs["LastPlayed"]));

            m_games.push_back(g);
        }
    }

    std::sort(m_games.begin(), m_games.end(), [](const auto &a, const auto &b) { return a.lastPlayed > b.lastPlayed; });
    endResetModel();
}
