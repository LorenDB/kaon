#pragma once

#include "Mod.h"

class GitHubMod : public Mod
{
    Q_OBJECT

public slots:
    void downloadRelease(ModRelease *release) final;
    void deleteRelease(ModRelease *release) final;

protected:
    explicit GitHubMod(QObject *parent = nullptr);
    ~GitHubMod() = default;

    enum class Paths
    {
        ReleaseBasePath,
        CachedReleasesJSON,
    };
    QString path(const Paths p) const;
    QString pathForRelease(ModRelease *release, const ModRelease::Asset &asset) const;

    virtual QUrl githubUrl() const = 0;
    virtual bool isThisFileTheActualModDownload(const QString &file) const = 0;
    virtual ModRelease::Asset chooseAssetToInstall(const Game *game, const Game::LaunchOption &exe) const;
    // False for mods whose prereleases must never be offered, not even behind the version menu's switch
    virtual bool offersPrereleases() const { return true; }

private:
    virtual QList<ModRelease *> releases() const final { return m_releases; }

    void updateAvailableReleases();
    void parseReleaseInfoJson();
    bool showsPrereleases() const;
    // The release a mod starts out on when it has no saved one: the newest that the version menu shows
    ModRelease *newestOfferedRelease() const;

    QList<ModRelease *> m_releases;
};

class GitHubZipExtractorMod : public GitHubMod
{
    Q_OBJECT

public:
    bool hasRepairOption() const override { return false; }

protected:
    explicit GitHubZipExtractorMod(QObject *parent = nullptr);
    ~GitHubZipExtractorMod() = default;

    virtual QString modInstallDirForGame(const Game *game, const Game::LaunchOption &executable) const;

public slots:
    void uninstallMod(Game *game) override;

protected:
    void installModImpl(Game *game, const Game::LaunchOption &exe) override;
};
