#include "Steam.h"

#include <QDir>
#include <QDirIterator>
#include <QTimer>

#include "vdf_parser.hpp"

using namespace Qt::Literals;

Steam *Steam::s_instance = nullptr;

Steam::Steam(QObject *parent)
    : QAbstractListModel{parent}
{
    static const QStringList steamPaths = {
        QDir::homePath() + "/.local/share/Steam"_L1,
        QDir::homePath() + "/.steam/steam"_L1,
    };

    for (const auto &path : steamPaths)
    {
        if (QFileInfo fi{path}; fi.exists() && fi.isDir())
        {
            m_steamRoot = path;
            break;
        }
    }

    if (m_steamRoot.isEmpty())
        qDebug() << "Steam not found";

    // We need to finish creating this object before scanning Steam. Otherwise the Game constructor will call
    // Steam::instance(), but since we haven't finished creating this object, s_instance hasn't been set, which leads to a
    // brief loop of Steam objects being created.
    QTimer::singleShot(0, this, &Steam::scanSteam);
}

Steam *Steam::instance()
{
    if (s_instance == nullptr)
        s_instance = new Steam;
    return s_instance;
}

Steam *Steam::create(QQmlEngine *qml, QJSEngine *js)
{
    return instance();
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
        return item->name();
    case Roles::SteamID:
        return item->id();
    case Roles::InstallDir:
        return item->installDir();
    case Roles::CardImage:
        return item->cardImage();
    case Roles::HeroImage:
        return item->heroImage();
    case Roles::LogoImage:
        return item->logoImage();
    case Roles::LastPlayed:
        return item->lastPlayed();
    }

    return {};
}

QHash<int, QByteArray> Steam::roleNames() const
{
    return {{Roles::Name, "name"_ba},
        {Roles::SteamID, "steamId"_ba},
        {Roles::InstallDir, "installDir"_ba},
        {Roles::CardImage, "cardImage"_ba},
        {Roles::HeroImage, "heroImage"_ba},
        {Roles::LogoImage, "logoImage"_ba},
        {Roles::LastPlayed, "lastPlayed"_ba}};
}

Game *Steam::gameFromId(int steamId) const
{
    for (const auto game : m_games)
        if (game->id() == steamId)
            return game;
    return nullptr;
}

void Steam::scanSteam()
{
    if (m_steamRoot.isEmpty())
        return;

    beginResetModel();

    for (const auto game : std::as_const(m_games))
        game->deleteLater();
    m_games.clear();

    if (const QFileInfo fi{m_steamRoot + "/steamapps/libraryfolders.vdf"_L1}; fi.exists() && fi.isFile())
    {
        std::ifstream vdfFile{fi.absoluteFilePath().toStdString()};

        try
        {
            auto libraryFolders = tyti::vdf::read(vdfFile);

            for (const auto &[_, folder] : libraryFolders.childs)
                for (const auto &[appId, _] : folder->childs["apps"]->attribs)
                    m_games.push_back(new Game{std::stoi(appId), this});

            std::sort(m_games.begin(), m_games.end(), [](const auto &a, const auto &b) {
                return a->lastPlayed() > b->lastPlayed();
            });
        }
        catch (const std::length_error &e)
        {
            qDebug() << "Failure while parsing libraryfolders.vdf:" << e.what();
        }
    }
    else
        qDebug() << "Could not open libraryfolders.vdf at" << fi.absoluteFilePath();

    endResetModel();
}

SteamFilter::SteamFilter(QObject *parent)
{
    m_engineFilter.setFlag(Game::Engine::Unreal);
    m_engineFilter.setFlag(Game::Engine::Unity);
    m_engineFilter.setFlag(Game::Engine::Godot);
    m_engineFilter.setFlag(Game::Engine::Source);
    m_engineFilter.setFlag(Game::Engine::Unknown);

    m_typeFilter.setFlag(Game::AppType::Game);
    m_typeFilter.setFlag(Game::AppType::Demo);

    setSourceModel(Steam::instance());
    connect(this, &SteamFilter::engineFilterChanged, this, &SteamFilter::invalidateFilter);
    connect(this, &SteamFilter::typeFilterChanged, this, &SteamFilter::invalidateFilter);
    connect(this, &SteamFilter::searchChanged, this, &SteamFilter::invalidateFilter);
}

void SteamFilter::setSearch(const QString &search)
{
    m_search = search;
    emit searchChanged();
}

bool SteamFilter::isEngineFilterSet(Game::Engine engine)
{
    return m_engineFilter.testFlag(engine);
}

void SteamFilter::setEngineFilter(Game::Engine engine, bool state)
{
    m_engineFilter.setFlag(engine, state);
    emit engineFilterChanged();
}

bool SteamFilter::isTypeFilterSet(Game::AppType type)
{
    return m_typeFilter.testFlag(type);
}

void SteamFilter::setTypeFilter(Game::AppType type, bool state)
{
    m_typeFilter.setFlag(type, state);
    emit typeFilterChanged();
}

bool SteamFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const auto g = Steam::instance()->gameFromId(
                sourceModel()->data(sourceModel()->index(row, 0, parent), Steam::Roles::SteamID).toInt());
    if (!g)
        return false;
    if (!m_engineFilter.testFlag(g->engine()))
        return false;
    if (!m_typeFilter.testFlag(g->type()))
        return false;
    if (!m_search.isEmpty() && !g->name().contains(m_search, Qt::CaseInsensitive))
        return false;

    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
}
