#include "Steam.h"

#include <QDir>
#include <QDirIterator>
#include <QLoggingCategory>
#include <QSettings>
#include <QTimer>

#include "Aptabase.h"
#include "vdf_parser.hpp"
#include "VDF.h"

using namespace Qt::Literals;

Q_LOGGING_CATEGORY(SteamLog, "steam")

Steam *Steam::s_instance = nullptr;

class SteamGame : public Game
{
    Q_OBJECT

public:
    SteamGame(const QString &steamId, const QString &steamDrive, QObject *parent)
        : Game{parent}
    {
        qCDebug(SteamLog) << "Creating game:" << steamId;

        m_id = steamId;
        m_canLaunch = true;
        m_canOpenSettings = true;

        const QString acfPath = "%1/steamapps/appmanifest_%2.acf"_L1.arg(steamDrive, m_id);
        try
        {
            std::ifstream acfFile{acfPath.toStdString()};
            auto app = tyti::vdf::read(acfFile);

            m_name = QString::fromStdString(app.attribs["name"]);
            m_installDir = steamDrive + "/steamapps/common/"_L1 + QString::fromStdString(app.attribs["installdir"]);
            if (app.attribs.contains("LastPlayed"))
                m_lastPlayed = QDateTime::fromSecsSinceEpoch(std::stoi(app.attribs["LastPlayed"]));
        }
        catch (const std::length_error &e)
        {
            qCWarning(SteamLog) << "Failure while parsing " << acfPath << ":" << e.what();
            auto parts = acfPath.split('/');
            Aptabase::instance()->track(
                        "failure-parsing-game-libraryfolders-bug"_L1,
                        {{"which"_L1, parts.size() >= 2 ? parts[parts.size() - 2] : ""_L1}, {"game"_L1, m_id}});
        }

        const auto imageDir = Steam::instance()->storeRoot() + "/appcache/librarycache/"_L1 + m_id;
        QDirIterator images{imageDir, QDirIterator::Subdirectories};
        while (images.hasNext())
        {
            images.next();
            if ((images.fileName() == "library_600x900.jpg"_L1 || images.fileName() == "library_capsule.jpg"_L1) &&
                    m_cardImage.isEmpty())
                m_cardImage = "file://"_L1 + images.filePath();
            else if (images.fileName() == "library_hero.jpg"_L1 && m_heroImage.isEmpty())
                m_heroImage = "file://"_L1 + images.filePath();
            else if (images.fileName() == "logo.png"_L1 && m_logoImage.isEmpty())
                m_logoImage = "file://"_L1 + images.filePath();
        }

        QString compatdata = steamDrive + "/steamapps/compatdata/"_L1 + m_id;
        m_protonPrefix = compatdata + "/pfx"_L1;
        if (QFileInfo fi{compatdata}; fi.exists() && fi.isDir())
        {
            qCDebug(SteamLog) << "Found Proton prefix for" << m_name << "at" << m_protonPrefix;
            QFile file{compatdata + "/config_info"_L1};
            if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                QTextStream compatInfo(&file);
                compatInfo.readLine(); // first line is useless for now
                QString proton = compatInfo.readLine();
                static const QRegularExpression re("/(files|dist)/share/fonts/$");
                if (proton.contains(re))
                {
                    QString protonBase = proton.remove(re);
                    if (QFileInfo files{protonBase + "/files"_L1}; files.exists() && files.isDir())
                        m_protonBinary = protonBase + "/files/bin/wine"_L1;
                    else
                        m_protonBinary = protonBase + "/dist/bin/wine"_L1;
                }
            }
        }

        auto *info = AppInfoVDF::instance()->game(m_id.toInt());
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

        for (auto &section : section.finished_sections)
        {
            if (section.name.startsWith("appinfo.config.launch."_L1))
            {
                int id = section.name.split('.').at(3).toInt();
                if (!m_executables.contains(id))
                    m_executables[id] = {};
                for (const auto &[key, value] : std::as_const(section.keys))
                {
                    if (key == "executable"_L1)
                        m_executables[id].executable = m_installDir + '/' + static_cast<const char *>(value.second);
                    else if (key == "type"_L1)
                    {
                        if (QString type{static_cast<const char *>(value.second)}; type == "vr" || type == "openxr")
                            m_supportsVr = true;
                    }
                    else if (key == "oslist"_L1)
                    {
                        const QString os = static_cast<const char *>(value.second);
                        if (os.contains("windows"_L1))
                            m_executables[id].platform |= LaunchOption::Platform::Windows;
                        if (os.contains("linux"_L1))
                            m_executables[id].platform |= LaunchOption::Platform::Linux;
                        if (os.contains("macos"_L1))
                            m_executables[id].platform |= LaunchOption::Platform::MacOS;
                    }
                }
            }

            // This is in theory a great way to detect Source games. Unfortunately it seems to pick up some non-Source games
            // (e.g. Sonic Racing Transformed for some reason). Therefore, I'm disabling this check for now unless and until I
            // can figure out why Sonic Racing Transformed is marked as using Source.
            //
            // else if (section.name == "appinfo.extended"_L1)
            // {
            //     for (const auto &[key, value] : std::as_const(section.keys))
            //     {
            //         // TODO: this does not detect all Source games (e.g. mods don't detect)
            //         if (key == "sourcegame"_L1 && parseInt(value.first, value.second) == 1)
            //             m_engine = Engine::Source;
            //     }
            // }

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
            else if (section.name == "appinfo.common")
            {
                for (const auto &[key, value] : std::as_const(section.keys))
                {
                    if (key == "name"_L1 && m_name.isEmpty())
                        m_name = static_cast<const char *>(value.second);
                    else if (key == "installdir"_L1 && m_installDir.isEmpty())
                        m_installDir = steamDrive + "/steamapps/common/"_L1 + static_cast<const char *>(value.second);
                    else if (key == "type"_L1)
                    {
                        QString type{static_cast<const char *>(value.second)};
                        type = type.toLower();
                        if (type == "game"_L1)
                            m_type = AppType::Game;
                        else if (type == "application"_L1)
                            m_type = AppType::App;
                        else if (type == "tool"_L1)
                            m_type = AppType::Tool;
                        else if (type == "demo"_L1)
                            m_type = AppType::Demo;
                        else if (type == "music"_L1)
                            m_type = AppType::Music;
                    }
                    else if (key == "icon"_L1)
                    {
                        const QString logoId{static_cast<const char *>(value.second)};
                        if (QFileInfo fi{u"%1/appcache/librarycache/%2/%3.jpg"_s.arg(
                                    Steam::instance()->storeRoot(), m_id, logoId)};
                                fi.exists())
                            m_icon = "file://"_L1 + fi.absoluteFilePath();
                        // TODO: could also extract clienticon key to find icons in $steamroot/steam/games
                    }
                    else if (key == "openvrsupport"_L1 || key == "openxrsupport"_L1)
                        m_supportsVr |= parseInt(value.first, value.second);
                    else if (key == "onlyvrsupport"_L1)
                    {
                        m_vrOnly = parseInt(value.first, value.second);
                        m_supportsVr |= m_vrOnly;
                    }
                }
            }
        }

        detectGameEngine();

        m_valid = true;
    }

    Store store() const override { return Store::Steam; }
};

Steam::Steam(QObject *parent)
    : Store{parent}
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
        qCInfo(SteamLog) << "Steam not found";
    else
        qCInfo(SteamLog) << "Found Steam:" << m_steamRoot;

    // We need to finish creating this object before scanning Steam. Otherwise the Game constructor will call
    // Steam::instance(), but since we haven't finished creating this object, s_instance hasn't been set, which leads to a
    // brief loop of Steam objects being created.
    QTimer::singleShot(0, this, &Steam::scanStore);
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

void Steam::scanStore()
{
    if (m_steamRoot.isEmpty())
        return;

    qCDebug(SteamLog) << "Scanning Steam library";
    beginResetModel();

    for (const auto game : std::as_const(m_games))
        game->deleteLater();
    m_games.clear();

    const auto parseLibraryFolders = [this](const QString &vdfPath) -> bool {
        qCDebug(SteamLog) << "Parsing libraryfolders.vdf from" << vdfPath;
        std::ifstream vdfFile{vdfPath.toStdString()};

        try
        {
            auto libraryFolders = tyti::vdf::read(vdfFile);
            for (const auto &[_, folder] : libraryFolders.childs)
            {
                qCDebug(SteamLog) << "Scanning Steam drive:" << folder->attribs["path"];
                for (const auto &[appId, _] : folder->childs["apps"]->attribs)
                {
                    if (auto g = new SteamGame{QString::fromStdString(appId),
                            QString::fromStdString(folder->attribs["path"]),
                            this};
                            g->isValid())
                        m_games.push_back(g);
                    else
                        g->deleteLater();
                }
            }
        }
        catch (const std::length_error &e)
        {
            qCWarning(SteamLog) << "Failure while parsing libraryfolders.vdf:" << e.what();
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
        qCWarning(SteamLog) << "Could not find libraryfolders.vdf";

    endResetModel();
}

#include "Steam.moc"
