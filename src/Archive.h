#pragma once

#include <QString>
#include <QStringList>

// Unpacks the archives mods are shipped as (zip, and 7z for OptiScaler). Kaon has no archive code of its own: it
// runs unzip/bsdtar/python3 for zip, and 7z or 7za for 7z.
namespace Archive
{
    // Extracts into dest, replacing files that are already there. An empty member list extracts everything; members
    // may use * as a wildcard. On failure, error is a sentence meant for the user.
    bool extract(const QString &archive, const QString &dest, const QStringList &members, QString *error);
    inline bool extract(const QString &archive, const QString &dest, QString *error)
    {
        return extract(archive, dest, {}, error);
    }

    // Unpacks a whole archive into dir by way of a scratch folder beside it, so a failure never leaves half a release
    // behind that looks complete. Whatever dir held before is replaced. mustContain names a file the archive has to
    // provide.
    bool extractFresh(const QString &archive, const QString &dir, const QString &mustContain, QString *error);
    // The same for an archive that is still in memory, as a download is when it arrives
    bool extractFresh(const QByteArray &archiveData, const QString &dir, const QString &mustContain, QString *error);

    // File and directory names inside the archive. Directories end in '/'.
    bool list(const QString &archive, QStringList *names, QString *error);
} // namespace Archive
