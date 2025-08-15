#include "Game.h"

#include <QFileInfo>
#include <QDirIterator>

#include <sourcepp/FS.h>
#include <kvpp/KV1.h>
#include <kvpp/KV1Binary.h>

#include "Dotnet.h"
#include "Steam.h"

using namespace Qt::Literals;

Game::Game(int steamId, QObject *parent)
    : QObject{parent},
      m_id{steamId}
{
    QFile acfFile{Steam::instance()->steamRoot() + u"/steamapps/appmanifest_%1.acf"_s.arg(m_id)};
    acfFile.open(QIODevice::ReadOnly);
    kvpp::KV1 app{acfFile.readAll().toStdString()};
    const auto &appState = app["AppState"];

    m_name = QString::fromStdString(appState["name"].getValue().data());
    m_installDir = Steam::instance()->steamRoot() + "/steamapps/common/"_L1 +
            QString::fromStdString(app["installdir"].getValue().data());
    if (app.hasChild("LastPlayed"))
        m_lastPlayed = QDateTime::fromSecsSinceEpoch(std::stoi(app["LastPlayed"].getValue().data()));

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

    kvpp::KV1Binary appInfo{sourcepp::fs::readFileBuffer(Steam::instance()->steamRoot().toStdString() + "/appcache/"
                                                                                                        "appinfo.vdf")};

    const auto &extendedInfo = appInfo["appinfo.extended"];
    if (extendedInfo.hasChild("sourcegame") && std::get<int32_t>(extendedInfo["sourcegame"].getValue()) == 1)
        m_engine = Engine::Source;

    m_name = QString::fromStdString(appState["name"].getValue().data());
    m_installDir = Steam::instance()->steamRoot() + "/steamapps/common/"_L1 +
            QString::fromStdString(app["installdir"].getValue().data());
    if (app.hasChild("LastPlayed"))
        m_lastPlayed = QDateTime::fromSecsSinceEpoch(std::stoi(app["LastPlayed"].getValue().data()));

    QStringList exes;
    // for (auto &section : section.finished_sections)
    // {
    //     if (section.name.startsWith("appinfo.config.launch."_L1))
    //     {
    //         for (const auto &[key, value] : std::as_const(section.keys))
    //         {
    //             if (key == "executable"_L1)
    //             {
    //                 assert(value.first == AppInfoVDF::AppInfo::Section::String);
    //                 exes.push_back(m_installDir + '/' + static_cast<const char *>(value.second));
    //             }
    //         }
    //     }
    //     else if (section.name == "appinfo.extended"_L1)
    //     {
    //         for (const auto &[key, value] : std::as_const(section.keys))
    //         {
    //             // TODO: this does not detect all Source games (e.g. mods don't detect)
    //             if (key == "sourcegame"_L1 && parseInt(value.first, value.second) == 1)
    //                 m_engine = Engine::Source;
    //         }
    //     }
    //     else if (section.name == "appinfo.common.library_assets.logo_position"_L1)
    //     {
    //         for (const auto &[key, value] : std::as_const(section.keys))
    //         {
    //             if (key == "width_pct"_L1)
    //                 m_logoWidth = parseDouble(value.first, value.second);
    //             else if (key == "height_pct"_L1)
    //                 m_logoHeight = parseDouble(value.first, value.second);
    //             else if (key == "pinned_position"_L1)
    //             {
    //                 QString posStr = static_cast<char *>(value.second);
    //                 if (posStr.startsWith("Center"_L1))
    //                     m_logoVPosition = LogoPosition::Center;
    //                 else if (posStr.startsWith("Top"_L1))
    //                     m_logoVPosition = LogoPosition::Top;
    //                 else if (posStr.startsWith("Bottom"_L1))
    //                     m_logoVPosition = LogoPosition::Bottom;

    //                 if (posStr.endsWith("Center"_L1))
    //                     m_logoHPosition = LogoPosition::Center;
    //                 else if (posStr.endsWith("Left"_L1))
    //                     m_logoHPosition = LogoPosition::Left;
    //                 else if (posStr.endsWith("Right"_L1))
    //                     m_logoHPosition = LogoPosition::Right;
    //             }
    //         }
    //     }
    // }

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
