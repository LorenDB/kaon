#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>

class Steam : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Steam(QObject *parent = nullptr);

    enum Roles
    {
        Name = Qt::UserRole + 1,
        SteamID,
        InstallDir,
        CardImage,
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
        QDateTime lastPlayed;
    };

signals:

private:
    void scanSteam();

    QList<Game> m_games;
};
