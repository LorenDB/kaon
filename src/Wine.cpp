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
#include "WinePrefix.h"

Q_LOGGING_CATEGORY(WineLog, "wine")

namespace
{
    // The .NET host treats DOTNET_ROOT as the folder its runtime is installed in. Several distributions set it for
    // .NET on Linux in a profile script, and Wine hands it on, which sends UEVR's injector looking for hostfxr.dll
    // under /usr instead of in the prefix.
    bool isHostDotnetVariable(const QString &name)
    {
        return name.startsWith("DOTNET_ROOT"_L1) || name == "DOTNET_BUNDLE_EXTRACT_BASE_DIR"_L1;
    }

    // WINEESYNC, WINEFSYNC and their relatives decide how Wine processes wait on each other
    bool isSyncVariable(const QString &name)
    {
        return (name.startsWith("WINE"_L1) || name.startsWith("PROTON"_L1)) && name.contains("SYNC"_L1);
    }

    // A program has to make the same choice as the wineserver it joins, or Wine stops it at once with "Server is
    // running with WINEFSYNC but this process is not". running is the environment of a Wine process already in the
    // prefix. Without one there is nothing to match, and this is what Proton turns on by itself.
    QHash<QString, QString> syncVariables(const QHash<QString, QString> &running)
    {
        if (!running.contains("WINEPREFIX"_L1))
            return {{"WINEESYNC"_L1, "1"_L1}, {"WINEFSYNC"_L1, "1"_L1}};

        QHash<QString, QString> sync;
        for (auto it = running.cbegin(); it != running.cend(); ++it)
            if (isSyncVariable(it.key()))
                sync.insert(it.key(), it.value());
        return sync;
    }

    // Proton keeps a prefix in <compatdata>/<app id>/pfx, and this variable names the folder around it
    QString compatDataPath(const QString &prefix)
    {
        const QFileInfo info{prefix};
        return info.fileName() == "pfx"_L1 ? info.absolutePath() : prefix;
    }

    // Kaon's own environment, without what must not reach a Windows program it starts
    QProcessEnvironment hostEnvironment()
    {
        auto env = QProcessEnvironment::systemEnvironment();
        for (const auto &name : env.keys())
            if (isHostDotnetVariable(name) || isSyncVariable(name))
                env.remove(name);
        return env;
    }
} // namespace

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
                            const std::function<void()> &failureCallback,
                            const std::function<bool()> &verifySuccess)
{
    auto process = new QProcess;
    process->setProcessEnvironment(environment);
    // Like winetricks' w_try_cd, run installers from their own directory so
    // companion payloads and relative paths resolve even when Wine is handed
    // an absolute Unix path.
    if (QFileInfo cmdInfo{command}; cmdInfo.isAbsolute() && cmdInfo.dir().exists())
        process->setWorkingDirectory(cmdInfo.absolutePath());
    auto settled = std::make_shared<bool>(false);

    connect(process, &QProcess::finished, this, [=, this] {
        if (*settled)
            return;
        *settled = true;

        const auto code = process->exitCode();
        // Wine keeps only the low 8 bits of a Windows exit code, so e.g. 0x83 here
        // may be a truncated 0x583 rather than a literal 131.
        if (code == 0)
            successCallback();
        else if (verifySuccess && verifySuccess())
        {
            // Some installers report failure (truncated codes, reboot quirks) after
            // they already laid down their files. Trust the verification over the exit code.
            qCInfo(WineLog) << "Running" << command << "exited with" << code
                            << "but verification passed; treating as success";
            successCallback();
        }
        else
        {
            emit processFailed(prettyName);
            qCWarning(WineLog) << "Running" << command << "with Wine" << wineBinary << "failed with exit code" << code;
            if (const auto overrides = environment.value("WINEDLLOVERRIDES"); !overrides.isEmpty())
                qCWarning(WineLog) << "WINEDLLOVERRIDES is set:" << overrides;
            if (program == "flatpak"_L1)
                qCWarning(WineLog) << "flatpak enter failed. Injection needs a running game sandbox and working user "
                                      "namespaces";
            if (process->exitStatus() == QProcess::CrashExit)
                qCWarning(WineLog) << "Wine process crashed:" << process->errorString();
            const auto out = process->readAllStandardOutput().trimmed();
            if (!out.isEmpty())
                qCWarning(WineLog) << "Wine stdout:" << out;
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
                            const std::function<void()> &failureCallback,
                            const std::function<bool()> &verifySuccess)
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
    const auto running = WinePrefix::environmentOfProcess(pid);
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
                          "STEAM_COMPAT_DATA_PATH="_L1 +
                              running.value("STEAM_COMPAT_DATA_PATH"_L1, compatDataPath(sandboxPrefix)),
                          "PATH=/app/bin:/usr/bin:/bin"_L1};
    const auto sync = syncVariables(running);
    for (auto it = sync.cbegin(); it != sync.cend(); ++it)
        arguments << it.key() + '='_L1 + it.value();
    // Same .NET WPF locale / W^X precautions as runInWine (see above).
    arguments << "DOTNET_SYSTEM_GLOBALIZATION_PREDEFINED_CULTURES_ONLY=0"_L1
               << "DOTNET_EnableWriteXorExecute=0"_L1;
    arguments << sandboxWine << sandboxCommand;
    arguments += args;

    qCInfo(WineLog) << "Entering Flatpak pid" << pid << "to run" << sandboxCommand << "with" << sandboxWine;
    // flatpak enter hands its own environment on to the command
    startWineProcess("flatpak"_L1,
                     arguments,
                     hostEnvironment(),
                     prettyName,
                     command,
                     sandboxWine,
                     successCallback,
                     failureCallback,
                     verifySuccess);
}

void Wine::runInWine(const QString &prettyName,
                     const Game *wineRoot,
                     const QString &command,
                     const QStringList &args,
                     std::function<void()> successCallback,
                     std::function<void()> failureCallback,
                     bool inLauncherSandbox,
                     std::function<bool()> verifySuccess,
                     const QHash<QString, QString> &extraEnvironment)
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
        qCWarning(WineLog) << "Cannot run" << command << "for" << wineRoot->name() << "- Wine binary"
                           << wineRoot->wineBinary() << "or prefix" << wineRoot->winePrefix() << "does not exist";
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
                enterGameSandbox(pid,
                                 appId,
                                 sandboxPrefix,
                                 sandboxWine,
                                 prettyName,
                                 command,
                                 args,
                                 successCallback,
                                 failureCallback,
                                 verifySuccess);
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
        commandLog += ' '_L1 + args.join(' ');
    qCInfo(WineLog) << "Executing command" << commandLog << "via Wine" << wineRoot->wineBinary();

    auto env = hostEnvironment();

    // The game's own wineserver, when the game is already running
    const auto running = WinePrefix::runningEnvironment(wineRoot);
    const auto sync = syncVariables(running);
    for (auto it = sync.cbegin(); it != sync.cend(); ++it)
        env.insert(it.key(), it.value());
    for (auto it = extraEnvironment.cbegin(); it != extraEnvironment.cend(); ++it)
    {
        // Overrides somebody set for all of Wine stay, behind these: Wine lets the last mention of a DLL decide
        const auto before = it.key() == "WINEDLLOVERRIDES"_L1 ? env.value(it.key()) : QString{};
        env.insert(it.key(), before.isEmpty() ? it.value() : before + ';'_L1 + it.value());
    }
    env.insert("WINEPREFIX"_L1, wineRoot->winePrefix());
    env.insert("STEAM_COMPAT_DATA_PATH"_L1,
               running.value("STEAM_COMPAT_DATA_PATH"_L1, compatDataPath(wineRoot->winePrefix())));
    // .NET 6 WPF (UEVRInjector) crashes on non-English locales: XmlLanguage.GetSpecificCulture
    // cannot resolve hardcoded xml:lang="en-US" against Wine's NLS data. Relax culture matching.
    env.insert("DOTNET_SYSTEM_GLOBALIZATION_PREDEFINED_CULTURES_ONLY"_L1, "0"_L1);
    // CoreCLR Write-XOR-Execute is a known crash source under Wine's exception handling.
    env.insert("DOTNET_EnableWriteXorExecute"_L1, "0"_L1);
    qCInfo(WineLog) << (running.isEmpty() ? "Nothing is running in the prefix; using Proton's defaults:" :
                                            "Matching the Wine processes in the prefix:")
                    << sync;
    startWineProcess(wineRoot->wineBinary(),
                     QStringList{command} + args,
                     env,
                     prettyName,
                     command,
                     wineRoot->wineBinary(),
                     successCallback,
                     failureCallback,
                     verifySuccess);
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
