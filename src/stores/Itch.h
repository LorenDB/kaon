#include <QAbstractListModel>
#include <QObject>
#include <QQmlEngine>
#include <QQuickAsyncImageProvider>

#include "Game.h"

class Itch : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static Itch *instance();
    static Itch *create(QQmlEngine *qml, QJSEngine *js) { return instance(); }

    enum Roles
    {
        GameObject = Qt::UserRole + 1,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:

private:
    explicit Itch(QObject *parent = nullptr);
    static Itch *s_instance;

    void scanItch();

    QString m_itchRoot;
    QList<Game *> m_games;
};

class ItchImageCache : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
