#include "Steam.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QSysInfo>
#include <QVariant>

#include "Aptabase.h"
#include "Flatpak.h"
#include "VDF.h"
#include "vdf_parser.hpp"

Q_LOGGING_CATEGORY(SteamLog, "steam")

namespace
{
    // What Steam runs a game with when that isn't simply the game's own Linux build. The player's choices are in
    // config.vdf; the list of tools and Valve's per-game defaults are in the appinfo of the Steam Play manifests app.
    struct CompatTools
    {
        struct Tool
        {
            QString displayName;
            // False for the Steam Linux Runtime containers, which run a game's Linux build
            bool runsWindows = true;
        };

        // Keyed by lowercased internal name, aliases included
        QHash<QString, Tool> tools;
        // App id to internal tool name or alias
        QHash<QString, QString> chosen;
        QHash<QString, QString> valveDefaults;

        Tool tool(const QString &name) const
        {
            if (const auto it = tools.constFind(name.toLower()); it != tools.cend())
                return *it;
            // Not one of Valve's, so a custom build such as GE-Proton
            return {name, !name.startsWith("steamlinuxruntime"_L1, Qt::CaseInsensitive)};
        }
    };

    constexpr int SteamPlayManifests = 891390;

    CompatTools readValveCompatTools()
    {
        CompatTools compat;
        auto *info = AppInfoVDF::instance()->game(SteamPlayManifests);
        if (!info)
        {
            qCWarning(SteamLog) << "No Steam Play manifests in appinfo.vdf. Only compatibility tools chosen in a game's"
                                << "Steam properties will be detected";
            return compat;
        }

        AppInfoVDF::AppInfo::Section section;
        AppInfoVDF::AppInfo::SectionDesc desc{};
        desc.blob = info->getRootSection(&desc.size);
        section.parse(desc);

        const auto toolsPrefix = "appinfo.extended.compat_tools."_L1;
        const auto mappingsPrefix = "appinfo.extended.app_mappings."_L1;
        for (const auto &finished : std::as_const(section.finished_sections))
        {
            if (finished.name.startsWith(toolsPrefix))
            {
                QStringList names{finished.name.sliced(toolsPrefix.size())};
                CompatTools::Tool tool{names.constFirst()};
                for (const auto &[key, value] : std::as_const(finished.keys))
                {
                    if (value.first != AppInfoVDF::AppInfo::Section::String)
                        continue;
                    const QString text = static_cast<const char *>(value.second);
                    if (key == "display_name"_L1)
                        tool.displayName = text;
                    else if (key == "from_oslist"_L1)
                        tool.runsWindows = text.contains("windows"_L1);
                    else if (key == "aliases"_L1)
                        names += text.split(','_L1, Qt::SkipEmptyParts);
                }
                for (const auto &name : std::as_const(names))
                    compat.tools.insert(name.toLower(), tool);
            }
            else if (finished.name.startsWith(mappingsPrefix))
            {
                for (const auto &[key, value] : std::as_const(finished.keys))
                {
                    if (key != "tool"_L1 || value.first != AppInfoVDF::AppInfo::Section::String)
                        continue;
                    // Valve leaves entries with an empty tool behind when it withdraws a mapping
                    if (const QString tool = static_cast<const char *>(value.second); !tool.isEmpty())
                        compat.valveDefaults.insert(finished.name.sliced(mappingsPrefix.size()), tool);
                }
            }
        }

        qCInfo(SteamLog) << "Steam Play manifests list" << compat.tools.size() << "compatibility tool names and"
                         << compat.valveDefaults.size() << "per-game defaults";
        return compat;
    }

    void readChosenCompatTools(const QString &steamRoot, CompatTools &compat)
    {
        const auto path = steamRoot + "/config/config.vdf"_L1;
        std::ifstream file{path.toStdString()};
        bool parsed = false;
        const auto config = file.is_open() ? tyti::vdf::read(file, &parsed) : tyti::vdf::object{};
        if (!parsed)
        {
            qCWarning(SteamLog) << "Could not read" << path << "- compatibility tools chosen in Steam will not be detected";
            return;
        }

        // The capitalization of these keys differs between Steam installs
        const auto *node = &config;
        for (const auto name : {"Software"_L1, "Valve"_L1, "Steam"_L1, "CompatToolMapping"_L1})
        {
            const auto child = std::find_if(node->childs.cbegin(), node->childs.cend(), [name](const auto &entry) {
                return QString::fromStdString(entry.first).compare(name, Qt::CaseInsensitive) == 0;
            });
            if (child == node->childs.cend())
            {
                qCWarning(SteamLog) << "No" << name << "section in" << path
                                    << "- compatibility tools chosen in Steam will not be detected";
                return;
            }
            node = child->second.get();
        }

        for (const auto &[appId, mapping] : node->childs)
        {
            // Turning the override off again can leave an entry with an empty name
            if (const auto name = mapping->attribs.find("name"); name != mapping->attribs.cend() && !name->second.empty())
                compat.chosen.insert(QString::fromStdString(appId), QString::fromStdString(name->second));
        }

        // App 0 is the tool for every game that has no entry of its own and no Linux build
        qCInfo(SteamLog) << path << "sets a compatibility tool for" << compat.chosen.size() - compat.chosen.contains("0"_L1)
                         << "games, default" << compat.chosen.value("0"_L1, "unset"_L1);
    }
} // namespace

class SteamGame : public Game
{
    Q_OBJECT

public:
    SteamGame(const QString &steamId,
              const QString &steamDrive,
              const QStringList &libraries,
              const QString &steamRoot,
              const QString &flatpakAppId,
              const CompatTools &compat,
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

        findPrefix(steamDrive, libraries);

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
                        m_executables[id].executable =
                            resolveWindowsPath(m_installDir, static_cast<const char *>(value.second));
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

        // A launch option can be marked for Linux and still name a Windows executable, which then runs through Proton.
        // That doesn't make a Linux build.
        for (auto &exe : m_executables)
            if (exe.executable.endsWith(".exe"_L1, Qt::CaseInsensitive))
                exe.platform = Platform::Windows;

        detectWindowsBuild(compat);
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

private:
    void findPrefix(const QString &steamDrive, const QStringList &libraries)
    {
        // The prefix is normally in the library the game is installed to, but that isn't guaranteed. Search every library
        // and take the prefix Proton ran in most recently; it rewrites the version file on each launch.
        QStringList drives{steamDrive};
        for (const auto &library : libraries)
            if (!drives.contains(library))
                drives << library;

        QString compatdata;
        QDateTime lastUsed;
        QStringList looked;
        for (const auto &drive : std::as_const(drives))
        {
            const auto dir = drive + "/steamapps/compatdata/"_L1 + m_id;
            if (!QFileInfo{dir + "/pfx"_L1}.isDir())
            {
                // Steam also makes an empty compatdata folder for games that never ran through Proton
                looked << dir + (QFileInfo::exists(dir) ? " (no pfx inside)"_L1 : " (missing)"_L1);
                continue;
            }
            const auto used = QFileInfo{dir + "/version"_L1}.lastModified();
            if (compatdata.isEmpty() || used > lastUsed)
            {
                compatdata = dir;
                lastUsed = used;
            }
        }

        if (compatdata.isEmpty())
        {
            m_winePrefix = steamDrive + "/steamapps/compatdata/"_L1 + m_id + "/pfx"_L1;
            qCDebug(SteamLog) << "No Proton prefix for" << m_name << "- looked in" << looked;
            return;
        }
        m_winePrefix = compatdata + "/pfx"_L1;

        // Proton records what it set the prefix up with in config_info. The second line is the fonts directory of that
        // Proton build, and a later one is the default prefix it started from.
        QFile file{compatdata + "/config_info"_L1};
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            // Steam's install scripts can start a prefix before Proton has ever run in it
            if (!QFileInfo::exists(m_winePrefix + "/system.reg"_L1))
                qCDebug(SteamLog) << "Proton has not set up the prefix for" << m_name << "at" << m_winePrefix << "yet";
            else
                qCWarning(SteamLog) << "Proton prefix for" << m_name << "at" << m_winePrefix
                                    << "has no Wine to go with it: could not read config_info:" << file.errorString();
            return;
        }
        const auto lines = QString::fromUtf8(file.readAll()).split('\n'_L1);
        static const QRegularExpression re("/(files|dist)/share/fonts/$");
        auto proton = lines.value(1);
        if (!proton.contains(re))
        {
            qCWarning(SteamLog) << "Proton prefix for" << m_name << "at" << m_winePrefix
                                << "has no Wine to go with it: config_info does not name a Proton build, it starts with"
                                << lines.first(qMin(3, lines.size()));
            return;
        }
        proton.remove(re);

        // On arm64 (the Steam Frame), Proton runs files/bin-arm64/wine when it ships one and then starts the prefix from
        // default_pfx_arm64. See Proton.__init__ in its proton script. Use the Wine the prefix was made for.
        const auto defaultPrefix = std::find_if(
            lines.cbegin(), lines.cend(), [](const QString &line) { return line.contains("/share/default_pfx"_L1); });
        const bool arm64 = defaultPrefix != lines.cend() ? defaultPrefix->endsWith("/default_pfx_arm64/"_L1) :
                                                           QSysInfo::currentCpuArchitecture() == "arm64"_L1;

        QStringList candidates{proton + "/files/bin/wine"_L1, proton + "/dist/bin/wine"_L1};
        if (arm64)
            candidates.prepend(proton + "/files/bin-arm64/wine"_L1);
        for (const auto &candidate : std::as_const(candidates))
        {
            if (QFileInfo{candidate}.isFile())
            {
                m_wineBinary = candidate;
                break;
            }
        }

        if (m_wineBinary.isEmpty())
            qCWarning(SteamLog) << "Proton prefix for" << m_name << "at" << m_winePrefix
                                << "has no Wine to go with it: config_info names" << proton
                                << "but none of these exist:" << candidates;
        else
            qCDebug(SteamLog) << "Found Proton prefix for" << m_name << "at" << m_winePrefix << "with Wine" << m_wineBinary;
    }

    // Steam starts a game's Linux build unless a compatibility tool is set for that game, by the player in its properties
    // or by Valve. Steam then swaps the installed files where the two builds don't share them.
    void detectWindowsBuild(const CompatTools &compat)
    {
        if (!hasLinuxBuild() || noWindowsSupport())
            return;

        const auto installed = [this](Platform platform) {
            return std::any_of(m_executables.cbegin(), m_executables.cend(), [platform](const LaunchOption &exe) {
                return exe.platform == platform && QFileInfo::exists(exe.executable);
            });
        };
        const bool windowsBuild = installed(Platform::Windows);
        const bool linuxBuild = installed(Platform::Linux);

        // The player's choice wins. Valve's default only settles it when the installed files don't.
        auto name = compat.chosen.value(m_id);
        const bool byPlayer = !name.isEmpty();
        if (!byPlayer && windowsBuild == linuxBuild)
            name = compat.valveDefaults.value(m_id);

        if (!name.isEmpty())
        {
            if (const auto tool = compat.tool(name); tool.runsWindows)
                m_windowsBuildReason = "Steam is set to use %1 instead of the Linux build."_L1.arg(tool.displayName);
        }
        else if (windowsBuild && !linuxBuild)
            m_windowsBuildReason = "Steam installed the Windows build instead of the Linux one."_L1;

        if (!name.isEmpty())
            name += byPlayer ? " (Steam properties)"_L1 : " (Valve default)"_L1;
        qCDebug(SteamLog) << m_name << "has a Linux build. Compatibility tool:" << (name.isEmpty() ? "none"_L1 : name)
                          << "Windows build installed:" << windowsBuild << "Linux build installed:" << linuxBuild
                          << (m_windowsBuildReason.isEmpty() ? "-> runs the Linux build" : "-> runs the Windows build");
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
    countAsLauncher();
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

void Steam::prepareScan()
{
    discover(false);
}

bool Steam::readLibrary(QList<Game *> &games)
{
    qCDebug(SteamLog) << "Scanning Steam library";

    auto installs = m_installs;
    auto hasSteamVR = false;

    for (const auto &install : std::as_const(installs))
        AppInfoVDF::load(install.path + "/appcache/appinfo.vdf"_L1);

    const auto valveCompat = readValveCompatTools();

    const auto parseLibraryFolders =
        [&games, &hasSteamVR](const QString &vdfPath, Install &install, const CompatTools &compat) -> bool {
        qCDebug(SteamLog) << "Parsing libraryfolders.vdf from" << vdfPath;
        std::ifstream vdfFile{vdfPath.toStdString()};

        try
        {
            auto libraryFolders = tyti::vdf::read(vdfFile);
            QStringList libraries;
            for (const auto &[_, folder] : libraryFolders.childs)
                libraries << QString::fromStdString(folder->attribs["path"]);
            for (const auto &[_, folder] : libraryFolders.childs)
            {
                qCDebug(SteamLog) << "Scanning Steam drive:" << folder->attribs["path"];
                for (const auto &[appId, _] : folder->childs["apps"]->attribs)
                {
                    if (auto g = new SteamGame{QString::fromStdString(appId),
                                               QString::fromStdString(folder->attribs["path"]),
                                               libraries,
                                               install.path,
                                               install.flatpakAppId,
                                               compat,
                                               nullptr};
                        g->isValid())
                    {
                        games.push_back(g);
                        ++install.count;
                        if (g->id() == "250820"_L1)
                        {
                            install.hasSteamVR = true;
                            hasSteamVR = true;
                        }
                    }
                    else
                        delete g;
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

    for (auto &install : installs)
    {
        install.count = 0;
        install.hasSteamVR = false;
        auto compat = valveCompat;
        readChosenCompatTools(install.path, compat);
        bool parsed = false;
        if (const QFileInfo fi{install.path + "/steamapps/libraryfolders.vdf"_L1}; fi.exists() && fi.isFile())
            parsed = parseLibraryFolders(fi.absoluteFilePath(), install, compat);
        if (!parsed)
        {
            if (const QFileInfo fi{install.path + "/config/libraryfolders.vdf"_L1}; fi.exists() && fi.isFile())
                parsed = parseLibraryFolders(fi.absoluteFilePath(), install, compat);
        }
        if (!parsed)
            qCWarning(SteamLog) << "Could not find libraryfolders.vdf in" << install.path;
    }

    m_scannedInstalls = installs;
    m_scannedSteamVR = hasSteamVR;
    return true;
}

void Steam::finishScan()
{
    m_installs = m_scannedInstalls;
    m_hasSteamVR = m_scannedSteamVR;
    emit hasSteamVRChanged(m_hasSteamVR);
    emit librariesChanged();
}

#include "Steam.moc"
