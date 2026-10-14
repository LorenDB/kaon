#include "UevrProfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSet>
#include <QTemporaryDir>

#include "Archive.h"
#include "Game.h"
#include "UevrPlugins.h"

Q_LOGGING_CATEGORY(UevrProfilesLog, "uevr.profiles")

namespace
{
    // A zip entry with forward slashes. UEVR profiles are zipped on Windows, so entries may use backslashes.
    QString normalized(QString entry)
    {
        return entry.replace('\\'_L1, '/'_L1);
    }

    bool isConfigFile(const QString &entry)
    {
        return normalized(entry).endsWith("/config.txt"_L1, Qt::CaseInsensitive) ||
               normalized(entry).compare("config.txt"_L1, Qt::CaseInsensitive) == 0;
    }

    bool isDll(const QString &entry)
    {
        return normalized(entry).endsWith(".dll"_L1, Qt::CaseInsensitive);
    }

    // The folder the profile's files sit under, extracted from the zip's own listing: a single top-level
    // folder is stripped (UEVR only strips it when it matches the zip's name; anything else can only be
    // wrapping), a single nested zip is followed the way UEVR's import does, and anything else is flat.
    // Returns the prefix to drop, or -1 for "follow nestedZipName instead".
    struct Unpacked
    {
        QString stripPrefix;
        QString nestedZip;
    };

    Unpacked unpackedLayout(const QStringList &entries)
    {
        QSet<QString> tops;
        QString singleFile;
        int files = 0;
        for (const auto &raw : entries)
        {
            const auto entry = normalized(raw);
            if (entry.isEmpty() || entry.endsWith('/'_L1))
                continue;
            ++files;
            singleFile = entry;
            const auto slash = entry.indexOf('/'_L1);
            tops.insert(slash < 0 ? QString{} : entry.left(slash));
        }
        // A zip holding only another zip. UEVR's import follows one level in.
        if (files == 1 && singleFile.endsWith(".zip"_L1, Qt::CaseInsensitive))
            return {.nestedZip = singleFile};
        if (tops.size() != 1)
            return {};
        const auto top = *tops.cbegin();
        if (top.isEmpty())
            return {};
        return {.stripPrefix = top + '/'_L1};
    }

    // Moves a tree that was spelled with backslashes (a zip made on Windows, unpacked by a tool that keeps
    // them literally) to forward-slash paths, so uobjecthook\1_props.json lands in uobjecthook/.
    bool normalizeSeparators(const QString &root, QString *error)
    {
        // Depth-first, so a renamed directory is never walked into under its old name
        const auto entries = QDir{root}.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const auto &name : entries)
        {
            const auto path = root + '/'_L1 + name;
            if (QFileInfo{path}.isDir() && !normalizeSeparators(path, error))
                return false;
            if (!name.contains('\\'_L1))
                continue;
            const auto target = QDir::cleanPath(root + '/'_L1 + QString{name}.replace('\\'_L1, '/'_L1));
            if (!QDir{}.mkpath(QFileInfo{target}.absolutePath()) || !QFile::rename(path, target))
            {
                qCWarning(UevrProfilesLog) << "Could not normalize" << path;
                *error = "Kaon couldn't unpack that profile."_L1;
                return false;
            }
        }
        return true;
    }

    // Copies a file over one that may already be there. QFile::copy won't overwrite, and the old file may be
    // the profile being replaced.
    bool copyOver(const QString &from, const QString &to, QString *error)
    {
        if (QFileInfo::exists(to) && !QFile::remove(to))
        {
            qCWarning(UevrProfilesLog) << "Could not replace" << to;
            *error = "Kaon couldn't replace the profile in this game's prefix. Quit the game and try again."_L1;
            return false;
        }
        if (!QFile::copy(from, to))
        {
            qCWarning(UevrProfilesLog) << "Could not copy" << from << "to" << to;
            *error = "Kaon couldn't write this game's Proton prefix."_L1;
            return false;
        }
        return true;
    }

    bool copyTree(const QString &from, const QString &to, QString *error)
    {
        if (!QDir{}.mkpath(to))
        {
            *error = "Kaon couldn't write this game's Proton prefix."_L1;
            return false;
        }
        const auto entries = QDir{from}.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const auto &name : entries)
        {
            const auto source = from + '/'_L1 + name;
            const auto target = to + '/'_L1 + name;
            if (QFileInfo{source}.isDir())
            {
                if (!copyTree(source, target, error))
                    return false;
            }
            else if (!copyOver(source, target, error))
                return false;
        }
        return true;
    }
} // namespace

QStringList UevrProfiles::profileDirsForGame(const Game *game, bool create, QString *error)
{
    if (!game)
    {
        if (error)
            *error = "This game has no Wine prefix."_L1;
        return {};
    }
    const auto stems = UevrPlugins::exeStems(game);
    if (stems.isEmpty())
    {
        if (error)
            *error = "%1 has no Windows executable for a UEVR profile to attach to."_L1.arg(game->name());
        return {};
    }

    QString roaming = UevrPlugins::roamingDir(game, create, error);
    if (roaming.isEmpty())
        return {};

    QStringList dirs;
    for (const auto &stem : std::as_const(stems))
    {
        const auto dir = roaming + "/UnrealVRMod/"_L1 + stem;
        if (create)
        {
            if (!QDir{}.mkpath(dir))
            {
                qCWarning(UevrProfilesLog) << "Could not create" << dir;
                if (error)
                    *error = "Kaon couldn't write this game's Proton prefix."_L1;
                return {};
            }
        }
        else if (!QFileInfo{dir}.isDir())
            continue;
        dirs << QDir::cleanPath(dir);
    }
    return dirs;
}

QString UevrProfiles::installedProfileName(const Game *game)
{
    for (const auto &dir : profileDirsForGame(game, false))
    {
        // The file system may spell the name differently; match case-insensitively like Windows does
        for (const auto &entry : QDir{dir}.entryList(QDir::Files))
        {
            if (entry.compare("config.txt"_L1, Qt::CaseInsensitive) == 0)
                return QDir{dir}.dirName();
        }
    }
    return {};
}

bool UevrProfiles::zipContainsDll(const QString &localPath)
{
    if (localPath.isEmpty() || !QFileInfo{localPath}.isFile())
        return false;
    QStringList entries;
    QString error;
    if (!Archive::list(localPath, &entries, &error))
        return false;
    return std::any_of(entries.cbegin(), entries.cend(), [](const QString &entry) { return isDll(entry); });
}

QString UevrProfiles::importProfile(Game *game, const QUrl &source)
{
    const auto local = source.isLocalFile() ? source.toLocalFile() : source.toString();
    const QFileInfo picked{local};
    if (local.isEmpty() || !picked.isFile())
        return "Kaon couldn't read that file."_L1;
    if (picked.suffix().compare("zip"_L1, Qt::CaseInsensitive) != 0)
        return "Pick a UEVR profile ZIP. \"%1\" isn't one."_L1.arg(picked.fileName());

    QString error;
    const auto dirs = profileDirsForGame(game, true, &error);
    if (dirs.isEmpty())
        return error.isEmpty() ? "Kaon couldn't find a UEVR profile folder in this game's prefix."_L1 : error;

    QTemporaryDir scratch;
    if (!scratch.isValid())
        return "Kaon couldn't unpack that profile."_L1;

    // A profile may wrap one zip in another, the way UEVR's own import handles it. Follow at most two.
    QString archive = picked.absoluteFilePath();
    QString root = scratch.path();
    bool settled = false;
    for (int level = 0; level < 3; ++level)
    {
        QStringList entries;
        if (!Archive::list(archive, &entries, &error))
        {
            qCWarning(UevrProfilesLog) << "Could not list" << archive << error;
            return "Kaon couldn't read %1 as a zip file."_L1.arg(picked.fileName());
        }
        if (!std::any_of(entries.cbegin(), entries.cend(), [](const QString &entry) { return isConfigFile(entry); }) &&
            unpackedLayout(entries).nestedZip.isEmpty())
            return "That ZIP has no config.txt in it, so Kaon can't use it as a UEVR profile."_L1;

        if (!Archive::extract(archive, root, &error))
        {
            qCWarning(UevrProfilesLog) << "Could not unpack" << archive << error;
            return "Kaon couldn't unpack that profile."_L1;
        }
        if (!normalizeSeparators(root, &error))
            return error;

        const auto layout = unpackedLayout(entries);
        if (layout.nestedZip.isEmpty())
        {
            root = QDir::cleanPath(root + '/'_L1 + layout.stripPrefix);
            settled = true;
            break;
        }
        archive = QDir::cleanPath(root + '/'_L1 + layout.nestedZip);
        root = QDir::cleanPath(root + "/nested"_L1);
        if (!QFileInfo{archive}.isFile())
            return "That ZIP has no config.txt in it, so Kaon can't use it as a UEVR profile."_L1;
    }
    if (!settled)
        return "That ZIP has no config.txt in it, so Kaon can't use it as a UEVR profile."_L1;

    for (const auto &dir : dirs)
    {
        if (!copyTree(root, dir, &error))
            return error;
    }
    qCInfo(UevrProfilesLog) << "Imported UEVR profile" << picked.fileName() << "for" << (game ? game->name() : QString{});
    return {};
}

QString UevrProfiles::removeProfile(Game *game)
{
    bool removedAny = false;
    for (const auto &dir : profileDirsForGame(game, false))
    {
        for (const auto &name : QDir{dir}.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        {
            // The profile shares its folder with installed plugins and engine dumps; those stay
            if (name.compare("plugins"_L1, Qt::CaseInsensitive) == 0 || name.compare("sdkdump"_L1, Qt::CaseInsensitive) == 0)
                continue;
            const auto path = dir + '/'_L1 + name;
            const bool gone = QFileInfo{path}.isDir() ? QDir{path}.removeRecursively() : QFile::remove(path);
            if (!gone)
            {
                qCWarning(UevrProfilesLog) << "Could not remove" << path;
                return "Kaon couldn't remove that profile. Quit the game and try again."_L1;
            }
            removedAny = true;
        }
        if (QDir{dir}.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty())
            QDir{}.rmdir(dir);
    }
    return removedAny ? QString{} : "Kaon couldn't find a profile to remove."_L1;
}

QString UevrProfiles::manageHoldReason(const Game *game)
{
    if (!game)
        return {};
    if (!game->hasValidWine())
        return game->store() == Game::Store::Custom ?
                   "Set this game's Wine prefix before importing a profile."_L1 :
                   "Launch the game once so it has a Proton prefix for Kaon to import a profile into."_L1;
    if (UevrPlugins::exeStems(game).isEmpty())
        return "UEVR profiles attach to a game's Windows build, and this game doesn't ship one Kaon can find."_L1;
    return {};
}
