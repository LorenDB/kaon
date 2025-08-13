#include "Steam.h"

#include <QDir>
#include <QDirIterator>
#include <QTimer>

#include "Dotnet.h"
#include "vdf_parser.hpp"

using namespace Qt::Literals;

Steam *Steam::s_instance = nullptr;

Game::Game(int steamId, QObject *parent)
    : QObject{parent},
      m_id{steamId}
{
    std::ifstream acfFile{Steam::instance()->steamPath().toStdString() + "/steamapps/appmanifest_" + std::to_string(m_id) + ".acf"};
    auto app = tyti::vdf::read(acfFile);

    m_name = QString::fromStdString(app.attribs["name"]);
    m_installDir = Steam::instance()->steamPath() + "/steamapps/common/"_L1 + QString::fromStdString(app.attribs["installdir"]);
    if (app.attribs.contains("LastPlayed"))
        m_lastPlayed = QDateTime::fromSecsSinceEpoch(std::stoi(app.attribs["LastPlayed"]));

    const auto imageDir = Steam::instance()->steamPath() + "/appcache/librarycache/"_L1 + QString::number(m_id);
    QDirIterator images{imageDir, QDirIterator::Subdirectories};
    while (images.hasNext())
    {
        images.next();
        if (images.fileName() == "library_600x900.jpg"_L1 && m_cardImage.isEmpty())
            m_cardImage = "file://"_L1 + images.filePath();
        if (images.fileName() == "library_hero.jpg"_L1 && m_heroImage.isEmpty())
            m_heroImage = "file://"_L1 + images.filePath();
        if (images.fileName() == "logo.png"_L1 && m_logoImage.isEmpty())
            m_logoImage = "file://"_L1 + images.filePath();
    }

    QString compatdata = Steam::instance()->steamPath() + "/steamapps/compatdata/"_L1 + QString::number(m_id);
    if (QFileInfo fi{compatdata}; fi.exists() && fi.isDir())
    {
        m_protonExists = true;

        if (QFileInfo pfx{compatdata + "/pfx"_L1}; pfx.exists() && pfx.isDir())
            m_protonPrefix = pfx.absoluteFilePath();

        QFile file{compatdata + "/config_info"_L1};
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream compatInfo(&file);
            compatInfo.readLine(); // first line is useless for now
            QString proton = compatInfo.readLine();
            static const QRegularExpression re("/(files|dist)/share/fonts/$");
            if (proton.contains(re))
            {
                m_selectedProtonInstall = proton.remove(re);
                if (QFileInfo files{m_selectedProtonInstall + "/files"_L1}; files.exists() && files.isDir())
                    m_filesOrDist = "files"_L1;
                else
                    m_filesOrDist = "dist"_L1;
            }
        }
    }
}

QString Game::protonBinary() const
{
    return m_selectedProtonInstall + '/' + m_filesOrDist + "/bin/wine"_L1;
}

bool Game::dotnetInstalled() const
{
    return Dotnet::instance()->isDotnetInstalled(m_id);
}


Steam::Steam(QObject *parent)
    : QAbstractListModel{parent}
{
    const QStringList steamPaths = {
        QDir::homePath() + "/.local/share/Steam"_L1,
        QDir::homePath() + "/.steam/steam"_L1,
    };
    for (const auto &path : steamPaths)
    {
        if (QFileInfo fi{path}; fi.exists() && fi.isDir())
        {
            m_steamPath = path;
            break;
        }
    }
    if (m_steamPath.isEmpty())
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
    if (m_steamPath.isEmpty())
        return;

    beginResetModel();

    for (const auto game : m_games)
        game->deleteLater();
    m_games.clear();

    if (const QFileInfo fi{m_steamPath + "/steamapps/libraryfolders.vdf"_L1}; fi.exists() && fi.isFile())
    {
        std::ifstream vdfFile{fi.absoluteFilePath().toStdString()};

        try
        {
            auto libraryFolders = tyti::vdf::read(vdfFile);

            for (const auto &[_, folder] : libraryFolders.childs)
                for (const auto &[appId, _] : folder->childs["apps"]->attribs)
                    m_games.push_back(new Game{std::stoi(appId), this});

            std::sort(m_games.begin(), m_games.end(), [](const auto &a, const auto &b) { return a->lastPlayed() > b->lastPlayed(); });
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
    setSourceModel(Steam::instance());
    connect(this, &SteamFilter::showAllChanged, this, &SteamFilter::invalidateRowsFilter);
}

void SteamFilter::setShowAll(bool state)
{
    m_showAll = state;
    emit showAllChanged(state);
}

bool SteamFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (!m_showAll)
    {
        const auto g = Steam::instance()->gameFromId(
                    sourceModel()->data(sourceModel()->index(row, 0, parent), Steam::Roles::SteamID).toInt());
        if (!g)
            return false;

        // filter out Proton and Steam Linux runtime installations
        if (QFile::exists(g->installDir() + "/proton"_L1) || QFile::exists(g->installDir() + "/toolmanifest.vdf"_L1))
            return false;
    }
    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
}
