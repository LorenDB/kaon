#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QSortFilterProxyModel>

class Steam : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Steam *instance();
    static Steam *create(QQmlEngine *qml, QJSEngine *js);

    enum Roles
    {
        Name = Qt::UserRole + 1,
        SteamID,
        InstallDir,
        CardImage,
        HeroImage,
        LogoImage,
        LastPlayed,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    struct Game
    {
        int id = 0;
        QString name;
        QString installDir;
        QString cardImage;
        QString heroImage;
        QString logoImage;
        QDateTime lastPlayed;
    };

    const Game &gameAtIndex(int index) const;

signals:

private:
    explicit Steam(QObject *parent = nullptr);
    static Steam *s_instance;

    void scanSteam();

    QList<Game> m_games;
};

class SteamFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool showAll READ showAll WRITE setShowAll NOTIFY showAllChanged FINAL)

public:
    explicit SteamFilter(QObject *parent = nullptr);

    bool showAll() const { return m_showAll; }

    void setShowAll(bool state);

signals:
    void showAllChanged(bool state);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    bool m_showAll{false};
};
