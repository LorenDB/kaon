#include "Wine.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTimer>

#include <memory>

#include "Aptabase.h"
#include "Flatpak.h"
#include "Heroic.h"
#include "Steam.h"

Q_LOGGING_CATEGORY(WineLog, "wine")

Wine::Wine(QObject *parent)
    : QObject{parent}
{}

Wine *Wine::instance()
{
    static auto w = new Wine;
    return w;
}

Wine *Wine::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

void Wine::startWineProcess(const QString &program,
                            const QStringList &arguments,
                            const QProcessEnvironment &environment,
                            const QString &prettyName,
                            const QString &command,
                            const QString &wineBinary,
                            const std::function<void()> &successCallback,
                            const std::function<void()> &failureCallback)
{
    auto process = new QProcess;
    process->setProcessEnvironment(environment);
    auto settled = std::make_shared<bool>(false);

    connect(process, &QProcess::finished, this, [=, this] {
        if (*settled)
            return;
        *settled = true;

        const auto code = process->exitCode();
        // Wine keeps only the low 8 bits of a Windows exit code. For the .NET installer,
        // 0x42 is 1602 (closed by the user), 0x69 is 1641 and 0xC2 is 3010 (installed, reboot wanted).
        const bool dotnetAccepted = command.endsWith("windowsdesktop-runtime-6.0.36-win-x64.exe"_L1) &&
                                    (code == 0x42 || code == 0x69 || code == 0xC2);
        if (code == 0 || dotnetAccepted)
            successCallback();
        else
        {
            emit processFailed(prettyName);
            qCWarning(WineLog) << "Running" << command << "with Wine" << wineBinary << "failed with exit code" << code;
            if (program == "flatpak"_L1)
                qCWarning(WineLog) << "flatpak enter failed. Injection needs a running game sandbox and working user "
                                      "namespaces";
            if (process->exitStatus() == QProcess::CrashExit)
                qCWarning(WineLog) << "Wine process crashed:" << process->errorString();
            const auto err = process->readAllStandardError().trimmed();
            if (!err.isEmpty())
                qCWarning(WineLog) << "Wine stderr:" << err;
            failureCallback();
        }
    });
    connect(process, &QProcess::errorOccurred, this, [=, this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || *settled)
            return;
        *settled = true;
        qCWarning(WineLog) << "Could not start" << program << process->errorString();
        emit processFailed(prettyName);
        failureCallback();
        process->deleteLater();
    });
    connect(process, &QProcess::finished, process, &QObject::deleteLater);

    process->start(program, arguments);
}

void Wine::enterGameSandbox(qint64 pid,
                            const QString &appId,
                            const QString &sandboxPrefix,
                            const QString &sandboxWine,
                            const QString &prettyName,
                            const QString &command,
                            const QStringList &args,
                            const std::function<void()> &successCallback,
                            const std::function<void()> &failureCallback)
{
    const QFileInfo injector{command};
    const auto stagedDir = Flatpak::stageTree(appId, injector.absolutePath());
    if (stagedDir.isEmpty())
    {
        qCWarning(WineLog) << "Could not stage" << command << "for" << appId;
        emit processFailed(prettyName);
        failureCallback();
        return;
    }

    const auto sandboxCommand = stagedDir + '/'_L1 + injector.fileName();
    const auto home = QDir::homePath();
    // flatpak enter does not copy the app environment, and the command has to run in the
    // game's subsandbox. The launcher's own /tmp is not the game's /tmp.
    QStringList arguments{"enter"_L1,
                          QString::number(pid),
                          "/bin/sh"_L1,
                          "-c"_L1,
                          "cd \"$1\" && shift && exec \"$@\""_L1,
                          "sh"_L1,
                          home,
                          "/usr/bin/env"_L1,
                          "-u"_L1,
                          "LD_PRELOAD"_L1,
                          "HOME="_L1 + home,
                          "USER="_L1 + qEnvironmentVariable("USER"),
                          "WINEPREFIX="_L1 + sandboxPrefix,
                          "STEAM_COMPAT_DATA_PATH="_L1 + sandboxPrefix,
                          "WINEFSYNC=1"_L1,
                          "PATH=/app/bin:/usr/bin:/bin"_L1,
                          sandboxWine,
                          sandboxCommand};
    arguments += args;

    qCInfo(WineLog) << "Entering Flatpak pid" << pid << "to run" << sandboxCommand << "with" << sandboxWine;
    startWineProcess("flatpak"_L1,
                     arguments,
                     QProcessEnvironment::systemEnvironment(),
                     prettyName,
                     command,
                     sandboxWine,
                     successCallback,
                     failureCallback);
}

void Wine::runInWine(const QString &prettyName,
                     const Game *wineRoot,
                     const QString &command,
                     const QStringList &args,
                     std::function<void()> successCallback,
                     std::function<void()> failureCallback,
                     bool inLauncherSandbox)
{
    if (!wineRoot)
    {
        Aptabase::instance()->track("null-wine-game-bug"_L1, {{"command"_L1, command}});
        emit processFailed(prettyName);
        failureCallback();
        return;
    }
    else if (!wineRoot->hasValidWine())
    {
        Aptabase::instance()->track(
            "empty-wine-binary-bug"_L1,
            {{"command"_L1, command},
             {"game"_L1, wineRoot->id()},
             {"store"_L1, QMetaEnum::fromType<Game::Store>().valueToKey(static_cast<quint64>(wineRoot->store()))}});
        emit processFailed(prettyName);
        failureCallback();
        return;
    }

    if (command.isEmpty())
    {
        failureCallback();
        return;
    }

    if (inLauncherSandbox && !wineRoot->flatpakAppId().isEmpty())
    {
        const auto appId = wineRoot->flatpakAppId();
        const auto sandboxPrefix = wineRoot->sandboxWinePrefix();
        const auto hostPrefix = wineRoot->winePrefix();
        const auto sandboxWine = wineRoot->sandboxWineBinary();
        const auto needles = Flatpak::prefixNeedles(appId, sandboxPrefix, hostPrefix);
        qCInfo(WineLog) << "Waiting for" << wineRoot->name() << "in" << appId;

        auto pending = new QObject{this};
        auto timer = new QTimer{pending};
        auto attempts = std::make_shared<int>(0);
        auto done = std::make_shared<bool>(false);
        timer->setInterval(500);
        const auto poll = [=, this] {
            if (*done)
                return;
            if (const auto pid = Flatpak::findGamePid(needles))
            {
                *done = true;
                timer->stop();
                pending->deleteLater();
                enterGameSandbox(
                    pid, appId, sandboxPrefix, sandboxWine, prettyName, command, args, successCallback, failureCallback);
                return;
            }
            if (++(*attempts) >= 40)
            {
                *done = true;
                timer->stop();
                pending->deleteLater();
                qCWarning(WineLog) << "Timed out waiting for a wine process in" << appId << "using prefix" << hostPrefix
                                   << "(the game sandbox is not running, or user namespaces are disabled)";
                emit processFailed(prettyName);
                failureCallback();
            }
        };
        connect(timer, &QTimer::timeout, pending, poll);
        poll();
        if (!*done)
            timer->start();
        return;
    }

    QString commandLog = command;
    if (!args.empty())
        commandLog += args.join(' ');
    qCInfo(WineLog) << "Executing command" << commandLog << "via Wine" << wineRoot->wineBinary();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("WINEPREFIX"_L1, wineRoot->winePrefix());
    env.insert("STEAM_COMPAT_DATA_PATH"_L1, wineRoot->winePrefix());
    env.insert("WINEFSYNC"_L1, "1"_L1);
    startWineProcess(wineRoot->wineBinary(),
                     QStringList{command} + args,
                     env,
                     prettyName,
                     command,
                     wineRoot->wineBinary(),
                     successCallback,
                     failureCallback);
}

QString Wine::whichWine() const
{
    QString result;

    QProcess whichWineProc;
    whichWineProc.start("which"_L1, {"wine"_L1});
    whichWineProc.waitForFinished();
    if (whichWineProc.exitCode() == 0)
        result = whichWineProc.readAllStandardOutput().trimmed();

    // If we can't find a system Wine, we might be able to piggyback off Proton installs from Steam or Heroic
    if (result.isEmpty())
        for (const auto g : Heroic::instance()->games() + Steam::instance()->games())
            if (!g->wineBinary().isEmpty())
                result = g->wineBinary();

    return result;
}

QString Wine::defaultWinePrefix() const
{
    return qEnvironmentVariable("WINEPREFIX", QDir::homePath() + "/.wine"_L1);
}
