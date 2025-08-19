#include "Steam.h"

#include <QDir>
#include <QDirIterator>
#include <QSettings>
#include <QTimer>

#include "Aptabase.h"
#include "vdf_parser.hpp"

using namespace Qt::Literals;

Steam *Steam::s_instance = nullptr;

Steam::Steam(QObject *parent)
    : QAbstractListModel{parent}
{
    QSettings settings;
    settings.beginGroup("Steam"_L1);
    m_viewType = settings.value("viewType"_L1, ViewType::Grid).value<ViewType>();

    static const QStringList steamPaths = {
        // ~/.steam comes first since it *should* point to the active Steam install
        QDir::homePath() + "/.steam/steam"_L1,
        QDir::homePath() + "/.local/share/Steam"_L1,
        // Debian ships a Steam installer script that uses this path for some weird reason
        QDir::homePath() + "/.steam/debian-installation"_L1,
        // TODO: sandboxing used in flatpak appears to make UEVR not work, so I'm disabling flatpak detection for now
        // QDir::homePath() + "/.var/app/com.valvesoftware.Steam/data/Steam"_L1,
        // TODO: Snap appears to be broken as well. This should get fixed eventually.
        // QDir::homePath() + "/snap/steam/common/.local/share/Steam"_L1,
        // QDir::homePath() + "/.snap/data/steam/common/.local/share/Steam"_L1,
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
    case Roles::Icon:
        return item->icon();
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
        {Roles::Icon, "iconImage"_ba},
        {Roles::LastPlayed, "lastPlayed"_ba}};
}

Game *Steam::gameFromId(int steamId) const
{
    for (const auto game : m_games)
        if (game->id() == steamId)
            return game;
    return nullptr;
}

void Steam::setViewType(ViewType viewType)
{
    m_viewType = viewType;
    emit viewTypeChanged(m_viewType);

    QSettings settings;
    settings.beginGroup("Steam"_L1);
    settings.setValue("viewType"_L1, m_viewType);
}

void Steam::scanSteam()
{
    if (m_steamRoot.isEmpty())
        return;

    beginResetModel();

    for (const auto game : std::as_const(m_games))
        game->deleteLater();
    m_games.clear();

    const auto parseLibraryFolders = [this](const QString &vdfPath) -> bool {
        std::ifstream vdfFile{vdfPath.toStdString()};

        try
        {
            auto libraryFolders = tyti::vdf::read(vdfFile);
            for (const auto &[_, folder] : libraryFolders.childs)
                for (const auto &[appId, _] : folder->childs["apps"]->attribs)
                    m_games.push_back(new Game{std::stoi(appId), QString::fromStdString(folder->attribs["path"]), this});

            std::sort(m_games.begin(), m_games.end(), [](const auto &a, const auto &b) {
                return a->lastPlayed() > b->lastPlayed();
            });
        }
        catch (const std::length_error &e)
        {
            qDebug() << "Failure while parsing libraryfolders.vdf:" << e.what();
            auto parts = vdfPath.split('/');
            Aptabase::instance()->track("failure-parsing-libraryfolders-bug"_L1,
                                        {{"which"_L1, parts.size() >= 2 ? parts[parts.size() - 2] : ""_L1}});
            return false;
        }

        return true;
    };

    bool parsed{false};
    if (const QFileInfo fi{m_steamRoot + "/steamapps/libraryfolders.vdf"_L1}; fi.exists() && fi.isFile())
        parsed = parseLibraryFolders({fi.absoluteFilePath()});
    if (const QFileInfo fi{m_steamRoot + "/config/libraryfolders.vdf"_L1}; !parsed && fi.exists() && fi.isFile())
        parseLibraryFolders({fi.absoluteFilePath()});
    if (!parsed)
        qDebug() << "Could not find libraryfolders.vdf";

    endResetModel();
}

SteamFilter::SteamFilter(QObject *parent)
{
    m_engineFilter.setFlag(Game::Engine::Unreal);

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
