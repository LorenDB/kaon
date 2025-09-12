#include "Mod.h"

ModRelease::ModRelease(
    int id, QString name, QDateTime timestamp, bool nightly, bool installed, QUrl downloadUrl, QObject *parent)
    : QObject{parent},
      m_id{id},
      m_name{name},
      m_timestamp{timestamp},
      m_nightly{nightly},
      m_downloadUrl{downloadUrl}
{}

void ModRelease::setInstalled(bool state)
{
    m_installed = state;
    emit installedChanged(state);
}

Mod::Mod(QObject *parent)
    : QAbstractListModel{parent}
{}

bool Mod::checkGameCompatibility(const Game *game) const
{
    return compatibleEngines().testFlag(game->engine());
}

bool Mod::dependenciesSatisfied(const Game *game) const
{
    for (const auto d : dependencies())
    {
        if (!d->isInstalledForGame(game))
            return false;
    }
    return true;
}

ModRelease *Mod::releaseFromId(const int id) const
{
    for (const auto release : releases())
        if (release->id() == id)
            return release;
    return nullptr;
}

int Mod::rowCount(const QModelIndex &parent) const
{
    return releases().count();
}

QVariant Mod::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= releases().size())
        return {};
    const auto &item = releases().at(index.row());
    switch (role)
    {
    case Roles::Id:
        return item->id();
    case Qt::DisplayRole:
    case Roles::Name:
        return item->name();
    case Roles::Timestamp:
        return item->timestamp();
    case Roles::Installed:
        return item->installed();
    }

    return {};
}

QHash<int, QByteArray> Mod::roleNames() const
{
    return {{Roles::Id, "id"_ba},
            {Roles::Name, "name"_ba},
            {Roles::Timestamp, "timestamp"_ba},
            {Roles::Installed, "installed"_ba}};
}
