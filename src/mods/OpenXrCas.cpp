#include "OpenXrCas.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>

Q_LOGGING_CATEGORY(OpenXrCasLog, "openxr.cas")

namespace
{
    // Wine stores this key with escaped backslashes, relative to the HKCU root named in the user.reg header.
    const auto kLayerKey = "Software\\\\Khronos\\\\OpenXR\\\\1\\\\ApiLayers\\\\Implicit"_L1;
    // Implicit OpenXR layers are registered per prefix. XR_API_LAYER_PATH does not enable them.
    const auto kWindowsJson = "C:\\Kaon\\OpenXR-CAS\\openxr-api-layer.json"_L1;

    QString layerDirectory(const Game *game)
    {
        return QDir{game->winePrefix()}.filePath("drive_c/Kaon/OpenXR-CAS"_L1);
    }

    QString userRegPath(const Game *game)
    {
        return QDir{game->winePrefix()}.filePath("user.reg"_L1);
    }

    QString escapeReg(const QString &text)
    {
        QString out;
        for (const auto c : text)
        {
            if (c == u'\\' || c == u'"')
                out += u'\\';
            out += c;
        }
        return out;
    }

    bool parseQuotedName(const QString &line, QString *name, QString *rest)
    {
        if (!line.startsWith(u'"'))
            return false;
        QString decoded;
        for (int i = 1; i < line.size(); ++i)
        {
            const auto c = line.at(i);
            if (c == u'\\' && i + 1 < line.size())
            {
                decoded += line.at(++i);
                continue;
            }
            if (c == u'"')
            {
                if (i + 1 >= line.size() || line.at(i + 1) != u'=')
                    return false;
                *name = decoded;
                *rest = line.mid(i + 2);
                return true;
            }
            decoded += c;
        }
        return false;
    }

    bool isLayerHeader(const QString &line)
    {
        if (!line.startsWith(u'['))
            return false;
        const auto end = line.indexOf(u']');
        if (end < 0)
            return false;
        return line.mid(1, end - 1).compare(kLayerKey, Qt::CaseInsensitive) == 0;
    }

    QString fileTimeNow()
    {
        const quint64 fileTime = (static_cast<quint64>(QDateTime::currentSecsSinceEpoch()) + 11644473600ULL) * 10000000ULL;
        return "#time=%1%2"_L1.arg(QString::number(fileTime >> 32, 16),
                                   QString::number(static_cast<quint32>(fileTime), 16).rightJustified(8, u'0'));
    }

    QStringList logicalLines(QString text)
    {
        text.replace("\r\n"_L1, "\n"_L1);
        if (text.endsWith(u'\n'))
            text.chop(1);
        return text.isEmpty() ? QStringList{} : text.split(u'\n');
    }

    QString joinLines(const QStringList &lines, bool crlf)
    {
        auto out = lines.join(u'\n') + u'\n';
        if (crlf)
            out.replace(u'\n', "\r\n"_L1);
        return out;
    }

    bool layerEnabled(const QString &text, const QString &windowsJsonPath)
    {
        bool inSection = false;
        for (const auto &line : logicalLines(text))
        {
            if (line.startsWith(u'['))
            {
                inSection = isLayerHeader(line);
                continue;
            }
            if (!inSection)
                continue;
            QString name, rest;
            if (!parseQuotedName(line, &name, &rest))
                continue;
            if (name.compare(windowsJsonPath, Qt::CaseInsensitive) != 0)
                continue;
            if (!rest.startsWith("dword:"_L1, Qt::CaseInsensitive))
                return false;
            bool ok = false;
            return rest.mid(6).toUInt(&ok, 16) == 0 && ok;
        }
        return false;
    }

    // Returns false only when the text is not a Wine registry. *changed is set when the bytes would differ.
    bool setLayerEnabled(QString *text, const QString &windowsJsonPath, bool enabled, bool *changed)
    {
        if (!text->startsWith("WINE REGISTRY Version 2"_L1))
            return false;

        const bool crlf = text->contains("\r\n"_L1);
        auto lines = logicalLines(*text);
        const auto valueLine = "\"%1\"=dword:00000000"_L1.arg(escapeReg(windowsJsonPath));

        bool found = false;
        for (int i = 0; i < lines.size();)
        {
            if (!isLayerHeader(lines.at(i)))
            {
                ++i;
                continue;
            }
            found = true;
            const int header = i;
            int end = header + 1;
            while (end < lines.size() && !lines.at(end).startsWith(u'['))
                ++end;

            int valueAt = -1;
            for (int j = header + 1; j < end; ++j)
            {
                QString name, rest;
                if (!parseQuotedName(lines.at(j), &name, &rest))
                    continue;
                if (name.compare(windowsJsonPath, Qt::CaseInsensitive) == 0)
                {
                    valueAt = j;
                    break;
                }
            }

            if (enabled)
            {
                if (valueAt >= 0)
                    lines[valueAt] = valueLine;
                else
                {
                    int insertAt = header + 1;
                    if (insertAt < end && lines.at(insertAt).startsWith("#time="_L1))
                        ++insertAt;
                    lines.insert(insertAt, valueLine);
                    ++end;
                }
                i = end;
                continue;
            }

            if (valueAt >= 0)
            {
                lines.removeAt(valueAt);
                --end;
            }
            bool valuesLeft = false;
            for (int j = header + 1; j < end; ++j)
            {
                QString name, rest;
                if (parseQuotedName(lines.at(j), &name, &rest))
                    valuesLeft = true;
            }
            if (!valuesLeft)
            {
                int from = header;
                if (from > 0 && lines.at(from - 1).isEmpty())
                    --from;
                lines.erase(lines.begin() + from, lines.begin() + end);
                i = from;
            }
            else
                i = end;
        }

        if (enabled && !found)
        {
            if (!lines.isEmpty() && !lines.last().isEmpty())
                lines << QString{};
            lines << "[%1] %2"_L1.arg(kLayerKey, QString::number(QDateTime::currentSecsSinceEpoch()));
            lines << fileTimeNow();
            lines << valueLine;
        }

        const auto out = joinLines(lines, crlf);
        *changed = out != *text;
        *text = out;
        return true;
    }

    QStringList prefixPaths(const Game *game)
    {
        QStringList paths;
        const auto add = [&](const QString &path) {
            if (path.isEmpty())
                return;
            const auto clean = QDir::cleanPath(path);
            if (!paths.contains(clean))
                paths << clean;
            const auto canonical = QFileInfo{clean}.canonicalFilePath();
            if (!canonical.isEmpty() && !paths.contains(canonical))
                paths << canonical;
        };
        add(game->winePrefix());
        add(game->sandboxWinePrefix());
        return paths;
    }

    bool environListsPrefix(const QByteArray &env, const QStringList &prefixes)
    {
        const QByteArray key{"WINEPREFIX="};
        int from = 0;
        while (from < env.size())
        {
            const auto at = env.indexOf(key, from);
            if (at < 0)
                return false;
            if (at == 0 || env.at(at - 1) == '\0')
            {
                const auto start = at + key.size();
                auto end = env.indexOf('\0', start);
                if (end < 0)
                    end = env.size();
                const auto value = QDir::cleanPath(QString::fromLocal8Bit(env.mid(start, end - start)));
                if (prefixes.contains(value))
                    return true;
            }
            from = at + 1;
        }
        return false;
    }

    // A running wineserver rewrites user.reg when it exits, which would drop a direct edit. Flatpak games see the sandbox
    // path.
    bool prefixInUse(const Game *game)
    {
        const auto prefixes = prefixPaths(game);
        if (prefixes.isEmpty())
            return false;

        // The game page asks again whenever a download ticks. One scan per second is enough, and a click that lands inside
        // that window still sees a game that is actually running.
        const auto key = prefixes.join(u'\n');
        struct Cache
        {
            QString key;
            qint64 at{0};
            bool inUse{false};
        };
        static Cache cache;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (cache.key == key && now - cache.at < 1000)
            return cache.inUse;

        bool inUse = false;
        const auto entries = QDir{"/proc"_L1}.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &entry : entries)
        {
            bool numeric = false;
            entry.toInt(&numeric);
            if (!numeric)
                continue;
            QFile env{"/proc/"_L1 + entry + "/environ"_L1};
            if (!env.open(QIODevice::ReadOnly))
                continue;
            if (environListsPrefix(env.readAll(), prefixes))
            {
                inUse = true;
                break;
            }
        }
        cache = {key, now, inUse};
        return inUse;
    }

    bool layerFilesReady(const Game *game)
    {
        const auto dir = layerDirectory(game);
        return QFileInfo::exists(dir + "/XR_APILAYER_OPENXR_SHARPENER.dll"_L1) &&
               QFileInfo::exists(dir + "/openxr-api-layer.json"_L1) && QFileInfo::exists(dir + "/shaders/CAS.hlsl"_L1);
    }

    bool registrySaysEnabled(const Game *game)
    {
        QFile file{userRegPath(game)};
        if (!file.open(QIODevice::ReadOnly))
            return false;
        return layerEnabled(QString::fromUtf8(file.readAll()), kWindowsJson);
    }

    bool writeLayerEnabled(const Game *game, bool enabled, QString *error)
    {
        const auto path = userRegPath(game);
        QFile file{path};
        if (!file.open(QIODevice::ReadOnly))
        {
            *error = "Kaon couldn't read this game's Proton prefix."_L1;
            return false;
        }
        auto text = QString::fromUtf8(file.readAll());
        file.close();

        bool changed = false;
        if (!setLayerEnabled(&text, kWindowsJson, enabled, &changed))
        {
            *error = "This Proton prefix's user.reg isn't a Wine registry Kaon can edit."_L1;
            return false;
        }
        if (layerEnabled(text, kWindowsJson) != enabled)
        {
            *error = enabled ? "Kaon couldn't turn OpenXR CAS on in this game's prefix."_L1 :
                               "Kaon couldn't turn OpenXR CAS off in this game's prefix."_L1;
            return false;
        }
        if (!changed)
            return true;

        QSaveFile out{path};
        if (!out.open(QIODevice::WriteOnly))
        {
            *error = "Kaon couldn't write this game's Proton prefix."_L1;
            return false;
        }
        const auto bytes = text.toUtf8();
        if (out.write(bytes) != bytes.size() || !out.commit())
        {
            *error = "Kaon couldn't write this game's Proton prefix."_L1;
            return false;
        }
        return true;
    }

    void removeLayerFiles(const Game *game)
    {
        QDir{layerDirectory(game)}.removeRecursively();
        QDir drive{QDir{game->winePrefix()}.filePath("drive_c"_L1)};
        if (QDir{drive.filePath("Kaon"_L1)}.isEmpty())
            drive.rmdir("Kaon"_L1);
    }

    QMap<int, Game::LaunchOption> windowsCandidates(const Game *game)
    {
        QMap<int, Game::LaunchOption> options;
        if (!game || game->noWindowsSupport())
            return options;

        for (auto it = game->executables().cbegin(); it != game->executables().cend(); ++it)
        {
            const auto &exe = it.value();
            if (exe.platform != Game::Platform::Windows || !QFileInfo::exists(exe.executable))
                continue;
            // The release is 64-bit only.
            if (exe.arch != Game::Architecture::x64)
                continue;
            options.insert(it.key(), exe);
        }
        return options;
    }
} // namespace

OpenXrCas *OpenXrCas::instance()
{
    static auto layer = new OpenXrCas;
    return layer;
}

OpenXrCas *OpenXrCas::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QString OpenXrCas::info() const
{
    return "Each game gets its own copy in that game's Proton prefix. Direct3D 11 OpenXR only. Quit the game before "
           "turning it on or off. Sharpness lives in config.cfg next to the layer, or in "
           "AppData\\Local\\XR_APILAYER_OPENXR_SHARPENER. See [GitHub](https://github.com/elliotttate/OpenXR-CAS)."_L1;
}

const QLoggingCategory &OpenXrCas::logger() const
{
    return OpenXrCasLog();
}

bool OpenXrCas::isInstalledForGame(const Game *game) const
{
    if (!game || game->winePrefix().isEmpty())
        return false;
    return layerFilesReady(game) && registrySaysEnabled(game);
}

QString OpenXrCas::installHoldReason(const Game *game) const
{
    if (!game)
        return {};
    const auto reg = userRegPath(game);
    const auto drive = QDir{game->winePrefix()}.filePath("dosdevices/c:"_L1);
    if (!game->hasValidWine() || !QFileInfo::exists(reg) || !QFileInfo::exists(drive))
        return "Launch the game once so Kaon can enable this in its Proton prefix."_L1;
    if (prefixInUse(game))
        return "Quit the game before changing this. Its Proton prefix is in use."_L1;
    return {};
}

QMap<int, Game::LaunchOption> OpenXrCas::acceptableInstallCandidates(const Game *game) const
{
    return windowsCandidates(game);
}

bool OpenXrCas::isThisFileTheActualModDownload(const QString &file) const
{
    return file.endsWith(".zip"_L1, Qt::CaseInsensitive) && file.contains("win64"_L1, Qt::CaseInsensitive) &&
           !file.contains("pdb"_L1, Qt::CaseInsensitive) && !file.contains("symbol"_L1, Qt::CaseInsensitive);
}

void OpenXrCas::installModImpl(Game *game, const Game::LaunchOption &exe)
{
    if (const auto hold = installHoldReason(game); !hold.isEmpty())
    {
        fail(hold);
        return;
    }

    const auto release = currentRelease();
    if (!release || release->assets().isEmpty())
    {
        fail("OpenXR CAS has no download yet."_L1);
        return;
    }
    const auto asset = chooseAssetToInstall(game, exe);
    if (asset.id < 0)
    {
        fail("This version of OpenXR CAS has no Windows x64 download."_L1);
        return;
    }
    const auto archive = pathForRelease(release, asset);
    if (!QFileInfo::exists(archive))
    {
        fail("Download OpenXR CAS before turning it on."_L1);
        return;
    }

    const auto dest = layerDirectory(game);
    if (!QDir{}.mkpath(dest))
    {
        fail("Couldn't create a folder for OpenXR CAS in this game's prefix."_L1);
        return;
    }

    // Leave config.cfg alone when it's already there. The zip has no config; the code default is sharpness 0.6.
    QProcess unzip;
    unzip.start("unzip"_L1,
                {"-o"_L1,
                 "-qq"_L1,
                 archive,
                 "XR_APILAYER_OPENXR_SHARPENER.dll"_L1,
                 "openxr-api-layer.json"_L1,
                 "shaders/*"_L1,
                 "-d"_L1,
                 dest});
    if (!unzip.waitForStarted(10000) || !unzip.waitForFinished(180000) || unzip.exitCode() != 0)
    {
        const auto detail = QString::fromLocal8Bit(unzip.readAllStandardError()).trimmed();
        fail(detail.isEmpty() ? "Couldn't extract the OpenXR CAS download."_L1 : detail);
        return;
    }
    if (!layerFilesReady(game))
    {
        fail("The OpenXR CAS download is missing the layer or its CAS shader."_L1);
        return;
    }

    QString error;
    if (!writeLayerEnabled(game, true, &error))
    {
        // The files stay. Without the registry value the layer is inert, and the next try overwrites them.
        fail(error);
        return;
    }

    Mod::installModImpl(game, exe);
}

void OpenXrCas::uninstallMod(Game *game)
{
    if (!game)
        return;
    // A live wineserver would rewrite user.reg on the way out and put the layer back.
    if (prefixInUse(game))
    {
        fail("Quit the game before changing this. Its Proton prefix is in use."_L1);
        return;
    }

    if (QFileInfo::exists(userRegPath(game)))
    {
        QString error;
        if (!writeLayerEnabled(game, false, &error))
        {
            fail(error);
            return;
        }
        if (registrySaysEnabled(game))
        {
            fail("Kaon couldn't turn OpenXR CAS off in this game's prefix."_L1);
            return;
        }
    }

    if (!game->winePrefix().isEmpty())
        removeLayerFiles(game);
    Mod::uninstallMod(game);
}

OpenXrCas::OpenXrCas(QObject *parent)
    : GitHubMod{parent}
{}
