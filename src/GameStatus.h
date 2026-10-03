#pragma once

#include <QHash>
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

    // "ready", "setup", "native" (the game has its own VR mode) or "none" (no mod can make it VR)
    Q_INVOKABLE QString group(Game *game) const;

    // One map per check: key, title, detail, state ("ok", "warn", "todo", "busy", "wait" or "block"), and optionally
    // action/actionLabel and secondaryAction/secondaryLabel for runStep().
    Q_INVOKABLE QVariantList steps(Game *game) const;

    // A short line describing the most important thing about the game's status
    Q_INVOKABLE QString summary(Game *game) const;

    // Mods that can make this game play in VR, and the one Play in VR uses
    Q_INVOKABLE QList<Mod *> vrMods(Game *game) const;
    Q_INVOKABLE Mod *preferredMod(Game *game) const;
    Q_INVOKABLE void setPreferredMod(Game *game, Mod *mod);

    // Compatible mods that don't provide VR and aren't needed by the preferred mod, like add-ons
    Q_INVOKABLE QList<Mod *> extraMods(Game *game) const;
    Q_INVOKABLE QList<Mod *> allMods() const;

    Q_INVOKABLE void runStep(Game *game, const QString &key, bool secondary = false);
    // Works through every step Kaon can do by itself, one after another
    Q_INVOKABLE void setUp(Game *game);
    Q_INVOKABLE bool canSetUp(Game *game) const;

    Q_INVOKABLE void download(Mod *mod, ModRelease *release);
    Q_INVOKABLE bool isDownloading(Mod *mod, ModRelease *release) const;

    Q_INVOKABLE void rescanLibraries();

signals:
    void changed();
    void chooseExecutable(GameExecutablePickerModel *model);

private:
    explicit GameStatus(QObject *parent = nullptr);

    void invalidate();
    void advanceSetUp();
    Mod *modForKey(const QString &key) const;
    QVariantMap findStep(Game *game, const QString &key) const;
    static QString downloadKey(Mod *mod, ModRelease *release);

    int m_revision{0};
    bool m_changePending{false};

    mutable QHash<const Game *, QVariantList> m_stepsCache;

    // "<mod>/<release id>" -> release name, for downloads Kaon started and hasn't seen finish
    QHash<QString, QString> m_pendingDownloads;
    QHash<Mod *, QPointer<Game>> m_installAfterDownload;

    QPointer<Game> m_setUpGame;
    QString m_setUpLastKey;
};
