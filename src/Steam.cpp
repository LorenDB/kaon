#include "Steam.h"

#include <QDir>
#include <QDirIterator>
#include <QTimer>

#include "Dotnet.h"
#include "VDF.h"
#include "vdf_parser.hpp"

using namespace Qt::Literals;

Steam *Steam::s_instance = nullptr;

Game::Game(int steamId, QObject *parent)
    : QObject{parent},
      m_id{steamId}
{
    std::ifstream acfFile{Steam::instance()->steamRoot().toStdString() + "/steamapps/appmanifest_" + std::to_string(m_id) +
                ".acf"};
    auto app = tyti::vdf::read(acfFile);

    m_name = QString::fromStdString(app.attribs["name"]);
    m_installDir =
            Steam::instance()->steamRoot() + "/steamapps/common/"_L1 + QString::fromStdString(app.attribs["installdir"]);
    if (app.attribs.contains("LastPlayed"))
        m_lastPlayed = QDateTime::fromSecsSinceEpoch(std::stoi(app.attribs["LastPlayed"]));

    const auto imageDir = Steam::instance()->steamRoot() + "/appcache/librarycache/"_L1 + QString::number(m_id);
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

    QString compatdata = Steam::instance()->steamRoot() + "/steamapps/compatdata/"_L1 + QString::number(m_id);
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

    if (QFile::exists(m_installDir + "/proton"_L1) || QFile::exists(m_installDir + "/toolmanifest.vdf"_L1))
        m_engine = Engine::Runtime;

    const QStringList signsOfUnreal = {
        m_installDir + "/Engine/Binaries/Win64"_L1,
        m_installDir + "/Engine/Binaries/Win32"_L1,
        m_installDir + "/Engine/Binaries/ThirdParty"_L1,
    };
    for (const auto &sign : signsOfUnreal)
    {
        if (QFileInfo fi{sign}; fi.exists() && fi.isDir())
        {
            m_engine = Engine::Unreal;
            break;
        }
    }

    auto *info = AppInfoVDF::instance()->game(m_id);
    AppInfoVDF::AppInfo::Section section;
    AppInfoVDF::AppInfo::SectionDesc app_desc{};

    app_desc.blob = info->getRootSection(&app_desc.size);
    section.parse(app_desc);

    constexpr auto parseInt = [](const auto type, const auto &value) -> int64_t {
        switch (type)
        {
        case AppInfoVDF::AppInfo::Section::Int32:
            return *static_cast<int32_t *>(value);
        case AppInfoVDF::AppInfo::Section::Int64:
            return *static_cast<int64_t *>(value);
        case AppInfoVDF::AppInfo::Section::String:
            return std::stoi(static_cast<char *>(value));
        default:
            return 0;
        }
    };

    constexpr auto parseDouble = [](const auto type, const auto &value) -> int64_t {
        switch (type)
        {
        case AppInfoVDF::AppInfo::Section::Int32:
            return *static_cast<int32_t *>(value);
        case AppInfoVDF::AppInfo::Section::Int64:
            return *static_cast<int64_t *>(value);
        case AppInfoVDF::AppInfo::Section::String:
            return std::stod(static_cast<char *>(value));
        default:
            return 0;
        }
    };

    QStringList exes;
    for (auto &section : section.finished_sections)
    {
        if (section.name.startsWith("appinfo.config.launch."_L1))
        {
            for (const auto &[key, value] : std::as_const(section.keys))
            {
                if (key == "executable"_L1)
                {
                    assert(value.first == AppInfoVDF::AppInfo::Section::String);
                    exes.push_back(m_installDir + '/' + static_cast<const char *>(value.second));
                }
            }
        }
        else if (section.name == "appinfo.extended"_L1)
        {
            for (const auto &[key, value] : std::as_const(section.keys))
            {
                // TODO: this does not detect all Source games (e.g. mods don't detect)
                if (key == "sourcegame"_L1 && parseInt(value.first, value.second) == 1)
                    m_engine = Engine::Source;
            }
        }
        else if (section.name == "appinfo.common.library_assets.logo_position"_L1)
        {
            for (const auto &[key, value] : std::as_const(section.keys))
            {
                if (key == "width_pct"_L1)
                    m_logoWidth = parseDouble(value.first, value.second);
                else if (key == "height_pct"_L1)
                    m_logoHeight = parseDouble(value.first, value.second);
                else if (key == "pinned_position"_L1)
                {
                    QString posStr = static_cast<char *>(value.second);
                    if (posStr.startsWith("Center"_L1))
                        m_logoVPosition = LogoPosition::Center;
                    else if (posStr.startsWith("Top"_L1))
                        m_logoVPosition = LogoPosition::Top;
                    else if (posStr.startsWith("Bottom"_L1))
                        m_logoVPosition = LogoPosition::Bottom;

                    if (posStr.endsWith("Center"_L1))
                        m_logoHPosition = LogoPosition::Center;
                    else if (posStr.endsWith("Left"_L1))
                        m_logoHPosition = LogoPosition::Left;
                    else if (posStr.endsWith("Right"_L1))
                        m_logoHPosition = LogoPosition::Right;
                }
            }
        }
    }

    for (const auto &e : exes)
    {
        QFileInfo exe{e};
        if (QFileInfo dataDir{exe.absolutePath() + '/' + exe.baseName() + "_Data"_L1}; dataDir.exists() && dataDir.isDir())
        {
            m_engine = Engine::Unity;
            break;
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

    setSourceModel(Steam::instance());
    connect(this, &SteamFilter::engineFilterChanged, this, &SteamFilter::invalidateFilter);
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

bool SteamFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const auto g = Steam::instance()->gameFromId(
                sourceModel()->data(sourceModel()->index(row, 0, parent), Steam::Roles::SteamID).toInt());
    if (!g)
        return false;

    if (!m_engineFilter.testFlag(g->engine()))
        return false;

    if (!m_search.isEmpty() && !g->name().contains(m_search, Qt::CaseInsensitive))
        return false;

    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
}
