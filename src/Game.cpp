#include "Game.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include "Aptabase.h"
#include "Dotnet.h"
#include "VDF.h"
#include "stores/Steam.h"
#include "vdf_parser.hpp"

using namespace Qt::Literals;

Game::Game(QObject *parent)
    : QObject{parent}
{
}

Game *Game::fromSteam(const QString &steamId, const QString &steamDrive, QObject *parent)
{
    auto g = new Game{parent};
    g->m_id = steamId;
    g->m_store = Store::Steam;
    g->m_canLaunch = true;
    g->m_canOpenSettings = true;

    const QString acfPath = "%1/steamapps/appmanifest_%2.acf"_L1.arg(steamDrive, g->m_id);
    try
    {
        std::ifstream acfFile{acfPath.toStdString()};
        auto app = tyti::vdf::read(acfFile);

        g->m_name = QString::fromStdString(app.attribs["name"]);
        g->m_installDir = steamDrive + "/steamapps/common/"_L1 + QString::fromStdString(app.attribs["installdir"]);
        if (app.attribs.contains("LastPlayed"))
            g->m_lastPlayed = QDateTime::fromSecsSinceEpoch(std::stoi(app.attribs["LastPlayed"]));
    }
    catch (const std::length_error &e)
    {
        qDebug() << "Failure while parsing game from libraryfolders.vdf:" << e.what();
        auto parts = acfPath.split('/');
        Aptabase::instance()->track("failure-parsing-game-libraryfolders-bug"_L1,
                                    {{"which"_L1, parts.size() >= 2 ? parts[parts.size() - 2] : ""_L1}});
    }

    const auto imageDir = Steam::instance()->steamRoot() + "/appcache/librarycache/"_L1 + g->m_id;
    QDirIterator images{imageDir, QDirIterator::Subdirectories};
    while (images.hasNext())
    {
        images.next();
        if ((images.fileName() == "library_600x900.jpg"_L1 || images.fileName() == "library_capsule.jpg"_L1) &&
                g->m_cardImage.isEmpty())
            g->m_cardImage = "file://"_L1 + images.filePath();
        else if (images.fileName() == "library_hero.jpg"_L1 && g->m_heroImage.isEmpty())
            g->m_heroImage = "file://"_L1 + images.filePath();
        else if (images.fileName() == "logo.png"_L1 && g->m_logoImage.isEmpty())
            g->m_logoImage = "file://"_L1 + images.filePath();
    }

    QString compatdata = steamDrive + "/steamapps/compatdata/"_L1 + g->m_id;
    g->m_protonPrefix = compatdata + "/pfx"_L1;
    if (QFileInfo fi{compatdata}; fi.exists() && fi.isDir())
    {
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
                    g->m_protonBinary = protonBase + "/files/bin/wine"_L1;
                else
                    g->m_protonBinary = protonBase + "/dist/bin/wine"_L1;
            }
        }
    }

    auto *info = AppInfoVDF::instance()->game(g->m_id.toInt());
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
            if (!g->m_executables.contains(id))
                g->m_executables[id] = {};
            for (const auto &[key, value] : std::as_const(section.keys))
            {
                if (key == "executable"_L1)
                    g->m_executables[id].executable = g->m_installDir + '/' + static_cast<const char *>(value.second);
                else if (key == "type"_L1)
                    g->m_executables[id].type = static_cast<const char *>(value.second);
                else if (key == "oslist"_L1)
                {
                    const QString os = static_cast<const char *>(value.second);
                    if (os.contains("windows"_L1))
                        g->m_executables[id].platform |= LaunchOption::Platform::Windows;
                    if (os.contains("linux"_L1))
                        g->m_executables[id].platform |= LaunchOption::Platform::Linux;
                    if (os.contains("macos"_L1))
                        g->m_executables[id].platform |= LaunchOption::Platform::MacOS;
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
                    g->m_logoWidth = parseDouble(value.first, value.second);
                else if (key == "height_pct"_L1)
                    g->m_logoHeight = parseDouble(value.first, value.second);
                else if (key == "pinned_position"_L1)
                {
                    QString posStr = static_cast<char *>(value.second);
                    if (posStr.startsWith("Center"_L1))
                        g->m_logoVPosition = LogoPosition::Center;
                    else if (posStr.startsWith("Top"_L1))
                        g->m_logoVPosition = LogoPosition::Top;
                    else if (posStr.startsWith("Bottom"_L1))
                        g->m_logoVPosition = LogoPosition::Bottom;

                    if (posStr.endsWith("Center"_L1))
                        g->m_logoHPosition = LogoPosition::Center;
                    else if (posStr.endsWith("Left"_L1))
                        g->m_logoHPosition = LogoPosition::Left;
                    else if (posStr.endsWith("Right"_L1))
                        g->m_logoHPosition = LogoPosition::Right;
                }
            }
        }
        else if (section.name == "appinfo.common")
        {
            for (const auto &[key, value] : std::as_const(section.keys))
            {
                if (key == "type"_L1)
                {
                    QString type{static_cast<const char *>(value.second)};
                    type = type.toLower();
                    if (type == "game"_L1)
                        g->m_type = AppType::Game;
                    else if (type == "application"_L1)
                        g->m_type = AppType::App;
                    else if (type == "tool"_L1)
                        g->m_type = AppType::Tool;
                    else if (type == "demo"_L1)
                        g->m_type = AppType::Demo;
                    else if (type == "music"_L1)
                        g->m_type = AppType::Music;
                }
                else if (key == "icon"_L1)
                {
                    const QString logoId{static_cast<const char *>(value.second)};
                    if (QFileInfo fi{u"%1/appcache/librarycache/%2/%3.jpg"_s.arg(
                                Steam::instance()->steamRoot(), g->m_id, logoId)};
                            fi.exists())
                        g->m_icon = "file://"_L1 + fi.absoluteFilePath();
                    // TODO: could also extract clienticon key to find icons in $steamroot/steam/games
                }
                else if (key == "openvrsupport"_L1 || key == "openxrsupport"_L1)
                    g->m_supportsVr |= parseInt(value.first, value.second);
                else if (key == "onlyvrsupport"_L1)
                {
                    g->m_vrOnly = parseInt(value.first, value.second);
                    g->m_supportsVr |= g->m_vrOnly;
                }
            }
        }
    }

    g->detectGameEngine();
    return g;
}

Game *Game::fromItch(const QString &installPath, QObject *parent)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
    {
        qDebug() << "Itch failed to create temporary directory";
        Aptabase::instance()->track("itch-failed-tempdir-bug"_L1);
        return nullptr;
    }

    const QString zipPath = installPath + "/.itch/receipt.json.gz"_L1;
    QProcess unzipJsonProc;
    unzipJsonProc.setWorkingDirectory(tempDir.path());
    QStringList args;
    args << "-c"_L1 << zipPath;
    unzipJsonProc.start("gunzip"_L1, args);
    unzipJsonProc.waitForFinished();
    if (unzipJsonProc.exitCode() != 0)
    {
        qDebug() << "Unzip Itch info failed:" << unzipJsonProc.errorString();
        qDebug() << unzipJsonProc.readAllStandardError();
        Aptabase::instance()->track("itch-failed-unzip-bug"_L1);
        return nullptr;
    }

    const auto json = QJsonDocument::fromJson(unzipJsonProc.readAllStandardOutput());
    const auto game = json["game"_L1].toObject();

    auto g = new Game{parent};
    g->m_store = Store::Itch;

    g->m_id = game["id"_L1].toString();
    g->m_name = game["title"_L1].toString();
    g->m_installDir = installPath;
    g->m_cardImage = "image://itch-image/"_L1 + game["coverUrl"_L1].toString();
    g->m_heroImage = g->m_cardImage;
    g->m_icon = g->m_cardImage;

    QProcess whichWineProc;
    whichWineProc.start("which"_L1, {"wine"_L1});
    whichWineProc.waitForFinished();
    if (whichWineProc.exitCode() == 0)
        g->m_protonBinary = whichWineProc.readAllStandardOutput().trimmed();
    g->m_protonPrefix = qEnvironmentVariable("WINEPREFIX", QDir::homePath() + "/.wine"_L1);

    if (const auto type = game["classification"_L1]; type == "game"_L1)
        g->m_type = Game::AppType::Game;
    // Itch refers to generic apps as tools, but we normally use Tools for things like Proton
    else if (type == "tool"_L1)
        g->m_type = Game::AppType::App;
    else if (type == "soundtrack"_L1)
        g->m_type = Game::AppType::Music;

    auto entries = QDir{installPath}.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    entries.removeIf([](const QFileInfo &fi) { return fi.baseName() == ".itch"_L1; });
    if (entries.size() == 1)
        entries = QDir{entries.first().absoluteFilePath()}.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    // TODO: better binary detection algorithm than this!
    for (const auto &entry : std::as_const(entries))
    {
        if (entry.suffix() == "exe"_L1)
        {
            LaunchOption lo;
            lo.executable = entry.absoluteFilePath();
            lo.platform |= LaunchOption::Platform::Windows;
            g->m_executables[0] = lo;
        }
    }

    g->detectGameEngine();
    return g;
}

Game *Game::fromHeroic(const QString &installPath, QObject *parent)
{
    auto g = new Game{parent};
    g->m_store = Store::Heroic;
    return g;
}

bool Game::protonPrefixExists() const
{
    return QFileInfo::exists(m_protonPrefix);
}

bool Game::hasLinuxBinary() const
{
    for (const auto &exe : m_executables)
        if (exe.platform.testFlag(LaunchOption::Platform::Linux))
            return true;
    return false;
}

bool Game::dotnetInstalled() const
{
    return Dotnet::instance()->isDotnetInstalled(this);
}

void Game::detectGameEngine()
{
    QString binaryDir{m_installDir};
    for (const auto &exe : std::as_const(m_executables))
    {
        if (QFileInfo fi{exe.executable}; fi.absolutePath() != m_installDir)
        {
            binaryDir = fi.absolutePath();
            break;
        }
    }

    // =======================================
    // Unreal detection
    //
    // Detection method sourced from Rai Pal
    // https://github.com/Raicuparta/rai-pal/blob/51157fdae6b1d87760580d85082ccd5026bb0320/backend/core/src/game_engines/unreal.rs
    const QStringList signsOfUnreal = {
        binaryDir + "/Engine/Binaries/Win64"_L1,
        binaryDir + "/Engine/Binaries/Win32"_L1,
        binaryDir + "/Engine/Binaries/ThirdParty"_L1,
    };
    for (const auto &sign : signsOfUnreal)
    {
        if (QFileInfo fi{sign}; fi.exists() && fi.isDir())
        {
            m_engine = Engine::Unreal;
            return;
        }
    }

    // =======================================
    // Source detection
    //
    // Regex sourced from SteamDB
    // https://github.com/SteamDatabase/FileDetectionRuleSets/blob/ac27c7cfc0a63dc07cc9e65157841857d82f347b/rules.ini#L191
    static const QRegularExpression signsOfSource{R"((?:^|/)(?:vphysics|bsppack)\.(?:dylib|dll|so)$)"_L1};
    for (QDirIterator gameDirIterator{binaryDir, QDirIterator::Subdirectories}; gameDirIterator.hasNext();)
    {
        auto localName = gameDirIterator.next().remove(binaryDir);
        if (localName.contains(signsOfSource))
        {
            m_engine = Engine::Source;
            return;
        }
    }

    // =======================================
    // Unity detection
    //
    // Detection method sourced from Rai Pal
    // https://github.com/Raicuparta/rai-pal/blob/51157fdae6b1d87760580d85082ccd5026bb0320/backend/core/src/game_engines/unity.rs
    for (const auto &e : std::as_const(m_executables))
    {
        QFileInfo exe{e.executable};
        if (QFileInfo dataDir{exe.absolutePath() + '/' + exe.baseName() + "_Data"_L1}; dataDir.exists() && dataDir.isDir())
        {
            m_engine = Engine::Unity;
            return;
        }
    }

    // =======================================
    // Godot detection
    //
    // Dectection method sourced from SteamDB
    // https://github.com/SteamDatabase/FileDetectionRuleSets/blob/ac27c7cfc0a63dc07cc9e65157841857d82f347b/tests/FileDetector.php#L316
    for (const auto &e : std::as_const(m_executables))
    {
        QFileInfo exe{e.executable};
        QDirIterator pckFinder{exe.absolutePath()};
        while (pckFinder.hasNext())
        {
            pckFinder.next();
            if (pckFinder.fileName().toLower() == exe.baseName().toLower() + ".pck"_L1)
            {
                m_engine = Engine::Godot;
                return;
            }
        }
    }

    // fall back to looking for a single data.pck file
    QStringList pcks;
    for (QDirIterator dirit{binaryDir, QDirIterator::Subdirectories}; dirit.hasNext();)
    {
        const auto file = dirit.next();
        if (file.endsWith(".pck"_L1, Qt::CaseInsensitive))
            pcks.push_back(file);
    }
    if (pcks.size() == 1 && pcks.first().endsWith("/data.pck"_L1, Qt::CaseInsensitive))
    {
        m_engine = Engine::Godot;
        return;
    }
}
