#pragma once

#include <QString>
#include <QStringList>

// Unpacks the zip files mods are shipped as. Kaon has no zip code of its own: it runs unzip, or bsdtar or Python on
// systems that don't install unzip.
namespace Archive
{
    // Extracts into dest, replacing files that are already there. An empty member list extracts everything; members
    // may use * as a wildcard. On failure, error is a sentence meant for the user.
    bool extract(const QString &zip, const QString &dest, const QStringList &members, QString *error);
    inline bool extract(const QString &zip, const QString &dest, QString *error)
    {
        return extract(zip, dest, {}, error);
    }

    // Unpacks a whole zip into dir by way of a scratch folder beside it, so a failure never leaves half a release behind
    // that looks complete. Whatever dir held before is replaced. mustContain names a file the zip has to provide.
    bool extractFresh(const QString &zip, const QString &dir, const QString &mustContain, QString *error);
    // The same for a zip that is still in memory, as a download is when it arrives
    bool extractFresh(const QByteArray &zipData, const QString &dir, const QString &mustContain, QString *error);

    // File and directory names inside the zip. Directories end in '/'.
    bool list(const QString &zip, QStringList *names, QString *error);
} // namespace Archive
