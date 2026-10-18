#include "WivrnLauncher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(WivrnLog, "wivrn")

namespace
{
    QString currentExecutable()
    {
        // Inside an AppImage, applicationFilePath() is the temporary mount,
        // which is gone the next time WiVRn tries to launch Kaon.
        const QString appImage = qEnvironmentVariable("APPIMAGE");
        if (!appImage.isEmpty())
            return appImage;
        return QCoreApplication::applicationFilePath();
    }

    QString desktopExec(const QString &path)
    {
        if (!path.contains(QLatin1Char(' ')) && !path.contains(QLatin1Char('\t')))
            return path;
        QString escaped = path;
        escaped.replace("\""_L1, "\\\""_L1);
        return "\""_L1 + escaped + "\""_L1;
    }

    bool advertisesKaon(const QString &path)
    {
        QFile file{path};
        if (!file.open(QIODevice::ReadOnly))
            return false;
        const auto text = QString::fromUtf8(file.readAll());
        if (!text.contains("X-WiVRn-VR"_L1))
            return false;
        return text.contains("Name=Kaon"_L1) || text.contains("dev.lorendb.kaon"_L1);
    }

    bool execMatches(const QString &path, const QString &executable)
    {
        QFile file{path};
        if (!file.open(QIODevice::ReadOnly))
            return false;
        const auto text = QString::fromUtf8(file.readAll());
        return text.contains(executable);
    }

    void installIcon()
    {
        const QString iconDir =
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/icons/hicolor/scalable/apps"_L1;
        const QString iconPath = iconDir + "/kaon.svg"_L1;
        if (QFile::exists(iconPath))
            return;
        QFile source{":/qt/qml/dev/lorendb/kaon/qml/icons/kaon.svg"_L1};
        if (!source.open(QIODevice::ReadOnly))
            return;
        QDir{}.mkpath(iconDir);
        QFile destination{iconPath};
        if (!destination.open(QIODevice::WriteOnly))
            return;
        destination.write(source.readAll());
    }
} // namespace

void ensureWivrnLauncherEntry()
{
    const QString executable = currentExecutable();
    const QString userDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    const QString userFile = userDir + "/dev.lorendb.kaon.desktop"_L1;

    const auto dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const auto &dir : dirs)
    {
        QDir folder{dir};
        const auto names = folder.entryList(QStringList{"*kaon*.desktop"_L1}, QDir::Files);
        for (const auto &name : names)
        {
            const QString path = folder.filePath(name);
            if (QFileInfo{path} == QFileInfo{userFile})
                continue;
            // A system package (or another install) already registers Kaon
            // with WiVRn. A second user file would show up twice.
            if (advertisesKaon(path))
                return;
        }
    }

    if (advertisesKaon(userFile) && execMatches(userFile, executable))
        return;

    if (!QDir{}.mkpath(userDir))
    {
        qCWarning(WivrnLog) << "Could not create" << userDir;
        return;
    }

    QFile file{userFile};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        qCWarning(WivrnLog) << "Could not write the WiVRn launcher entry:" << file.errorString();
        return;
    }
    const auto text = QStringLiteral("[Desktop Entry]\n"
                                     "Name=Kaon\n"
                                     "Exec=%1\n"
                                     "Icon=kaon\n"
                                     "Comment=Use UEVR with Steam games via Proton\n"
                                     "GenericName=Kaon\n"
                                     "Terminal=false\n"
                                     "Type=Application\n"
                                     "Categories=Game;X-WiVRn-VR;\n")
                          .arg(desktopExec(executable));
    file.write(text.toUtf8());
    file.close();
    installIcon();
    qCInfo(WivrnLog) << "Wrote" << userFile << "so Kaon shows up in the WiVRn launcher";
}
