#include "Game.h"

#include <functional>

#include <QDir>
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

    // Folders that can hold an engine, anticheat, or upscaler sign. Content, paks, and movies are not in
    // this list: descending into those is what made a large game on a slow drive stall the library scan.
    bool descendForProbe(const QString &name)
    {
        static const QStringList exact{"bin"_L1,
                                       "Binaries"_L1,
                                       "Win64"_L1,
                                       "Win32"_L1,
                                       "x64"_L1,
                                       "linux64"_L1,
                                       "osx64"_L1,
                                       "Engine"_L1,
                                       "Plugins"_L1,
                                       "Runtime"_L1,
                                       "ThirdParty"_L1,
                                       "EasyAntiCheat"_L1,
                                       "BattlEye"_L1,
                                       "AntiCheatExpert"_L1,
                                       "AceAntibotClient"_L1,
                                       "FredaikisAntiCheat"_L1,
                                       "HShield"_L1,
                                       "Punkbuster"_L1,
                                       "PunkBuster"_L1,
                                       "GameGuard"_L1,
                                       "DLSS"_L1,
                                       "NGX"_L1,
                                       "Streamline"_L1,
                                       "FidelityFX"_L1,
                                       "XeSS"_L1,
                                       "NVIDIA"_L1,
                                       "Nvidia"_L1,
                                       "nvngx"_L1};
        if (exact.contains(name))
            return true;
        const auto lower = name.toLower();
        return lower == "dlss"_L1 || lower == "ngx"_L1 || lower == "streamline"_L1 || lower == "nvdlss"_L1 ||
               lower.startsWith("fsr"_L1) || lower == "fidelityfx"_L1 || lower.contains("fidelityfx"_L1) ||
               lower.startsWith("xess"_L1);
    }

    // Same names the full-tree walk matched. Directory hits are the folders those rules treated as decisive
    // on their own; the file names are matched exactly, case-sensitive, as the old comparisons were.
    bool walkDirAncestors(const QString &installDir,
                          const QString &startDir,
                          const std::function<bool(const QString &dir)> &visit)
    {
        const auto rooted = QDir::cleanPath(installDir);
        for (auto dir = QDir::cleanPath(startDir); !dir.isEmpty();)
        {
            if (visit(dir))
                return true;
            if (!rooted.isEmpty() && dir == rooted)
                break;
            const auto parent = QFileInfo{dir}.absolutePath();
            if (parent == dir)
                break;
            dir = QDir::cleanPath(parent);
            if (!rooted.isEmpty() && dir != rooted && !dir.startsWith(rooted + '/'_L1))
                break;
        }
        return false;
    }

    bool walkDirsFromExecutables(const QString &installDir,
                                 const QMap<int, Game::LaunchOption> &executables,
                                 bool alsoWalkInstallDir,
                                 const std::function<bool(const QString &dir)> &visit)
    {
        const auto rooted = QDir::cleanPath(installDir);
        QSet<QString> starts;
        for (const auto &exe : std::as_const(executables))
        {
            if (!exe.executable.isEmpty())
                starts.insert(QDir::cleanPath(QFileInfo{exe.executable}.absolutePath()));
        }
        if (alsoWalkInstallDir && !rooted.isEmpty())
            starts.insert(rooted);

        QSet<QString> visited;
        for (const auto &start : std::as_const(starts))
        {
            if (walkDirAncestors(installDir, start, [&](const QString &dir) {
                    if (visited.contains(dir))
                        return false;
                    visited.insert(dir);
                    return visit(dir);
                }))
                return true;
        }
        return false;
    }

    bool dirHasUnrealEngineTree(const QString &dir)
    {
        for (const auto &platform : {"Win64"_L1, "Win32"_L1, "ThirdParty"_L1})
        {
            if (QFileInfo fi{dir + "/Engine/Binaries/"_L1 + platform}; fi.exists() && fi.isDir())
                return true;
        }
        return false;
    }

    bool detectUnrealEngine(const QString &installDir, const QMap<int, Game::LaunchOption> &executables)
    {
        return walkDirsFromExecutables(
            installDir, executables, true, [](const QString &dir) { return dirHasUnrealEngineTree(dir); });
    }

    bool dirHasSourceEngineFiles(const QString &dir)
    {
        for (const auto &name :
             {"vphysics.dll"_L1, "vphysics.so"_L1, "vphysics.dylib"_L1, "bsppack.dll"_L1, "bsppack.so"_L1, "bsppack.dylib"_L1})
        {
            if (QFileInfo fi{dir + '/' + name}; fi.exists() && fi.isFile())
                return true;
        }
        return false;
    }

    bool detectSourceEngine(const QString &installDir, const QMap<int, Game::LaunchOption> &executables)
    {
        return walkDirsFromExecutables(
            installDir, executables, true, [](const QString &dir) { return dirHasSourceEngineFiles(dir); });
    }

    bool dirHasGodotPck(const QString &dir, const QString &pckName)
    {
        for (const auto &fi : QDir{dir}.entryInfoList(QDir::Files))
        {
            if (fi.fileName().compare(pckName, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
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

    bool detectGodotEngine(const QString &installDir, const QMap<int, Game::LaunchOption> &executables)
    {
        for (const auto &opt : std::as_const(executables))
        {
            if (opt.executable.isEmpty())
                continue;
            const QFileInfo exe{opt.executable};
            const auto pck = exe.completeBaseName() + ".pck"_L1;
            if (walkDirAncestors(installDir, exe.absolutePath(), [&](const QString &dir) {
                    return dirHasGodotPck(dir, pck);
                }))
                return true;
        }
        return walkDirsFromExecutables(installDir, executables, true, [](const QString &dir) {
            return dirHasGodotPck(dir, "data.pck"_L1);
        });
    }

    bool detectUnityEngine(const QString &installDir,
                           const QMap<int, Game::LaunchOption> &executables,
                           QVersionNumber *versionOut)
    {
        const auto unityInDir = [&](const QString &dir, const QStringList &dataNames) {
            for (const auto &name : dataNames)
            {
                if (QFileInfo data{dir + '/' + name + "_Data"_L1}; data.exists() && data.isDir())
                {
                    if (versionOut != nullptr)
                        *versionOut = unityVersion(data.absoluteFilePath());
                    return true;
                }
            }
            return QFileInfo{dir + "/UnityCrashHandler64.exe"_L1}.exists() ||
                   QFileInfo{dir + "/UnityCrashHandler32.exe"_L1}.exists();
        };

        for (const auto &opt : std::as_const(executables))
        {
            if (opt.executable.isEmpty())
                continue;
            const QFileInfo exe{opt.executable};
            const QStringList names{exe.completeBaseName(), exe.baseName()};
            if (walkDirAncestors(installDir, exe.absolutePath(), [&](const QString &dir) { return unityInDir(dir, names); }))
                return true;
        }

        const auto rooted = QDir::cleanPath(installDir);
        if (!rooted.isEmpty())
            return walkDirAncestors(installDir, rooted, [&](const QString &dir) { return unityInDir(dir, {}); });
        return false;
    }

    bool anticheatHit(const QString &name, const QString &path, bool isDir)
    {
        if (isDir)
            return name == "AntiCheatExpert"_L1 || name == "AceAntibotClient"_L1 || name == "FredaikisAntiCheat"_L1 ||
                   name == "Punkbuster"_L1;
        static const QStringList files{"anybrainSDK.dll"_L1,
                                       "BEService.exe"_L1,
                                       "BEService_x64.exe"_L1,
                                       "BlackCall.aes"_L1,
                                       "BlackCall64.aes"_L1,
                                       "BlackCat64.sys"_L1,
                                       "EasyAntiCheat_Setup.exe"_L1,
                                       "EasyAntiCheat_EOS_Setup.exe"_L1,
                                       "EasyAntiCheat.dll"_L1,
                                       "EasyAntiCheat_x64.dll"_L1,
                                       "eac_server64.dll"_L1,
                                       "EAAntiCheat.Installer.exe"_L1,
                                       "equ8_conf.json"_L1,
                                       "gameguard.des"_L1,
                                       "PnkBstrA.exe"_L1,
                                       "pbsvc.exe"_L1,
                                       "pbsv.dll"_L1,
                                       "Randgrid.sys"_L1,
                                       "TP3Helper.exe"_L1};
        return files.contains(name) || name.endsWith(".xem"_L1) || path.endsWith("/HShield/HSInst.dll"_L1);
    }

    void noteUpscaler(bool &dlss, bool &fsr, bool &xess, const QString &name, bool isDir)
    {
        if (dlss && fsr && xess)
            return;
        const auto lower = name.toLower();
        if (!isDir && lower.endsWith(".dll"_L1))
        {
            if (!dlss)
                dlss = lower.contains("dlss"_L1) || lower == "sl.dll"_L1;
            if (!xess)
                xess = lower.contains("xess"_L1);
            if (!fsr)
                fsr = lower.contains("fidelityfx"_L1) || lower.startsWith("ffx_"_L1) || lower.contains("amd_fsr"_L1) ||
                      lower.contains("fsr"_L1);
        }
        if (isDir)
        {
            if (!dlss)
                dlss = lower == "dlss"_L1 || lower == "ngx"_L1 || lower == "streamline"_L1 || lower == "nvdlss"_L1;
            if (!fsr)
                fsr = lower.startsWith("fsr"_L1) || lower == "fidelityfx"_L1 || lower.contains("fidelityfx"_L1);
            if (!xess)
                xess = lower.startsWith("xess"_L1);
        }
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
    // Listing the install root, each executable directory, and a few known child folders. .pck files count
    // only in those starting directories: the Godot fallback used to require the single data.pck in the
    // whole tree, and counting every .pck we happen to pass while descending would change that.
    if (!m_installScan)
    {
        InstallScan scan;
        if (!m_installDir.isEmpty() && QFileInfo{m_installDir}.isDir())
        {
            struct Frame
            {
                QString dir;
                int depth = 0;
                bool atRoot = false;
            };
            QSet<QString> seen;
            QList<Frame> pending;
            for (const auto &root : layoutRoots())
                pending.append({root, 8, true});

            while (!pending.isEmpty())
            {
                const auto frame = pending.takeLast();
                const auto clean = QDir::cleanPath(frame.dir);
                if (clean.isEmpty() || seen.contains(clean))
                    continue;
                seen.insert(clean);
                if (QFileInfo info{clean}; !info.exists() || !info.isDir())
                    continue;

                const auto settled = [&scan] {
                    return scan.anticheat && (scan.source || scan.unityCrashHandler || scan.pcks.size() > 1) &&
                           scan.hasDlssFile && scan.hasFsrFile && scan.hasXessFile;
                };

                const auto entries = QDir{clean}.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
                for (const auto &fi : entries)
                {
                    const auto name = fi.fileName();
                    const auto path = fi.filePath();
                    const bool isDir = fi.isDir();

                    if (!scan.source)
                        scan.source = name == "vphysics.dll"_L1 || name == "vphysics.so"_L1 || name == "vphysics.dylib"_L1 ||
                                      name == "bsppack.dll"_L1 || name == "bsppack.so"_L1 || name == "bsppack.dylib"_L1;
                    if (!scan.unityCrashHandler)
                        scan.unityCrashHandler =
                            name == "UnityCrashHandler64.exe"_L1 || name == "UnityCrashHandler32.exe"_L1;
                    if (frame.atRoot && name.endsWith(".pck"_L1, Qt::CaseInsensitive))
                        scan.pcks.push_back(path);
                    if (!scan.anticheat)
                        scan.anticheat = anticheatHit(name, path, isDir);
                    noteUpscaler(scan.hasDlssFile, scan.hasFsrFile, scan.hasXessFile, name, isDir);

                    if (isDir && frame.depth > 0 && descendForProbe(name))
                        pending.append({path, frame.depth - 1, false});
                    if (settled())
                        break;
                }
                if (settled())
                    break;
            }
        }
        m_installScan = std::move(scan);
    }
    return *m_installScan;
}

QStringList Game::upscalerInputs() const
{
    const auto &scan = installScan();
    QStringList out;
    if (scan.hasDlssFile)
        out << "DLSS"_L1;
    if (scan.hasFsrFile)
        out << "FSR"_L1;
    if (scan.hasXessFile)
        out << "XeSS"_L1;
    return out;
}

void Game::detectGameEngine()
{
    // =======================================
    // Unreal detection
    //
    // Walk up from each launch executable (and the install dir) looking for Engine/Binaries.
    // UE4 WindowsNoEditor builds put that tree next to the launcher, not at the Steam install root.
    // https://github.com/Raicuparta/rai-pal/blob/51157fdae6b1d87760580d85082ccd5026bb0320/backend/core/src/game_engines/unreal.rs
    if (detectUnrealEngine(m_installDir, m_executables))
    {
        m_engine = Engine::Unreal;
        return;
    }

    // =======================================
    // Source detection
    //
    // File names sourced from SteamDB
    // https://github.com/SteamDatabase/FileDetectionRuleSets/blob/ac27c7cfc0a63dc07cc9e65157841857d82f347b/rules.ini#L191
    if (detectSourceEngine(m_installDir, m_executables) || installScan().source)
    {
        m_engine = Engine::Source;
        return;
    }

    // =======================================
    // Unity detection
    //
    // Detection method sourced from Rai Pal
    // https://github.com/Raicuparta/rai-pal/blob/51157fdae6b1d87760580d85082ccd5026bb0320/backend/core/src/game_engines/unity.rs
    if (detectUnityEngine(m_installDir, m_executables, &m_engineVersion) || installScan().unityCrashHandler)
    {
        m_engine = Engine::Unity;
        return;
    }

    // =======================================
    // Godot detection
    //
    // Dectection method sourced from SteamDB
    // https://github.com/SteamDatabase/FileDetectionRuleSets/blob/ac27c7cfc0a63dc07cc9e65157841857d82f347b/tests/FileDetector.php#L316
    if (detectGodotEngine(m_installDir, m_executables))
    {
        m_engine = Engine::Godot;
        return;
    }

    // installScan() still descends into content trees when the signs sit off the exe path
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
