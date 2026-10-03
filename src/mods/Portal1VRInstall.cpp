#include "Portal1VRInstall.h"

#include <utility>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>

namespace
{
    bool runTool(const QString &program, const QStringList &args, QByteArray *output, QString &error)
    {
        QProcess process;
        process.start(program, args);
        if (!process.waitForStarted(10000))
        {
            error = "Couldn't run %1."_L1.arg(program);
            return false;
        }
        // The avatar archive expands to about 150 MB.
        if (!process.waitForFinished(180000))
        {
            process.kill();
            process.waitForFinished(5000);
            error = "%1 took too long."_L1.arg(program);
            return false;
        }
        if (output)
            *output = process.readAllStandardOutput();
        if (process.exitCode() != 0)
        {
            error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            if (error.isEmpty())
                error = "%1 failed."_L1.arg(program);
            return false;
        }
        return true;
    }

    QString packageRootOf(const QString &extracted)
    {
        QDir dir{extracted};
        if (QFileInfo::exists(dir.filePath("Release/d3d9.dll"_L1)))
            return dir.absolutePath();
        for (const auto &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            if (QFileInfo::exists(dir.filePath(name + "/Release/d3d9.dll"_L1)))
                return dir.absoluteFilePath(name);
        }
        return {};
    }

    void trackFile(const QString &portalDir, const QString &filePath, QSet<QString> &tracked)
    {
        const QString root = QDir{portalDir}.absolutePath();
        const QString file = QFileInfo{filePath}.absoluteFilePath();
        tracked.insert(file);

        auto dir = QFileInfo{file}.absolutePath();
        while (dir.startsWith(root + '/'_L1))
        {
            tracked.insert(dir + '/'_L1);
            const auto parent = QFileInfo{dir}.absolutePath();
            if (parent == dir)
                break;
            dir = parent;
        }
    }

    bool copyReplacing(const QString &from, const QString &to, QString &error)
    {
        if (!QFileInfo::exists(from))
        {
            error = "The Portal 1 VR package is missing %1."_L1.arg(from);
            return false;
        }
        if (!QDir{}.mkpath(QFileInfo{to}.absolutePath()))
        {
            error = "Couldn't create %1."_L1.arg(QFileInfo{to}.absolutePath());
            return false;
        }
        if (QFile::exists(to) && !QFile::remove(to))
        {
            error = "Couldn't replace %1. Close Portal and try again."_L1.arg(to);
            return false;
        }
        if (!QFile::copy(from, to))
        {
            error = "Couldn't copy %1."_L1.arg(QFileInfo{from}.fileName());
            return false;
        }
        return true;
    }

    bool copyInto(const QString &portalDir, const QString &from, const QString &to, QSet<QString> &tracked, QString &error)
    {
        if (!copyReplacing(from, to, error))
            return false;
        trackFile(portalDir, to, tracked);
        return true;
    }

    // The runtime rejects a missing key, including one whose capitalization differs.
    // Existing lines, comments, and the player's own values stay as they were.
    bool installConfig(const QString &portalDir, const QString &defaultsPath, QSet<QString> &tracked, QString &error)
    {
        const auto destination = QDir{portalDir}.filePath("bin/VR/config.txt"_L1);
        if (!QFileInfo::exists(destination))
            return copyInto(portalDir, defaultsPath, destination, tracked, error);

        QFile defaults{defaultsPath};
        if (!defaults.open(QIODevice::ReadOnly))
        {
            error = "The Portal 1 VR package is missing its config.txt."_L1;
            return false;
        }
        QFile current{destination};
        if (!current.open(QIODevice::ReadOnly))
        {
            error = "Couldn't read %1."_L1.arg(destination);
            return false;
        }
        const auto original = current.readAll();
        current.close();
        const auto text = QString::fromUtf8(original);

        static const QRegularExpression keyLine{R"(^[A-Za-z0-9_]+=)"};
        QStringList missing;
        for (auto line : QString::fromUtf8(defaults.readAll()).split('\n'_L1))
        {
            if (line.endsWith('\r'_L1))
                line.chop(1);
            if (!keyLine.match(line).hasMatch())
                continue;
            const auto key = line.left(line.indexOf('='_L1));
            const QRegularExpression present{"(?m)^"_L1 + QRegularExpression::escape(key) + "="_L1};
            if (!present.match(text).hasMatch())
                missing << line;
        }
        if (missing.isEmpty())
            return true;

        const QByteArray newline = original.contains("\r\n") ? QByteArray{"\r\n"} : QByteArray{"\n"};
        QByteArray updated = original;
        if (!original.endsWith('\n') && !original.endsWith('\r'))
            updated += newline;
        for (int i = 0; i < missing.size(); ++i)
        {
            if (i > 0)
                updated += newline;
            updated += missing.at(i).toUtf8();
        }
        updated += newline;

        QSaveFile out{destination};
        if (!out.open(QIODevice::WriteOnly) || out.write(updated) != updated.size() || !out.commit())
        {
            error = "Couldn't update %1."_L1.arg(destination);
            return false;
        }
        return true;
    }

    bool copyTopFiles(
        const QString &portalDir, const QString &sourceDir, const QString &destDir, QSet<QString> &tracked, QString &error)
    {
        QDir source{sourceDir};
        for (const auto &file : source.entryInfoList(QDir::Files))
        {
            if (!copyInto(portalDir, file.absoluteFilePath(), QDir{destDir}.filePath(file.fileName()), tracked, error))
                return false;
        }
        return true;
    }

    // copy-to-portal.ps1 copies each directory under materials/, and skips loose files there.
    bool copyMaterialDirs(
        const QString &portalDir, const QString &sourceDir, const QString &destDir, QSet<QString> &tracked, QString &error)
    {
        QDir source{sourceDir};
        for (const auto &dir : source.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            QDirIterator files{dir.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories};
            while (files.hasNext())
            {
                const auto from = files.next();
                const auto relative = source.relativeFilePath(from);
                if (!copyInto(portalDir, from, QDir{destDir}.filePath(relative), tracked, error))
                    return false;
            }
        }
        return true;
    }

    bool archiveIsSingleVpk(const QString &archive, QString &error)
    {
        QByteArray listing;
        if (!runTool("unzip"_L1, {"-Z1"_L1, archive}, &listing, error))
        {
            error = "Couldn't read the Bowman model archive."_L1;
            return false;
        }
        const auto names = QString::fromLocal8Bit(listing).split('\n'_L1, Qt::SkipEmptyParts);
        if (names.size() != 1 || names.constFirst() != "bowman_portal1.vpk"_L1)
        {
            error = "The Bowman model archive must contain only bowman_portal1.vpk."_L1;
            return false;
        }
        return true;
    }

    bool installVpk(const QString &portalDir, const QString &packageRoot, QSet<QString> &tracked, QString &error)
    {
        const auto archive = QDir{packageRoot}.filePath("L4D2VR/custom/bowman_portal1.zip"_L1);
        const auto loose = QDir{packageRoot}.filePath("L4D2VR/custom/bowman_portal1.vpk"_L1);
        const auto destination = QDir{portalDir}.filePath("portal/custom/bowman_portal1.vpk"_L1);

        QString extracted;
        QTemporaryDir staging;
        if (QFileInfo::exists(archive))
        {
            if (!archiveIsSingleVpk(archive, error))
                return false;
            if (!staging.isValid())
            {
                error = "Couldn't create a temporary folder for the Bowman model."_L1;
                return false;
            }
            if (!runTool("unzip"_L1,
                         {"-o"_L1, "-qq"_L1, "-j"_L1, archive, "bowman_portal1.vpk"_L1, "-d"_L1, staging.path()},
                         nullptr,
                         error))
            {
                error = "Couldn't extract the Bowman model."_L1;
                return false;
            }
            extracted = QDir{staging.path()}.filePath("bowman_portal1.vpk"_L1);
            if (!QFileInfo::exists(extracted))
            {
                error = "The Bowman model archive did not extract bowman_portal1.vpk."_L1;
                return false;
            }
        }
        else if (QFileInfo::exists(loose))
            extracted = loose;
        else
        {
            error = "The Portal 1 VR package has no Bowman model."_L1;
            return false;
        }

        if (!copyInto(portalDir, extracted, destination, tracked, error))
            return false;

        // Older installs left a loose radio WAV and sound caches that hide the song in the VPK.
        const auto customRoot = QDir{portalDir}.absoluteFilePath("portal/custom"_L1);
        const QStringList retired{
            "portal1vr/sound/ambient/music/looping_radio_mix.wav"_L1,
            "portal1vr/portal1vr_streamer_warning.txt"_L1,
            "portal1vr/sound/sound.cache"_L1,
            "bowman_portal1.vpk.sound.cache"_L1,
        };
        QStringList moving;
        for (const auto &relative : retired)
        {
            const auto oldFile = QDir::cleanPath(customRoot + '/'_L1 + relative);
            if (!oldFile.startsWith(customRoot + '/'_L1))
            {
                error = "Radio migration path escaped the custom directory."_L1;
                return false;
            }
            if (QFileInfo{oldFile}.isFile())
                moving << relative;
        }
        if (moving.isEmpty())
            return true;

        const auto now = QDateTime::currentDateTimeUtc();
        const auto fraction = QString{"%1"}.arg(now.time().msec() * 10000, 7, 10, QChar{u'0'});
        const auto stamp = now.toString("yyyyMMdd-HHmmss-"_L1) + fraction;
        const auto backupRoot = QDir{portalDir}.filePath("bin/VR/InstallBackups/radio-"_L1 + stamp);
        for (const auto &relative : std::as_const(moving))
        {
            const auto oldFile = QDir::cleanPath(customRoot + '/'_L1 + relative);
            const auto backupFile = QDir{backupRoot}.filePath(relative);
            if (!QDir{}.mkpath(QFileInfo{backupFile}.absolutePath()))
            {
                error = "Couldn't create %1."_L1.arg(QFileInfo{backupFile}.absolutePath());
                return false;
            }
            if (QFile::exists(backupFile))
                QFile::remove(backupFile);
            if (!QFile::rename(oldFile, backupFile) && !(QFile::copy(oldFile, backupFile) && QFile::remove(oldFile)))
            {
                error = "Couldn't move the old radio file out of portal/custom."_L1;
                return false;
            }
        }
        return true;
    }
} // namespace

bool installPortal1VRPackage(const QString &portalDir,
                             const QString &extractedRoot,
                             QStringList &installedFiles,
                             QString &error)
{
    const auto packageRoot = packageRootOf(extractedRoot);
    if (packageRoot.isEmpty())
    {
        error = "This download isn't a Portal 1 VR package. It has no Release/d3d9.dll."_L1;
        return false;
    }
    if (!QFileInfo::exists(QDir{portalDir}.filePath("hl2.exe"_L1)))
    {
        error =
            "Portal's Windows hl2.exe isn't in %1. Force Proton in Steam properties and wait for Steam to finish."_L1.arg(
                portalDir);
        return false;
    }

    const auto root = QDir{packageRoot};
    const QStringList requiredFiles{
        "Release/d3d9.dll"_L1,
        "Launch Portal VR.cmd"_L1,
        "thirdparty/openvr/bin/win32/openvr_api.dll"_L1,
        "L4D2VR/manifest.vrmanifest"_L1,
        "L4D2VR/portal1vr_capsule_main.png"_L1,
        "L4D2VR/portal1vr_portrait_main.png"_L1,
        "L4D2VR/config.txt"_L1,
    };
    for (const auto &relative : requiredFiles)
    {
        if (!QFileInfo::exists(root.filePath(relative)))
        {
            error = "The Portal 1 VR package is missing %1."_L1.arg(relative);
            return false;
        }
    }
    if (!QFileInfo{root.filePath("L4D2VR/SteamVRActionManifest"_L1)}.isDir())
    {
        error = "The Portal 1 VR package is missing L4D2VR/SteamVRActionManifest."_L1;
        return false;
    }

    const auto archive = root.filePath("L4D2VR/custom/bowman_portal1.zip"_L1);
    const auto looseVpk = root.filePath("L4D2VR/custom/bowman_portal1.vpk"_L1);
    if (QFileInfo::exists(archive))
    {
        // Reject a bad archive before anything in the game folder changes.
        if (!archiveIsSingleVpk(archive, error))
            return false;
    }
    else if (!QFileInfo::exists(looseVpk))
    {
        error = "The Portal 1 VR package has no Bowman model."_L1;
        return false;
    }

    const struct Copy
    {
        const char *from;
        const char *to;
    } runtime[] = {
        {"Release/d3d9.dll", "bin/d3d9.dll"},
        {"Launch Portal VR.cmd", "Launch Portal VR.cmd"},
        {"thirdparty/openvr/bin/win32/openvr_api.dll", "bin/openvr_api.dll"},
        {"L4D2VR/manifest.vrmanifest", "bin/VR/manifest.vrmanifest"},
        {"L4D2VR/portal1vr_capsule_main.png", "bin/VR/portal1vr_capsule_main.png"},
        {"L4D2VR/portal1vr_portrait_main.png", "bin/VR/portal1vr_portrait_main.png"},
    };

    QSet<QString> tracked;
    QDir portal{portalDir};
    for (const auto &file : runtime)
    {
        if (!copyInto(portalDir,
                      root.filePath(QString::fromUtf8(file.from)),
                      portal.filePath(QString::fromUtf8(file.to)),
                      tracked,
                      error))
            return false;
    }

    if (!copyTopFiles(portalDir,
                      root.filePath("L4D2VR/SteamVRActionManifest"_L1),
                      portal.filePath("bin/VR/SteamVRActionManifest"_L1),
                      tracked,
                      error))
        return false;

    if (!installConfig(portalDir, root.filePath("L4D2VR/config.txt"_L1), tracked, error))
        return false;

    const auto resource = root.filePath("L4D2VR/resource"_L1);
    if (QFileInfo{resource}.isDir() &&
        !copyTopFiles(portalDir, resource, portal.filePath("portal/custom/portal1vr/resource"_L1), tracked, error))
        return false;

    const auto materials = root.filePath("L4D2VR/materials"_L1);
    if (QFileInfo{materials}.isDir() &&
        !copyMaterialDirs(portalDir, materials, portal.filePath("portal/custom/portal1vr/materials"_L1), tracked, error))
        return false;

    if (!installVpk(portalDir, packageRoot, tracked, error))
        return false;

    installedFiles = QStringList{tracked.cbegin(), tracked.cend()};
    return true;
}
