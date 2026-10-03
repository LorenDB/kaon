#include "Steam.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QSettings>
#include <QVariant>

#include "Aptabase.h"
#include "Flatpak.h"
#include "VDF.h"
#include "vdf_parser.hpp"

Q_LOGGING_CATEGORY(SteamLog, "steam")

class SteamGame : public Game
{
    Q_OBJECT

public:
    SteamGame(const QString &steamId,
              const QString &steamDrive,
              const QString &steamRoot,
              const QString &flatpakAppId,
              QObject *parent)
        : Game{parent}
    {
        qCDebug(SteamLog) << "Creating game:" << steamId;

        m_id = steamId;
        m_flatpakAppId = flatpakAppId;
        m_canLaunch = true;
        m_canOpenSettings = true;

        const QString acfPath = "%1/steamapps/appmanifest_%2.acf"_L1.arg(steamDrive, m_id);
        try
        {
            std::ifstream acfFile{acfPath.toStdString()};
            auto app = tyti::vdf::read(acfFile);

            m_name = QString::fromStdString(app.attribs["name"]);
            if (const auto installDir = QString::fromStdString(app.attribs["installdir"]); !installDir.isEmpty())
                m_installDir = steamDrive + "/steamapps/common/"_L1 + installDir;
            if (app.attribs.contains("LastPlayed"))
                m_lastPlayed = QDateTime::fromSecsSinceEpoch(std::stoi(app.attribs["LastPlayed"]));
        }
        catch (const std::length_error &e)
        {
            qCWarning(SteamLog) << "Failure while parsing " << acfPath << "from .acf:" << e.what();
            return;
        }

        const auto imageDir = steamRoot + "/appcache/librarycache/"_L1 + m_id;
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
        m_winePrefix = compatdata + "/pfx"_L1;
        if (QFileInfo fi{compatdata}; fi.exists() && fi.isDir())
        {
            qCDebug(SteamLog) << "Found Proton prefix for" << m_name << "at" << m_winePrefix;
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
                        m_wineBinary = protonBase + "/files/bin/wine"_L1;
                    else
                        m_wineBinary = protonBase + "/dist/bin/wine"_L1;
                }
            }
        }

        if (!m_flatpakAppId.isEmpty())
        {
            // Flatpak Steam records the host spelling, and that spelling is valid inside the sandbox.
            m_sandboxWinePrefix = m_winePrefix;
            m_sandboxWineBinary = m_wineBinary;
        }

        auto *info = AppInfoVDF::instance()->game(m_id.toInt());
        if (!info)
        {
            qCWarning(SteamLog) << "No appinfo entry for" << m_id << "under" << steamRoot;
            return;
        }
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
                            m_features.setFlag(Feature::VR);
                    }
                    else if (key == "oslist"_L1)
                    {
                        const QString os = static_cast<const char *>(value.second);
                        if (os.contains("windows"_L1))
                            m_executables[id].platform = Platform::Windows;
                        if (os.contains("linux"_L1))
                            m_executables[id].platform = Platform::Linux;
                        if (os.contains("macos"_L1))
                            m_executables[id].platform = Platform::MacOS;
                    }
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
                        else if (posStr.startsWith("Top"_L1) || posStr.startsWith("Upper"_L1))
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
                        if (type == "game"_L1 || type == "beta"_L1)
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
                    else if (key == "icon"_L1 || key == "clienticon"_L1)
                    {
                        const QString logoId{static_cast<const char *>(value.second)};

                        // We prefer to use the .jpg but will fall back to the .ico if the .jpg is available
                        if (QFileInfo fi{u"%1/appcache/librarycache/%2/%3.jpg"_s.arg(steamRoot, m_id, logoId)}; fi.exists())
                            m_icon = "file://"_L1 + fi.absoluteFilePath();
                        else if (QFileInfo fi{u"%1/steam/games/%2.ico"_s.arg(steamRoot, logoId)};
                                 fi.exists() && !m_icon.isEmpty())
                            m_icon = "file://"_L1 + fi.absoluteFilePath();
                    }
                    else if ((key == "openvrsupport"_L1 || key == "openxrsupport"_L1) && !m_features.testFlag(Feature::VR))
                        m_features.setFlag(Feature::VR, parseInt(value.first, value.second));
                    else if (key == "onlyvrsupport"_L1 && !m_features.testFlag(Feature::VR))
                    {
                        if (bool vrOnly = parseInt(value.first, value.second); vrOnly)
                        {
                            m_features.setFlag(Feature::VR);
                            m_features.setFlag(Feature::Flatscreen, false);
                        }
                    }
                }
            }
            else if (section.name == "appinfo.extended")
            {
                for (const auto &[key, value] : std::as_const(section.keys))
                {
                    if (key == "vacmacmodulecache"_L1 || key == "vacmodulecache"_L1 || key == "vacmodulefilename"_L1)
                        m_features.setFlag(Feature::Anticheat);
                }
            }
        }

        detectGameEngine();
        detectArchitectures();
        detectAnticheat();

        m_valid = m_executables.size() > 0;
    }

    Store store() const override { return Store::Steam; }

    void launch() const override
    {
        qCInfo(SteamLog) << "Launching" << m_id;
        const auto url = "steam://launch/"_L1 + m_id;
        if (m_flatpakAppId.isEmpty())
            QDesktopServices::openUrl(url);
        else if (!QProcess::startDetached("flatpak"_L1, {"run"_L1, m_flatpakAppId, url}))
            qCWarning(SteamLog) << "Could not launch" << m_id << "through Flatpak Steam";
    }
};

QVariantList Steam::libraries() const
{
    if (m_installs.isEmpty())
        return {QVariantMap{{"store"_L1, "Steam"_L1}, {"path"_L1, QString{}}, {"count"_L1, 0}}};

    QVariantList rows;
    for (const auto &install : std::as_const(m_installs))
        rows << QVariantMap{{"store"_L1, install.name}, {"path"_L1, install.path}, {"count"_L1, install.count}};
    return rows;
}

void Steam::discover(bool report)
{
    struct Candidate
    {
        QString path;
        QString flatpakAppId;
        QString name;
    };
    const QList<Candidate> candidates = {
        // ~/.steam comes first since it *should* point to the active native install
        {QDir::homePath() + "/.steam/steam"_L1, {}, "Steam"_L1},
        {QDir::homePath() + "/.local/share/Steam"_L1, {}, "Steam"_L1},
        // Debian ships a Steam installer script that uses this path for some weird reason
        {QDir::homePath() + "/.steam/debian-installation"_L1, {}, "Steam"_L1},
        {QDir::homePath() + "/.var/app/"_L1 + Flatpak::SteamAppId + "/data/Steam"_L1,
         Flatpak::SteamAppId,
         "Steam (Flatpak)"_L1},
        // Snap Steam stays unsupported: there is no unprivileged way to enter its sandbox.
    };

    QList<Install> found;
    QStringList seen;
    for (const auto &candidate : candidates)
    {
        const QFileInfo info{candidate.path};
        if (!info.exists() || !info.isDir())
            continue;
        const auto canonical = info.canonicalFilePath();
        if (canonical.isEmpty() || seen.contains(canonical))
            continue;
        seen << canonical;

        Install install;
        install.path = canonical;
        install.flatpakAppId = candidate.flatpakAppId;
        install.name = candidate.name;
        found << install;
    }

    m_steamRoot.clear();
    for (const auto &install : found)
    {
        if (install.flatpakAppId.isEmpty())
        {
            m_steamRoot = install.path;
            break;
        }
    }
    if (m_steamRoot.isEmpty() && !found.isEmpty())
        m_steamRoot = found.constFirst().path;

    auto changed = found.size() != m_installs.size();
    for (int i = 0; !changed && i < found.size(); ++i)
        changed = found.at(i).path != m_installs.at(i).path || found.at(i).flatpakAppId != m_installs.at(i).flatpakAppId;
    if (changed)
        m_installs = found;

    if (!report && !changed)
        return;
    if (m_installs.isEmpty())
        qCInfo(SteamLog) << "Steam not found";
    else
        for (const auto &install : std::as_const(m_installs))
            qCInfo(SteamLog) << "Found" << install.name << "at" << install.path;
}

Steam::Steam(QObject *parent)
    : Store{parent}
{
    discover(true);
}

Steam *Steam::instance()
{
    static auto s = new Steam;
    return s;
}

Steam *Steam::create(QQmlEngine *qml, QJSEngine *js)
{
    return instance();
}

void Steam::launchSteamVR()
{
    const Install *flatpakOnly = nullptr;
    for (const auto &install : std::as_const(m_installs))
    {
        if (!install.hasSteamVR)
            continue;
        if (install.flatpakAppId.isEmpty())
        {
            QDesktopServices::openUrl({"steam://run/250820"_L1});
            return;
        }
        flatpakOnly = &install;
    }
    if (flatpakOnly != nullptr)
    {
        if (!QProcess::startDetached("flatpak"_L1, {"run"_L1, flatpakOnly->flatpakAppId, "steam://run/250820"_L1}))
            qCWarning(SteamLog) << "Could not launch SteamVR through Flatpak Steam";
        return;
    }
    QDesktopServices::openUrl({"steam://run/250820"_L1});
}

void Steam::scanStore()
{
    discover(false);

    qCDebug(SteamLog) << "Scanning Steam library";
    beginResetModel();

    for (const auto game : std::as_const(m_games))
        game->deleteLater();
    m_games.clear();
    m_hasSteamVR = false;

    for (const auto &install : std::as_const(m_installs))
        AppInfoVDF::load(install.path + "/appcache/appinfo.vdf"_L1);

    const auto parseLibraryFolders = [this](const QString &vdfPath, Install &install) -> bool {
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
                                               install.path,
                                               install.flatpakAppId,
                                               this};
                        g->isValid())
                    {
                        m_games.push_back(g);
                        ++install.count;
                        if (g->id() == "250820"_L1)
                        {
                            install.hasSteamVR = true;
                            m_hasSteamVR = true;
                        }
                    }
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

    for (auto &install : m_installs)
    {
        install.count = 0;
        install.hasSteamVR = false;
        bool parsed = false;
        if (const QFileInfo fi{install.path + "/steamapps/libraryfolders.vdf"_L1}; fi.exists() && fi.isFile())
            parsed = parseLibraryFolders(fi.absoluteFilePath(), install);
        if (!parsed)
        {
            if (const QFileInfo fi{install.path + "/config/libraryfolders.vdf"_L1}; fi.exists() && fi.isFile())
                parsed = parseLibraryFolders(fi.absoluteFilePath(), install);
        }
        if (!parsed)
            qCWarning(SteamLog) << "Could not find libraryfolders.vdf in" << install.path;
    }

    endResetModel();
    emit hasSteamVRChanged(m_hasSteamVR);
    emit librariesChanged();
}

#include "Steam.moc"
