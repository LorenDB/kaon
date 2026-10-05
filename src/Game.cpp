#include "Game.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

#include "Aptabase.h"

namespace
{
    // e_machine from the header of an ELF file, or 0 if it isn't one
    quint16 elfMachine(QFile &raw)
    {
        QDataStream ds(&raw);
        ds.setByteOrder(QDataStream::LittleEndian); // Initial read is fixed

        // Verify ELF magic number
        raw.seek(0);
        quint32 magic;
        ds >> magic;
        if (magic != 0x464C457F)
            return 0;

        // Read class (32/64 bit)
        raw.seek(4);
        quint8 elfClass;
        ds >> elfClass;
        if (elfClass != 1 && elfClass != 2)
            return 0;

        // Read data encoding (endianness)
        quint8 data;
        ds >> data;
        if (data == 1)
            ds.setByteOrder(QDataStream::LittleEndian);
        else if (data == 2)
            ds.setByteOrder(QDataStream::BigEndian);
        else
            return 0; // invalid encoding

        // Skip to e_machine (offset 18)
        raw.seek(18);
        quint16 machine;
        ds >> machine;
        return machine;
    }
} // namespace

Game::Game(QObject *parent)
    : QObject{parent}
{}

QString Game::resolveWindowsPath(const QString &root, const QString &relative)
{
    auto path = root;
    for (const auto &part : QString{relative}.replace('\\'_L1, '/'_L1).split('/'_L1, Qt::SkipEmptyParts))
    {
        // Only look for another spelling when this one isn't there
        if (!QFileInfo::exists(path + '/' + part))
        {
            const auto entries = QDir{path}.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
            const auto match = std::find_if(entries.cbegin(), entries.cend(), [&part](const QString &entry) {
                return entry.compare(part, Qt::CaseInsensitive) == 0;
            });
            if (match != entries.cend())
            {
                path += '/' + *match;
                continue;
            }
        }
        path += '/' + part;
    }
    return path;
}

bool Game::hasArm64Wine() const
{
    QFile wine{m_wineBinary};
    return wine.open(QFile::ReadOnly) && elfMachine(wine) == 183; // EM_AARCH64
}

bool Game::hasValidWine() const
{
    if (m_wineBinary.isEmpty() || m_winePrefix.isEmpty())
        return false;
    if (QFileInfo wb{m_wineBinary}; !wb.exists() || !wb.isFile())
        return false;
    if (QFileInfo wp{m_winePrefix}; !wp.exists() || !wp.isDir())
        return false;

    return true;
}

bool Game::hasMultiplePlatforms() const
{
    if (m_executables.isEmpty())
        return false;

    Platform prev = m_executables.first().platform;
    for (const auto &exe : m_executables)
    {
        if (exe.platform != prev)
            return true;
        prev = exe.platform;
    }
    return false;
}

bool Game::hasLinuxBuild() const
{
    return std::any_of(
        m_executables.begin(), m_executables.end(), [](const auto &exe) { return exe.platform == Platform::Linux; });
}

bool Game::runsWindowsBuild() const
{
    if (noWindowsSupport())
        return false;
    return !hasLinuxBuild() || !m_windowsBuildReason.isEmpty();
}

bool Game::noWindowsSupport() const
{
    return std::all_of(
        m_executables.begin(), m_executables.end(), [](const auto &exe) { return exe.platform != Platform::Windows; });
}

void Game::detectGameEngine()
{
    // The game's own files can start below the install directory: itch unpacks many games into a single subfolder, and a
    // launch option can point several folders down. Any folder from an executable up to the install directory may be the
    // one the engine's layout is relative to.
    const auto installDir = QDir::cleanPath(m_installDir);
    QStringList roots{installDir};
    for (const auto &exe : std::as_const(m_executables))
    {
        for (auto dir = QFileInfo{exe.executable}.absolutePath(); !roots.contains(dir); dir = QFileInfo{dir}.absolutePath())
        {
            roots << dir;
            // An executable outside the install directory has no path back up to it
            if (!dir.startsWith(installDir + '/'))
                break;
        }
    }

    // =======================================
    // Unreal detection
    //
    // Detection method sourced from Rai Pal
    // https://github.com/Raicuparta/rai-pal/blob/51157fdae6b1d87760580d85082ccd5026bb0320/backend/core/src/game_engines/unreal.rs
    for (const auto &root : std::as_const(roots))
    {
        const QStringList signsOfUnreal = {
            root + "/Engine/Binaries/Win64"_L1,
            root + "/Engine/Binaries/Win32"_L1,
            root + "/Engine/Binaries/ThirdParty"_L1,
        };
        for (const auto &sign : signsOfUnreal)
        {
            if (QFileInfo fi{sign}; fi.exists() && fi.isDir())
            {
                m_engine = Engine::Unreal;
                return;
            }
        }
    }

    // =======================================
    // Source detection
    //
    // Regex sourced from SteamDB
    // https://github.com/SteamDatabase/FileDetectionRuleSets/blob/ac27c7cfc0a63dc07cc9e65157841857d82f347b/rules.ini#L191
    static const QRegularExpression signsOfSource{R"((?:^|/)(?:vphysics|bsppack)\.(?:dylib|dll|so)$)"_L1};
    for (QDirIterator gameDirIterator{m_installDir, QDirIterator::Subdirectories}; gameDirIterator.hasNext();)
    {
        auto localName = gameDirIterator.next().remove(m_installDir);
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
    for (QDirIterator dirit{m_installDir, QDirIterator::Subdirectories}; dirit.hasNext();)
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

void Game::detectArchitectures()
{
    for (auto &exe : m_executables)
    {
        QFile raw{exe.executable};
        if (raw.open(QFile::ReadOnly))
        {
            if (exe.platform == Platform::Windows)
            {
                QDataStream ds(&raw);
                ds.setByteOrder(QDataStream::LittleEndian);

                // Verify DOS header (MZ signature)
                raw.seek(0);
                quint16 dosSig;
                ds >> dosSig;
                if (dosSig != 0x5A4D)
                    continue;

                // Read PE header offset
                raw.seek(0x3C);
                qint32 peOffset;
                ds >> peOffset;

                // Seek to PE header and verify signature
                raw.seek(peOffset);
                quint32 peSig;
                ds >> peSig;
                if (peSig != 0x00004550)
                    continue;

                // Read the Machine field (architecture)
                quint16 machine;
                ds >> machine;

                // Determine architecture
                switch (machine)
                {
                case 0x014C:
                    exe.arch = Architecture::x86;
                    break;
                case 0x8664:
                    exe.arch = Architecture::x64;
                    break;
                // In case we need to start dealing with Arm games:
                // case 0xAA64:
                //     // ARM64
                //     break;
                // case 0x01C0:
                // case 0x01C4:
                //     // ARM
                //     break;
                default:
                    Aptabase::instance()->track(
                        "unknown-pe-architecture-bug"_L1,
                        {{"pe-arch"_L1, machine},
                         {"game-id", m_id},
                         {"executable", exe.executable},
                         {"store", QMetaEnum::fromType<Store>().valueToKey(static_cast<quint64>(store()))}});
                    break;
                }
            }
            else if (exe.platform == Platform::Linux)
            {
                // TODO: some games (e.g. Portal 2) ship a .sh for the Linux launch option. I should come up with a generic
                // solution eventually. For now, we hardcode it.
                if (m_id == "620"_L1 && store() == Store::Steam && exe.executable.endsWith(".sh"_L1))
                {
                    // Portal 2 launches via shell script but has an x86 binary
                    exe.arch = Architecture::x86;
                    continue;
                }

                const auto machine = elfMachine(raw);
                if (machine == 0)
                    continue;

                // Determine architecture
                switch (machine)
                {
                case 3: // EM_386
                    exe.arch = Architecture::x86;
                    break;
                case 62: // EM_X86_64
                    exe.arch = Architecture::x64;
                    break;
                // If Deckard is what they say it is...
                // case 183: // EM_AARCH64
                //     // ARM64 (AARCH64)
                //     break;
                // case 40: // EM_ARM
                //     // ARM
                //     break;
                default:
                    Aptabase::instance()->track(
                        "unknown-elf-architecture-bug"_L1,
                        {{"elf-arch"_L1, machine},
                         {"game-id", m_id},
                         {"executable", exe.executable},
                         {"store", QMetaEnum::fromType<Store>().valueToKey(static_cast<quint64>(store()))}});
                    break;
                }
            }
        }
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
                m_features.setFlag(Feature::Anticheat);
                return;
            }
        }
    }
}
