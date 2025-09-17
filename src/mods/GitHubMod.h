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
    ~GitHubMod() {}

    enum class Paths
    {
        CurrentRelease,
        ReleaseBasePath,
        CachedReleasesJSON,
    };
    QString path(const Paths p) const;
    QString pathForRelease(int id) const;

    virtual QUrl githubUrl() const = 0;
    virtual bool isThisFileTheActualModDownload(const QString &file) const = 0;

private:
    virtual QList<ModRelease *> releases() const final { return m_releases; }

    void updateAvailableReleases();
    void parseReleaseInfoJson();

    QList<ModRelease *> m_releases;
};

class GitHubZipExtractorMod : public GitHubMod
{
    Q_OBJECT

public:
    bool hasRepairOption() const override { return false; }

protected:
    explicit GitHubZipExtractorMod(QObject *parent = nullptr);
    ~GitHubZipExtractorMod() {}

    virtual QString modInstallDirForGame(const Game *game, const Game::LaunchOption &executable) const;

public slots:
    void installMod(Game *game) override;
    void uninstallMod(Game *game) override;
};
