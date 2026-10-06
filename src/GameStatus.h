#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>

#include "Game.h"
#include "Mod.h"

class GameExecutablePickerModel;

// Works out what stands between a game and playing it in VR. The UI groups the library by this and shows the steps as a
// checklist, each with the action that fixes it.
class GameStatus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Bumped whenever anything that feeds into the status of any game changes. Bindings read it to re-evaluate.
    Q_PROPERTY(int revision READ revision NOTIFY changed FINAL)

public:
    static GameStatus *instance();
    static GameStatus *create(QQmlEngine *, QJSEngine *) { return instance(); }

    void watch(Mod *mod);

    int revision() const { return m_revision; }

    // The answers below are worked out from files on disk, so QML can't tell when they change. A binding that calls one
    // of these passes GameStatus.revision as the last argument. The functions ignore it; naming it is what makes the
    // binding run again whenever any status changes. Reading the property without using it is not enough: the QML
    // compiler drops a read whose value goes nowhere, and the binding then never updates.

    // "ready", "setup", "native" (the game has its own VR mode) or "none" (no mod can make it VR)
    Q_INVOKABLE QString group(Game *game, int revision = 0) const;

    // One map per check: key, title, detail, state ("ok", "warn", "todo", "busy", "wait" or "block"), and optionally
    // action/actionLabel and secondaryAction/secondaryLabel for runStep(), and copyText for something to paste elsewhere.
    Q_INVOKABLE QVariantList steps(Game *game, int revision = 0) const;

    // A short line describing the most important thing about the game's status
    Q_INVOKABLE QString summary(Game *game, int revision = 0) const;

    // Mods that can make this game play in VR, and the one Play in VR uses
    Q_INVOKABLE QList<Mod *> vrMods(Game *game, int revision = 0) const;
    Q_INVOKABLE Mod *preferredMod(Game *game, int revision = 0) const;
    Q_INVOKABLE void setPreferredMod(Game *game, Mod *mod);

    // Compatible mods that don't provide VR and aren't needed by the preferred mod, like add-ons
    QList<Mod *> extraMods(Game *game) const;
    // The same mods as rows for the game page, one map each: mod, on, working, enabled and detail
    Q_INVOKABLE QVariantList tools(Game *game, int revision = 0) const;
    // Turns one of them on or off for the game, downloading it first when that is needed
    Q_INVOKABLE void toggleTool(Game *game, Mod *mod);
    Q_INVOKABLE QList<Mod *> allMods() const;

    // Whether an installable mod is in the game, and which version Kaon put there
    Q_INVOKABLE bool isInstalled(Mod *mod, Game *game, int revision = 0) const;
    Q_INVOKABLE ModRelease *installedRelease(Mod *mod, Game *game, int revision = 0) const;
    // Whether Play in VR for this game starts with a countdown that its mod's delay sets
    Q_INVOKABLE bool usesLaunchDelay(Game *game, int revision = 0) const;

    // What the mods in use for this game need it to be started with, as one line of launch options
    QString launchOptions(const Game *game) const;
    // Opens the game's properties window in the Steam it was installed with
    Q_INVOKABLE void openSteamProperties(Game *game);
    Q_INVOKABLE void openFolder(Game *game);

    // Downloads the current release when it isn't on disk, then installs it for this game.
    Q_INVOKABLE void installForGame(Game *game, Mod *mod);

    Q_INVOKABLE void runStep(Game *game, const QString &key, bool secondary = false);
    // Works through every step Kaon can do by itself, one after another
    Q_INVOKABLE void setUp(Game *game);
    Q_INVOKABLE bool canSetUp(Game *game, int revision = 0) const;

    Q_INVOKABLE void download(Mod *mod, ModRelease *release);
    Q_INVOKABLE bool isDownloading(Mod *mod, ModRelease *release, int revision = 0) const;

    Q_INVOKABLE void rescanLibraries();

    // Setup and installs that were waiting on a game follow the object that replaced it.
    void retargetGames(const QList<Game *> &previous, const QList<Game *> &current);

signals:
    void changed();
    void chooseExecutable(GameExecutablePickerModel *model);
    // A step's action didn't work, and the checklist can't say why
    void actionFailed(const QString &title, const QString &message);
    // Something worth a line in the status strip, not a dialog
    void noticed(const QString &text);

private:
    explicit GameStatus(QObject *parent = nullptr);

    void invalidate();
    void advanceSetUp();
    Mod *modForKey(const QString &key) const;
    QVariantMap findStep(Game *game, const QString &key) const;
    // The mods whose launch options this game needs: the one it is played with, and the extras that are on
    QList<Mod *> modsWithLaunchOptions(Game *game) const;
    // Empty when the game needs no launch options. Carries "copyText" when there is something to paste.
    QVariantMap launchOptionsStep(Game *game) const;
    static QString downloadKey(Mod *mod, ModRelease *release);

    int m_revision{0};
    bool m_changePending{false};

    mutable QHash<const Game *, QVariantList> m_stepsCache;

    // "<mod>/<release id>" -> release name, for downloads Kaon started and hasn't seen finish
    QHash<QString, QString> m_pendingDownloads;
    QHash<Mod *, QPointer<Game>> m_installAfterDownload;

    QPointer<Game> m_setUpGame;
    QString m_setUpLastKey;

    // As of the last look, to tell when a game has started or quit
    QStringList m_prefixesInUse;
};
