#include "L4D2VR.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSaveFile>

Q_LOGGING_CATEGORY(L4D2VRLog, "l4d2vr")

namespace
{
    // The player's VR settings with any setting a newer release added. Unpacking a release over an install would
    // otherwise put every setting back to its default. The mod's parser throws if a key it reads is missing.
    QByteArray keepSettings(const QByteArray &previous, const QByteArray &shipped)
    {
        static const QRegularExpression setting{R"(^([A-Za-z0-9_]+)=)"_L1};
        const auto text = QString::fromUtf8(previous);

        QByteArray missing;
        const QByteArray newline = previous.contains("\r\n") ? "\r\n"_ba : "\n"_ba;
        for (auto line : QString::fromUtf8(shipped).split('\n'_L1))
        {
            if (line.endsWith('\r'_L1))
                line.chop(1);
            const auto match = setting.match(line);
            if (!match.hasMatch())
                continue;
            // The mod reads names with their exact capitalization
            const QRegularExpression present{"(?m)^"_L1 + QRegularExpression::escape(match.captured(1)) + '='_L1};
            if (!present.match(text).hasMatch())
                missing += line.toUtf8() + newline;
        }

        auto merged = previous;
        if (!missing.isEmpty() && !merged.endsWith('\n'))
            merged += newline;
        return merged + missing;
    }
} // namespace

L4D2VR *L4D2VR::instance()
{
    static auto p = new L4D2VR;
    return p;
}

L4D2VR *L4D2VR::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QString L4D2VR::info() const
{
    return "Start SteamVR before the game. Left 4 Dead 2 uses VAC, so the launch options include -insecure."_L1;
}

QString L4D2VR::launchOptions() const
{
    // The shipped d3d9.dll is a patched DXVK build, so Proton has to load that file instead of its own. Remaining
    // flags are the mod's required settings from
    // https://github.com/sd805/l4d2vr?tab=readme-ov-file#how-to-use
    return "WINEDLLOVERRIDES=\"d3d9=n,b\" %command% -insecure -window -novid +mat_motion_blur_percent_of_screen_max 0 "
           "+crosshair 0 -w 1280 -h 720 +mat_queue_mode 0 +mat_vsync 0 +mat_antialias 0 +mat_grain_scale_override 0"_L1;
}

QStringList L4D2VR::conflictingLaunchOptions() const
{
    // -vulkan makes the game load the DXVK it ships with (bin/dxvk_d3d9.dll) and never open the mod's d3d9.dll. The
    // mod also wants a window, not fullscreen.
    return {"-vulkan"_L1, "-fullscreen"_L1, "-full"_L1};
}

const QLoggingCategory &L4D2VR::logger() const
{
    return L4D2VRLog();
}

QString L4D2VR::configFileForGame(const Game *game) const
{
    if (!game)
        return {};
    for (const auto &exe : acceptableInstallCandidates(game))
    {
        const auto path = modInstallDirForGame(game, exe) + "/VR/config.txt"_L1;
        if (QFileInfo::exists(path))
            return QFileInfo{path}.absoluteFilePath();
    }
    return {};
}

bool L4D2VR::isInstalledForGame(const Game *game) const
{
    if (!game)
        return false;
    const auto exes = acceptableInstallCandidates(game);
    return std::any_of(exes.cbegin(), exes.cend(), [this, game](const auto &exe) {
        const auto root = modInstallDirForGame(game, exe);
        // The release zip puts both next to left4dead2.exe. The game ships neither there.
        return QFileInfo::exists(root + "/d3d9.dll"_L1) && QFileInfo::exists(root + "/openvr_api.dll"_L1);
    });
}

QString L4D2VR::installHoldReason(const Game *game) const
{
    const auto exes = acceptableInstallCandidates(game);
    const auto ready =
        std::any_of(exes.cbegin(), exes.cend(), [](const auto &exe) { return QFileInfo::exists(exe.executable); });
    if (exes.isEmpty() || ready)
        return {};
    if (game->runsWindowsBuild())
        return "Needs Left 4 Dead 2's Windows build. Wait for Steam to finish installing it, then rescan."_L1;
    return "Needs Left 4 Dead 2's Windows build. Force Proton in this game's Steam properties and wait for Steam to finish."_L1;
}

QMap<int, Game::LaunchOption> L4D2VR::acceptableInstallCandidates(const Game *game) const
{
    // The release's VR manifest is steam.app.550. App 563 (Authoring Tools) shares the game files but does not launch
    // left4dead2.exe. App 222860 is the dedicated server; its zip is a dbgcore.dll, not this mod.
    if (!game || game->store() != Game::Store::Steam || game->id() != "550"_L1 || game->engine() != Game::Engine::Source)
        return {};

    QMap<int, Game::LaunchOption> options;
    for (auto it = game->executables().cbegin(); it != game->executables().cend(); ++it)
    {
        const auto &exe = it.value();
        if (!supportsPlatform(exe.platform))
            continue;
        const auto leaf = exe.executable.mid(exe.executable.lastIndexOf('/'_L1) + 1);
        if (leaf.compare("left4dead2.exe"_L1, Qt::CaseInsensitive) != 0)
            continue;
        if (QDir{QFileInfo{exe.executable}.absolutePath()} != QDir{game->installDir()})
            continue;
        // The release is a 32-bit DXVK build. A 64-bit left4dead2.exe cannot load it.
        if (QFileInfo::exists(exe.executable) && exe.arch == Game::Architecture::x64)
            continue;
        options.insert(it.key(), exe);
    }
    return options;
}

bool L4D2VR::isThisFileTheActualModDownload(const QString &file) const
{
    // L4D2VR-dedicated-server.zip is not the game mod.
    return file.compare("L4D2VR.zip"_L1, Qt::CaseInsensitive) == 0;
}

void L4D2VR::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    if (!QFileInfo::exists(exe.executable))
    {
        const auto hold = installHoldReason(game);
        fail(hold.isEmpty() ? "Left 4 Dead 2's Windows left4dead2.exe isn't installed yet."_L1 : hold);
        return;
    }

    const auto configPath = modInstallDirForGame(game, exe) + "/VR/config.txt"_L1;
    QByteArray settingsBefore;
    if (QFile config{configPath}; config.open(QIODevice::ReadOnly))
        settingsBefore = config.readAll();

    if (!unpackInto(game, exe))
        return;

    if (!settingsBefore.isEmpty())
    {
        QFile config{configPath};
        const auto shipped = config.open(QIODevice::ReadOnly) ? config.readAll() : QByteArray{};
        config.close();
        if (QSaveFile out{configPath};
            !out.open(QIODevice::WriteOnly) || out.write(keepSettings(settingsBefore, shipped)) < 0 || !out.commit())
            qCWarning(L4D2VRLog) << "Could not put the VR settings back in" << configPath;
    }

    Mod::installModImpl(game, exe);
}

L4D2VR::L4D2VR(QObject *parent)
    : GitHubZipExtractorMod{parent}
{}
