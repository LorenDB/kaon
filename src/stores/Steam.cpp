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
        return item->name();
    case Roles::GameObject:
        return QVariant::fromValue(item);
    }

    return {};
}

QHash<int, QByteArray> Steam::roleNames() const
{
    return {{Roles::GameObject, "game"_ba}};
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

    const auto parseLibraryFolders = [this](const QString &vdfPath) -> bool {
        std::ifstream vdfFile{vdfPath.toStdString()};

        try
        {
            auto libraryFolders = tyti::vdf::read(vdfFile);
            for (const auto &[_, folder] : libraryFolders.childs)
                for (const auto &[appId, _] : folder->childs["apps"]->attribs)
                    m_games.push_back(Game::fromSteam(std::stoi(appId), QString::fromStdString(folder->attribs["path"]), this));

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
