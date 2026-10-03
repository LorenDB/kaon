#include "Flatpak.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(FlatpakLog, "flatpak")

namespace
{
    void copyUpdated(const QString &source, const QString &dest)
    {
        const QFileInfo src{source};
        if (src.isSymLink() || !src.isDir())
        {
            const QFileInfo existing{dest};
            if (existing.exists() && existing.size() == src.size() && existing.lastModified() >= src.lastModified())
                return;
            QFile::remove(dest);
            if (!QFile::copy(source, dest))
                qCWarning(FlatpakLog) << "Could not stage" << source;
            return;
        }

        QDir{}.mkpath(dest);
        const auto entries = QDir{source}.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const auto &entry : entries)
            copyUpdated(entry.absoluteFilePath(), dest + '/' + entry.fileName());
    }
} // namespace

QString Flatpak::hostPath(const QString &appId, const QString &sandboxPath)
{
    if (appId.isEmpty() || sandboxPath.isEmpty() || QFileInfo::exists(sandboxPath))
        return sandboxPath;

    const auto home = QDir::homePath();
    if (!sandboxPath.startsWith(home + '/'))
        return sandboxPath;

    const auto rewritten = home + "/.var/app/"_L1 + appId + sandboxPath.sliced(home.size());
    if (QFileInfo::exists(rewritten))
        return rewritten;
    return sandboxPath;
}

QStringList Flatpak::prefixNeedles(const QString &appId, const QString &sandboxPrefix, const QString &hostPrefix)
{
    QStringList needles;
    const auto add = [&needles](const QString &path) {
        if (!path.isEmpty() && !needles.contains(path))
            needles << path;
    };
    add(sandboxPrefix);
    add(hostPrefix);

    const auto home = QDir::homePath();
    const auto marker = "/.var/app/"_L1 + appId;
    for (const auto &path : {sandboxPrefix, hostPrefix})
    {
        if (const auto at = path.indexOf(marker); at >= 0)
            add(home + path.sliced(at + marker.size()));
        else if (path.startsWith(home + '/'))
            add(home + marker + path.sliced(home.size()));
    }
    return needles;
}

QString Flatpak::stageTree(const QString &appId, const QString &sourceDir)
{
    const QFileInfo source{sourceDir};
    if (appId.isEmpty() || !source.isDir())
        return {};

    const auto relative = source.dir().dirName() + '/' + source.fileName();
    const auto hostDest = QDir::homePath() + "/.var/app/"_L1 + appId + "/.local/share/kaon/staged/"_L1 + relative;
    if (!QDir{}.mkpath(hostDest))
    {
        qCWarning(FlatpakLog) << "Could not create" << hostDest;
        return {};
    }

    copyUpdated(source.absoluteFilePath(), hostDest);
    // --persist=. mounts ~/.var/app/<id> as $HOME, so this host directory is
    // $HOME/.local/share/kaon/staged/... inside the sandbox.
    return QDir::homePath() + "/.local/share/kaon/staged/"_L1 + relative;
}

qint64 Flatpak::findGamePid(const QStringList &needles)
{
    QStringList useful;
    for (const auto &needle : needles)
        if (!needle.isEmpty())
            useful << needle;
    if (useful.isEmpty())
        return 0;

    qint64 best = 0;
    int bestScore = 0;
    const auto entries = QDir{"/proc"_L1}.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &name : entries)
    {
        bool ok = false;
        const auto pid = name.toLongLong(&ok);
        if (!ok || pid <= 1)
            continue;

        QFile cgroup{"/proc/"_L1 + name + "/cgroup"_L1};
        if (!cgroup.open(QIODevice::ReadOnly))
            continue;
        const auto cgroupText = cgroup.readAll();
        if (!cgroupText.contains("flatpak"))
            continue;

        QFile environFile{"/proc/"_L1 + name + "/environ"_L1};
        if (!environFile.open(QIODevice::ReadOnly))
            continue;
        const auto environ = environFile.read(256 * 1024);
        QByteArray cmdline;
        QFile cmdlineFile{"/proc/"_L1 + name + "/cmdline"_L1};
        if (cmdlineFile.open(QIODevice::ReadOnly))
            cmdline = cmdlineFile.readAll();

        // The launcher UI can mention a prefix in its own files. Only a wine process
        // (or pressure-vessel wrapping one) shares the game's wineserver.
        const bool wineProcess = environ.contains("WINEPREFIX=") || cmdline.contains("wineserver") ||
                                 cmdline.contains("wine64") || cmdline.contains("wine-preloader") ||
                                 cmdline.contains("pressure-vessel") || cmdline.contains("pv-adverb");
        if (!wineProcess)
            continue;

        int score = 0;
        const auto blob = environ + cmdline;
        for (const auto &needle : useful)
            if (blob.contains(needle.toUtf8()))
                score += 2;
        if (score == 0)
            continue;
        if (cmdline.contains("wineserver") || cmdline.contains("wine-preloader") || cmdline.contains("wine64"))
            score += 3;
        if (score > bestScore || (score == bestScore && pid > best))
        {
            bestScore = score;
            best = pid;
        }
    }
    return best;
}
