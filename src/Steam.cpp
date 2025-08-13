#include "Steam.h"

#include <QDir>
#include <QDirIterator>

#include "Dotnet.h"
#include "vdf_parser.hpp"

using namespace Qt::Literals;

Steam *Steam::s_instance = nullptr;

Game::Game(int steamId, QObject *parent)
    : QObject{parent},
      m_id{steamId}
{
    const auto basepath = QDir::homePath() + "/.local/share/Steam/steamapps"_L1;

    std::ifstream acfFile{basepath.toStdString() + "/appmanifest_" + std::to_string(m_id) + ".acf"};
    auto app = tyti::vdf::read(acfFile);

    m_name = QString::fromStdString(app.attribs["name"]);
    m_installDir = basepath + "/common/"_L1 + QString::fromStdString(app.attribs["installdir"]);
    if (app.attribs.contains("LastPlayed"))
        m_lastPlayed = QDateTime::fromSecsSinceEpoch(std::stoi(app.attribs["LastPlayed"]));

    const auto imageDir =
            QDir::homePath() + "/.local/share/Steam/appcache/librarycache/"_L1 + QString::number(m_id);
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

    QString compatdata = basepath + "/compatdata/"_L1 + QString::number(m_id);
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
    scanSteam();
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
    beginResetModel();

    for (const auto game : m_games)
        game->deleteLater();
    m_games.clear();

    // TODO: use more intelligent Steam location detection algorithm
    // maybe check Rai Pal or Protontricks for inspiration
    const auto basepath = QDir::homePath() + "/.local/share/Steam/steamapps"_L1;
    if (!QFileInfo::exists(basepath))
    {
        qDebug() << "Cannot find local Steam data!";
        endResetModel();
        return;
    }

    std::ifstream vdfFile{basepath.toStdString() + "/libraryfolders.vdf"};
    auto libraryFolders = tyti::vdf::read(vdfFile);

    for (const auto &[_, folder] : libraryFolders.childs)
        for (const auto &[appId, _] : folder->childs["apps"]->attribs)
            m_games.push_back(new Game{std::stoi(appId), this});

    std::sort(m_games.begin(), m_games.end(), [](const auto &a, const auto &b) { return a->lastPlayed() > b->lastPlayed(); });
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
