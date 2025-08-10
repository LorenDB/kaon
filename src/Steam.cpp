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
    return 0;
}

QVariant Steam::data(const QModelIndex &index, int role) const
{
    return {};
}

QHash<int, QByteArray> Steam::roleNames() const
{
    return {};
}

void Steam::scanSteam()
{
    beginResetModel();

    // TODO: use more intelligent Steam location detection algorithm
    // maybe check Rai Pal or Protontricks for inspiration
    const auto basepath = QDir::homePath() + "/.local/share/Steam/steamapps"_L1;

    std::ifstream vdfFile{basepath.toStdString() + "/libraryfolders.vdf"};
    auto libraryFolders = tyti::vdf::read(vdfFile);

    for (const auto &[_, folder] : libraryFolders.childs)
    {
        for (const auto &[appId, _] : folder->childs["apps"]->attribs)
        {
            std::ifstream acfFile{basepath.toStdString() + "/appmanifest_" + appId + ".acf"};
            auto app = tyti::vdf::read(acfFile);

            for (const auto &[key, value] : app.attribs)
            {
                // TODO: build model
            }
        }
    }

    endResetModel();
}
