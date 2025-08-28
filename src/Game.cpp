#include "Game.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

#include "Dotnet.h"

using namespace Qt::Literals;

Game::Game(QObject *parent)
    : QObject{parent}
{}

bool Game::hasValidWine() const
{
    if (m_wineBinary.isEmpty() || m_winePrefix.isEmpty())
        return false;
    if (QFileInfo wb{m_wineBinary}; !wb.exists() || !wb.isFile())
        return false;
    if (QFileInfo wp{m_winePrefix}; !wp.exists() || !wp.isFile())
        return false;

    return true;
}

bool Game::hasMultiplePlatforms() const
{
    if (m_executables.isEmpty())
        return false;

    LaunchOption::Platforms prev = m_executables.first().platform;
    for (const auto &exe : m_executables)
    {
        if (exe.platform != prev)
            return true;
        prev = exe.platform;
    }
    return false;
}

bool Game::noWindowsSupport() const
{
    return std::all_of(m_executables.begin(), m_executables.end(), [](const auto &exe) {
        return !exe.platform.testFlag(LaunchOption::Platform::Windows);
    });
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

    // Unity fallback: if the crash handler exists, it's a dead giveaway
    for (QDirIterator gameDirIterator{m_installDir, QDirIterator::Subdirectories}; gameDirIterator.hasNext();)
    {
        gameDirIterator.next();
        if (gameDirIterator.fileName() == "UnityCrashHandler64.exe"_L1 ||
            gameDirIterator.fileName() == "UnityCrashHandler32.exe"_L1)
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
        for (QDirIterator pckFinder{exe.absolutePath()}; pckFinder.hasNext();)
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

void Game::detectAnticheat()
{
    // Rules from
    // https://github.com/SteamDatabase/FileDetectionRuleSets/blob/1e4ec6197ab40fcd3706e09166acaccc96f7e5d7/rules.ini#L238
    static const QList<QRegularExpression> anticheats = {
        QRegularExpression{R"((?:^|/)AntiCheatExpert/)"_L1},
        QRegularExpression{R"((?:^|/)AceAntibotClient/)"_L1},
        QRegularExpression{R"((?:^|/)anybrainSDK\.dll$)"_L1},
        QRegularExpression{R"((?:^|/)BEService(?:_x64)?\.exe$)"_L1},
        QRegularExpression{R"((?:^|/)BlackCall(?:64)?\.aes$)"_L1},
        QRegularExpression{R"((?:^|/)BlackCat64\.sys$)"_L1},
        QRegularExpression{R"((?:^|/)EasyAntiCheat_(?:EOS_)?Setup\.exe$)"_L1},
        QRegularExpression{R"((?:^|/)(?:EasyAntiCheat(?:_x64)?|eac_server64)\.dll$)"_L1},
        QRegularExpression{R"((?:^|/)EAAntiCheat\.Installer\.exe$)"_L1},
        QRegularExpression{R"((?:^|/)equ8_conf\.json$)"_L1},
        QRegularExpression{R"((?:^|/)FredaikisAntiCheat/)"_L1},
        QRegularExpression{R"((?:^|/)HShield/HSInst\.dll$)"_L1},
        QRegularExpression{R"((?:^|/)gameguard\.des$)"_L1},
        QRegularExpression{R"((?:^|/)(?:PnkBstrA|pbsvc)\.exe$)"_L1},
        QRegularExpression{R"((?:^|/)pbsv\.dll$)"_L1},
        QRegularExpression{R"((?:^|/)Punkbuster(?:$|/))"_L1},
        QRegularExpression{R"((?:^|/)Randgrid\.sys$)"_L1},
        QRegularExpression{R"((?:^|/)TP3Helper\.exe$)"_L1},
        QRegularExpression{R"(\.xem$)"_L1},
    };

    for (QDirIterator gameDirIterator{m_installDir, QDirIterator::Subdirectories}; gameDirIterator.hasNext();)
    {
        auto localName = gameDirIterator.next().remove(m_installDir);
        for (const auto &anticheat : anticheats)
        {
            if (localName.contains(anticheat))
            {
                m_hasAnticheat = true;
                return;
            }
        }
    }
}
