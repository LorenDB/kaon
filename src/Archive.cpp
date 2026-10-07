#include "Archive.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

Q_LOGGING_CATEGORY(ArchiveLog, "archive")

namespace
{
    enum class Kind
    {
        Zip,
        SevenZip,
    };

    enum class Tool
    {
        None,
        Unzip,
        Bsdtar,
        Python,
        SevenZip,
    };

    struct Unpacker
    {
        Tool tool = Tool::None;
        QString program;
    };

    Kind kindOf(const QString &path)
    {
        return path.endsWith(".7z"_L1, Qt::CaseInsensitive) ? Kind::SevenZip : Kind::Zip;
    }

    // Python's zipfile is the last resort for zip: it is on nearly every desktop, but it doesn't restore file
    // permissions. It cannot read 7z.
    const auto pythonScript = R"(
import sys, zipfile, fnmatch
mode, path = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(path) as z:
    names = z.namelist()
    if mode == "list":
        sys.stdout.write("\n".join(names))
    else:
        patterns = sys.argv[4:]
        if patterns:
            names = [n for n in names if any(fnmatch.fnmatchcase(n, p) for p in patterns)]
            if not names:
                sys.exit(11)
        z.extractall(sys.argv[3], names)
)"_L1;

    Unpacker zipUnpacker()
    {
        static Unpacker found;
        if (found.tool != Tool::None)
            return found;

        const std::pair<Tool, QLatin1StringView> candidates[] = {
            {Tool::Unzip, "unzip"_L1},
            {Tool::Bsdtar, "bsdtar"_L1},
            {Tool::Python, "python3"_L1},
        };
        for (const auto &[tool, name] : candidates)
        {
            if (const auto program = QStandardPaths::findExecutable(name); !program.isEmpty())
            {
                found = {tool, program};
                qCInfo(ArchiveLog) << "Unpacking zip files with" << program;
                break;
            }
        }
        return found;
    }

    Unpacker sevenZipUnpacker()
    {
        static Unpacker found;
        if (found.tool != Tool::None)
            return found;

        for (const auto name : {"7z"_L1, "7za"_L1})
        {
            if (const auto program = QStandardPaths::findExecutable(name); !program.isEmpty())
            {
                found = {Tool::SevenZip, program};
                qCInfo(ArchiveLog) << "Unpacking 7z files with" << program;
                break;
            }
        }
        return found;
    }

    const auto noZipUnpacker = "Kaon needs unzip, bsdtar or python3 to unpack downloads, and none of them is installed. "
                               "Install unzip with your package manager and try again."_L1;
    const auto noSevenZipUnpacker =
        "Kaon needs 7z or 7za (from p7zip) to unpack OptiScaler and other .7z downloads, and neither is installed. "
        "Install p7zip with your package manager and try again."_L1;

    struct Run
    {
        bool ok = false;
        int exitCode = -1;
        QByteArray out;
        QByteArray err;
    };

    Run run(const QString &program, const QStringList &arguments)
    {
        Run result;
        QProcess process;
        process.start(program, arguments);
        // A process that never started reports exit code 0, so the exit code alone says nothing
        if (!process.waitForStarted(10000))
        {
            result.err = process.errorString().toLocal8Bit();
            return result;
        }
        if (!process.waitForFinished(600000))
        {
            process.kill();
            process.waitForFinished(5000);
            result.err = "timed out";
            return result;
        }
        result.ok = process.exitStatus() == QProcess::NormalExit;
        result.exitCode = process.exitCode();
        result.out = process.readAllStandardOutput();
        result.err = process.readAllStandardError();
        return result;
    }

    // unzip exits with 1 when it had something to warn about and still unpacked everything, e.g. a zip made on
    // Windows with backslashes in its paths
    bool succeeded(const Unpacker &with, const Run &result)
    {
        if (!result.ok)
            return false;
        return result.exitCode == 0 || (with.tool == Tool::Unzip && result.exitCode == 1);
    }
} // namespace

bool Archive::extract(const QString &archive, const QString &dest, const QStringList &members, QString *error)
{
    const auto kind = kindOf(archive);
    const auto with = kind == Kind::SevenZip ? sevenZipUnpacker() : zipUnpacker();
    if (with.tool == Tool::None)
    {
        qCWarning(ArchiveLog) << "No tool to unpack" << archive;
        *error = kind == Kind::SevenZip ? noSevenZipUnpacker : noZipUnpacker;
        return false;
    }
    if (!QDir{}.mkpath(dest))
    {
        *error = "Kaon couldn't create %1."_L1.arg(dest);
        return false;
    }

    QStringList arguments;
    switch (with.tool)
    {
    case Tool::Unzip:
        arguments = QStringList{"-o"_L1, "-qq"_L1, archive} + members + QStringList{"-d"_L1, dest};
        break;
    case Tool::Bsdtar:
        arguments = QStringList{"-x"_L1, "-f"_L1, archive, "-C"_L1, dest} + members;
        break;
    case Tool::SevenZip:
        // -y answers yes; -o has no space before the path. Members after the archive limit what is extracted.
        arguments = QStringList{"x"_L1, "-y"_L1, "-o"_L1 + dest, archive} + members;
        break;
    default:
        arguments = QStringList{"-I"_L1, "-c"_L1, pythonScript, "extract"_L1, archive, dest} + members;
        break;
    }

    const auto result = run(with.program, arguments);
    if (!succeeded(with, result))
    {
        qCWarning(ArchiveLog).noquote() << "Unpacking" << archive << "into" << dest << "failed with exit code"
                                        << result.exitCode << result.err.trimmed();
        *error = "Kaon couldn't unpack %1. The file may be damaged: delete that version in its version menu and "
                 "download it again."_L1.arg(QFileInfo{archive}.fileName());
        return false;
    }
    if (result.exitCode != 0)
        qCInfo(ArchiveLog).noquote() << "Unpacked" << archive << "with warnings:" << result.err.trimmed();
    return true;
}

bool Archive::extractFresh(const QString &archive, const QString &dir, const QString &mustContain, QString *error)
{
    const auto staging = dir + ".partial"_L1;
    QDir{staging}.removeRecursively();

    if (!extract(archive, staging, error))
    {
        QDir{staging}.removeRecursively();
        return false;
    }
    if (!mustContain.isEmpty() && !QFileInfo::exists(staging + '/' + mustContain))
    {
        qCWarning(ArchiveLog) << archive << "has no" << mustContain;
        QDir{staging}.removeRecursively();
        *error = "That download has no %1 in it, so Kaon can't use it."_L1.arg(mustContain);
        return false;
    }
    if (QFileInfo::exists(dir) && !QDir{dir}.removeRecursively())
    {
        QDir{staging}.removeRecursively();
        *error = "Kaon couldn't replace %1."_L1.arg(dir);
        return false;
    }
    if (!QDir{}.rename(staging, dir))
    {
        QDir{staging}.removeRecursively();
        *error = "Kaon couldn't move the unpacked files to %1."_L1.arg(dir);
        return false;
    }
    return true;
}

bool Archive::extractFresh(const QByteArray &archiveData, const QString &dir, const QString &mustContain, QString *error)
{
    // Callers that download AFW/UEVR still hand over zip bytes; OptiScaler is saved to disk first as .7z.
    QTemporaryFile file{QDir::tempPath() + "/kaon-XXXXXX.zip"_L1};
    if (!file.open() || file.write(archiveData) != archiveData.size() || !file.flush())
    {
        qCWarning(ArchiveLog) << "Could not write" << file.fileName() << file.errorString();
        *error = "Kaon couldn't save the download to %1 to unpack it."_L1.arg(QDir::tempPath());
        return false;
    }
    return extractFresh(file.fileName(), dir, mustContain, error);
}

bool Archive::list(const QString &archive, QStringList *names, QString *error)
{
    const auto kind = kindOf(archive);
    const auto with = kind == Kind::SevenZip ? sevenZipUnpacker() : zipUnpacker();
    if (with.tool == Tool::None)
    {
        *error = kind == Kind::SevenZip ? noSevenZipUnpacker : noZipUnpacker;
        return false;
    }

    QStringList arguments;
    switch (with.tool)
    {
    case Tool::Unzip:
        arguments = {"-Z1"_L1, archive};
        break;
    case Tool::Bsdtar:
        arguments = {"-t"_L1, "-f"_L1, archive};
        break;
    case Tool::SevenZip:
        arguments = {"l"_L1, "-slt"_L1, archive};
        break;
    default:
        arguments = {"-I"_L1, "-c"_L1, pythonScript, "list"_L1, archive};
        break;
    }

    const auto result = run(with.program, arguments);
    if (!succeeded(with, result))
    {
        qCWarning(ArchiveLog).noquote() << "Listing" << archive << "failed with exit code" << result.exitCode
                                        << result.err.trimmed();
        *error = "Kaon couldn't read %1. The file may be damaged: delete that version in its version menu and "
                 "download it again."_L1.arg(QFileInfo{archive}.fileName());
        return false;
    }

    names->clear();
    if (with.tool == Tool::SevenZip)
    {
        // -slt prints "Path = name" lines; skip the archive itself.
        for (const auto &line : QString::fromUtf8(result.out).split('\n'_L1, Qt::SkipEmptyParts))
        {
            auto trimmed = line.endsWith('\r'_L1) ? line.chopped(1) : line;
            if (!trimmed.startsWith("Path = "_L1))
                continue;
            const auto name = trimmed.mid(7);
            if (!name.isEmpty() && name != QFileInfo{archive}.fileName())
                names->append(name);
        }
    }
    else
    {
        for (const auto &name : QString::fromUtf8(result.out).split('\n'_L1, Qt::SkipEmptyParts))
            names->append(name.endsWith('\r'_L1) ? name.chopped(1) : name);
    }
    return true;
}
