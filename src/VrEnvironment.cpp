#include "VrEnvironment.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace
{
    QString openvrPathsFile()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/openvr/openvrpaths.vrpath"_L1;
    }

    QString openXrActiveRuntimeFile()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/openxr/1/active_runtime.json"_L1;
    }

    QString readText(const QString &path)
    {
        QFile file{path};
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(file.readAll());
    }

    bool dirExists(const QString &path)
    {
        return !path.isEmpty() && QFileInfo{path}.isDir();
    }

    bool fileExists(const QString &path)
    {
        return !path.isEmpty() && QFileInfo{path}.isFile();
    }

    // OpenXR runtime manifests store library_path relative to the manifest file.
    // WiVRn's active_runtime.json is a symlink into the Flatpak, and the library
    // walks up from that real file, not from ~/.config/openxr/1.
    QString resolveRuntimeLibrary(const QString &libraryPath, const QString &manifestPath)
    {
        if (libraryPath.isEmpty() || QFileInfo{libraryPath}.isAbsolute() || manifestPath.isEmpty())
            return libraryPath;
        const QFileInfo manifest{manifestPath};
        const QString canonical = manifest.canonicalFilePath();
        const QString base = QFileInfo{canonical.isEmpty() ? manifest.absoluteFilePath() : canonical}.absolutePath();
        return QDir::cleanPath(base + QLatin1Char('/') + libraryPath);
    }
} // namespace

bool VrEnvironment::Test::hasOpenVrRuntime(const QString &vrpathJson, const QString &vrOverride)
{
    // An explicit runtime override (xrizer, OpenComposite) counts when it points
    // somewhere real. It is checked first so a stale vrpath file can't hide it.
    if (dirExists(vrOverride))
        return true;

    if (vrpathJson.isEmpty())
        return false;
    const auto doc = QJsonDocument::fromJson(vrpathJson.toUtf8());
    if (!doc.isObject())
        return false;
    const auto runtime = doc.object().value("runtime"_L1);
    // vrpathreg writes "runtime" as an array of candidate directories.
    const auto candidates = runtime.isArray() ? runtime.toArray() : QJsonArray{runtime};
    for (const auto &candidate : candidates)
    {
        if (candidate.isString() && dirExists(candidate.toString()))
            return true;
    }
    return false;
}

bool VrEnvironment::Test::hasOpenXrRuntime(const QString &activeRuntimeJson,
                                           const QString &xrRuntimeJson,
                                           const QString &activeRuntimePath)
{
    // XR_RUNTIME_JSON bypasses the active_runtime.json lookup entirely.
    if (fileExists(xrRuntimeJson))
        return true;

    if (activeRuntimeJson.isEmpty())
        return false;
    const auto doc = QJsonDocument::fromJson(activeRuntimeJson.toUtf8());
    if (!doc.isObject())
        return false;
    const auto runtime = doc.object().value("runtime"_L1).toObject();
    const auto library = runtime.value("library_path"_L1).toString();
    return fileExists(resolveRuntimeLibrary(library, activeRuntimePath));
}

QString VrEnvironment::Test::openXrRuntimeName(const QString &activeRuntimeJson)
{
    const auto doc = QJsonDocument::fromJson(activeRuntimeJson.toUtf8());
    if (!doc.isObject())
        return {};
    return doc.object().value("runtime"_L1).toObject().value("name"_L1).toString();
}

VrEnvironment::Status VrEnvironment::detect()
{
    // GameStatus asks once per game on every status refresh; the library can hold
    // a hundred games, so answers are kept while the files stay put. Only the UI
    // thread uses this.
    static Status cached;
    static qint64 lookedAt = 0;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (lookedAt != 0 && now - lookedAt < 2000)
        return cached;
    lookedAt = now;

    const auto vrOverride = QString::fromLocal8Bit(qgetenv("VR_OVERRIDE"));
    const auto xrRuntimeJson = QString::fromLocal8Bit(qgetenv("XR_RUNTIME_JSON"));

    const auto vrpathJson = readText(openvrPathsFile());
    const auto activeJson = readText(openXrActiveRuntimeFile());

    cached.hasOpenVr = Test::hasOpenVrRuntime(vrpathJson, vrOverride);
    cached.hasOpenXr = Test::hasOpenXrRuntime(activeJson, xrRuntimeJson, openXrActiveRuntimeFile());
    // Prefer the override's manifest name when one is forced, otherwise the
    // registered runtime's own name ("SteamVR", "Monado", ...).
    if (!xrRuntimeJson.isEmpty() && fileExists(xrRuntimeJson))
        cached.activeRuntimeName = Test::openXrRuntimeName(readText(xrRuntimeJson));
    else
        cached.activeRuntimeName = Test::openXrRuntimeName(activeJson);
    return cached;
}

bool VrEnvironment::isProtonWineBinary(const QString &wineBinary)
{
    QDir dir = QFileInfo{wineBinary}.dir();
    if (dir.dirName() != "bin"_L1 && dir.dirName() != "bin-arm64"_L1)
        return false;
    if (!dir.cdUp())
        return false;
    return dir.dirName() == "files"_L1 || dir.dirName() == "dist"_L1;
}

bool VrEnvironment::looksLikeProtonPrefix(const QString &prefix)
{
    if (prefix.isEmpty())
        return false;
    // Proton keeps <compatdata>/<id>/{pfx,config_info,version}. A plain Wine
    // prefix has neither sibling.
    const auto clean = QDir::cleanPath(prefix);
    const auto parent = QFileInfo{clean}.absolutePath();
    if (QFileInfo{clean}.fileName() == "pfx"_L1)
        return QFileInfo::exists(parent + "/config_info"_L1) || QFileInfo::exists(parent + "/version"_L1);
    return QFileInfo::exists(clean + "/config_info"_L1) || clean.contains("/compatdata/"_L1);
}

QStringList VrEnvironment::watchedFiles()
{
    QStringList files{openvrPathsFile(), openXrActiveRuntimeFile()};
    // Watch the directories too: neither file exists before its runtime's first run.
    files << QFileInfo{openvrPathsFile()}.absolutePath() << QFileInfo{openXrActiveRuntimeFile()}.absolutePath();
    files.removeDuplicates();
    return files;
}
