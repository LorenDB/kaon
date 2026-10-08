#include "Game.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

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

    // Some games (e.g. Portal 2) ship a shell script as their Linux launch option, which then starts a binary next to
    // it. Returns the e_machine the ELF files named in the script agree on, or 0 if this isn't a script, it names none,
    // or they differ.
    quint16 elfMachineStartedByScript(QFile &raw)
    {
        raw.seek(0);
        const auto script = raw.read(64 * 1024);
        if (!script.startsWith("#!"_ba))
            return 0;

        // Variables are dropped, so "$DIR/bin/game" leaves /bin/game. Every name is then tried relative to the script.
        static const QRegularExpression variable{R"(\$\{[^}]*\}|\$\w+)"_L1};
        static const QRegularExpression pathLike{R"([\w.+\-/]+)"_L1};
        const auto text = QString::fromLatin1(script).replace(variable, " "_L1);
        const auto dir = QFileInfo{raw}.absolutePath();
        QSet<QString> seen;
        quint16 machine = 0;
        for (const auto &match : pathLike.globalMatch(text))
        {
            const auto name = match.captured();
            if (seen.contains(name))
                continue;
            seen.insert(name);

            QFile candidate{dir + '/' + name};
            if (!QFileInfo{candidate}.isFile() || !candidate.open(QFile::ReadOnly))
                continue;
            const auto found = elfMachine(candidate);
            if (found == 0)
                continue;
            if (machine != 0 && found != machine)
                return 0;
            machine = found;
        }
        return machine;
    }

    // Unity writes its version near the start of these files, e.g. "2019.4.30f1". The same files Rai Pal reads:
    // https://github.com/Raicuparta/rai-pal/blob/51157fdae6b1d87760580d85082ccd5026bb0320/backend/core/src/game_engines/unity.rs
    QVersionNumber unityVersion(const QString &dataDir)
    {
        static const QRegularExpression version{R"((\d+)\.(\d+)\.(\d+)[abfp]\d+)"_L1};
        for (const auto name : {"globalgamemanagers"_L1, "mainData"_L1, "data.unity3d"_L1})
        {
            QFile file{dataDir + '/' + name};
            if (!file.open(QFile::ReadOnly))
                continue;
            const auto match = version.match(QString::fromLatin1(file.read(4096)));
            if (match.hasMatch())
                return {match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt()};
        }
        return {};
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

QStringList Game::layoutRoots() const
{
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
    return roots;
}

QString Game::windowsBinaryDir(const LaunchOption &exe) const
{
    const QFileInfo launcher{exe.executable};
    const auto own = launcher.absolutePath();
    if (engine() != Engine::Unreal || own.contains("/Binaries/Win"_L1, Qt::CaseInsensitive))
        return own;

    QStringList found;
    // A game that has both builds is started from the 64-bit one
    for (const auto platform : {"Binaries/Win64"_L1, "Binaries/Win32"_L1})
    {
        for (const auto &root : layoutRoots())
        {
            for (const auto &project : QDir{root}.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            {
                // Engine/Binaries holds the crash reporter, not the game
                if (project.fileName().compare("Engine"_L1, Qt::CaseInsensitive) == 0)
                    continue;
                const QDir binaries{resolveWindowsPath(project.absoluteFilePath(), platform)};
                if (binaries.exists() && !binaries.entryList({"*.exe"_L1}, QDir::Files).isEmpty() &&
                    !found.contains(binaries.absolutePath()))
                    found << binaries.absolutePath();
            }
        }
        if (!found.isEmpty())
            break;
    }

    // The launcher is named after the project, so that settles it when a game ships more than one
    if (found.size() > 1)
    {
        const auto named = found.filter('/'_L1 + launcher.completeBaseName() + "/Binaries/"_L1, Qt::CaseInsensitive);
        if (named.size() == 1)
            return named.constFirst();
    }
    return found.size() == 1 ? found.constFirst() : own;
}

const Game::InstallScan &Game::installScan() const
{
    // One walk over the install directory collects what detectGameEngine() and detectAnticheat() each used
    // to gather with their own full walk. The filename comparisons are exactly equivalent to the regexes
    // they replace: every one of those was anchored at a path separator and case-sensitive, and the
    // relative path always starts with '/' (it is the full path with the install dir removed).
    if (!m_installScan)
    {
        InstallScan scan;
        for (QDirIterator it{m_installDir, QDirIterator::Subdirectories}; it.hasNext();)
        {
            const QString path = it.next();
            const QString name = it.fileName();

            if (!scan.source)
                scan.source = name == "vphysics.dll"_L1 || name == "vphysics.so"_L1 || name == "vphysics.dylib"_L1 ||
                              name == "bsppack.dll"_L1 || name == "bsppack.so"_L1 || name == "bsppack.dylib"_L1;

            if (!scan.unityCrashHandler)
                scan.unityCrashHandler = name == "UnityCrashHandler64.exe"_L1 || name == "UnityCrashHandler32.exe"_L1;

            if (path.endsWith(".pck"_L1, Qt::CaseInsensitive))
                scan.pcks.push_back(path);

            if (!scan.anticheat)
            {
                // QString::remove drops every occurrence, matching what the regexes used to see
                auto local = path;
                local.remove(m_installDir);
                scan.anticheat =
                    local.contains("/AntiCheatExpert/"_L1) || local.contains("/AceAntibotClient/"_L1) ||
                    local.contains("/FredaikisAntiCheat/"_L1) || local.endsWith("/HShield/HSInst.dll"_L1) ||
                    local.endsWith("/Punkbuster"_L1) || local.contains("/Punkbuster/"_L1) || local.endsWith(".xem"_L1) ||
                    name == "anybrainSDK.dll"_L1 || name == "BEService.exe"_L1 || name == "BEService_x64.exe"_L1 ||
                    name == "BlackCall.aes"_L1 || name == "BlackCall64.aes"_L1 || name == "BlackCat64.sys"_L1 ||
                    name == "EasyAntiCheat_Setup.exe"_L1 || name == "EasyAntiCheat_EOS_Setup.exe"_L1 ||
                    name == "EasyAntiCheat.dll"_L1 || name == "EasyAntiCheat_x64.dll"_L1 || name == "eac_server64.dll"_L1 ||
                    name == "EAAntiCheat.Installer.exe"_L1 || name == "equ8_conf.json"_L1 || name == "gameguard.des"_L1 ||
                    name == "PnkBstrA.exe"_L1 || name == "pbsvc.exe"_L1 || name == "pbsv.dll"_L1 ||
                    name == "Randgrid.sys"_L1 || name == "TP3Helper.exe"_L1;
            }

            // Everything is decided: the Godot fallback needs exactly one .pck, so a second one rules it out.
            if (scan.source && scan.unityCrashHandler && scan.anticheat && scan.pcks.size() > 1)
                break;
        }
        m_installScan = std::move(scan);
    }
    return *m_installScan;
}

void Game::detectGameEngine()
{
    const auto roots = layoutRoots();

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
    // File names sourced from SteamDB
    // https://github.com/SteamDatabase/FileDetectionRuleSets/blob/ac27c7cfc0a63dc07cc9e65157841857d82f347b/rules.ini#L191
    if (installScan().source)
    {
        m_engine = Engine::Source;
        return;
    }

    // =======================================
    // Unity detection
    //
    // Detection method sourced from Rai Pal
    // https://github.com/Raicuparta/rai-pal/blob/51157fdae6b1d87760580d85082ccd5026bb0320/backend/core/src/game_engines/unity.rs
    for (const auto &e : std::as_const(m_executables))
    {
        QFileInfo exe{e.executable};
        // The folder is named after the executable without its extension, dots in the name included
        for (const auto &name : {exe.completeBaseName(), exe.baseName()})
        {
            if (QFileInfo dataDir{exe.absolutePath() + '/' + name + "_Data"_L1}; dataDir.exists() && dataDir.isDir())
            {
                m_engine = Engine::Unity;
                m_engineVersion = unityVersion(dataDir.absoluteFilePath());
                return;
            }
        }
    }

    // Unity fallback: if the crash handler exists, it's a dead giveaway
    if (installScan().unityCrashHandler)
    {
        m_engine = Engine::Unity;
        return;
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
            // completeBaseName keeps dots in the name, so lin_v1.3.x86_64 matches lin_v1.3.pck.
            // baseName() would stop at the first dot and look for lin_v1.pck.
            if (pckFinder.fileName().compare(exe.completeBaseName() + ".pck"_L1, Qt::CaseInsensitive) == 0)
            {
                m_engine = Engine::Godot;
                return;
            }
        }
    }

    // fall back to looking for a single data.pck file
    const auto &pcks = installScan().pcks;
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
                auto machine = elfMachine(raw);
                if (machine == 0)
                    machine = elfMachineStartedByScript(raw);
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
    // The file names and path fragments are checked by installScan(); this only applies its result.
    if (installScan().anticheat)
        m_features.setFlag(Feature::Anticheat);
}
