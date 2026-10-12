#pragma once

#include <QJsonDocument>
#include <QNetworkRequest>
#include <QObject>
#include <QQmlEngine>

class QNetworkReply;

// GitHub's API, shared by every release list. Unauthenticated calls are limited to a few dozen an hour for the whole
// machine, and a 403 used to be easy to mistake for a fatal error. A token raises that limit. It is only sent to
// api.github.com.
class GitHub : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString token READ token WRITE setToken NOTIFY tokenChanged FINAL)
    // False when the saved token is empty or contains characters that must not go in an HTTP header.
    Q_PROPERTY(bool tokenUsable READ tokenUsable NOTIFY tokenChanged FINAL)
    Q_PROPERTY(bool rateLimited READ rateLimited NOTIFY rateLimitedChanged FINAL)
    Q_PROPERTY(bool tokenRejected READ tokenRejected NOTIFY tokenRejectedChanged FINAL)
    // Words for the rate-limit dialog. Empty when GitHub has not asked Kaon to slow down.
    Q_PROPERTY(QString rateLimitDetail READ rateLimitDetail NOTIFY rateLimitedChanged FINAL)

public:
    static GitHub *instance();
    static GitHub *create(QQmlEngine *, QJSEngine *);

    QString token() const { return m_token; }
    void setToken(const QString &token);
    bool tokenUsable() const;
    bool rateLimited() const { return m_rateLimited; }
    bool tokenRejected() const { return m_tokenRejected; }
    QString rateLimitDetail() const;

    // User-Agent (required), API version, and the token when there is one. No-op for any other host, so a download
    // from a release CDN never receives the token.
    void prepare(QNetworkRequest &request) const;

    // Non-empty when this URL should not be requested: the hourly limit is still in effect, or the token was just
    // rejected. Further calls would get the same answer and spend more of the limit.
    QString deferReason(const QUrl &url) const;

    // A finished api.github.com reply. Rate limits and rejected tokens are remembered and reported once. The returned
    // string is the message to log; empty means the transport's own error string is enough. errorBody is only read
    // when the reply failed.
    QString observe(QNetworkReply *reply, const QByteArray &errorBody);

    // A cached JSON list. A missing file is an empty document. A file that is not a list, such as a saved rate-limit
    // error, is deleted so the next fetch is unconditional.
    static QJsonDocument readCachedList(const QString &path);

signals:
    void tokenChanged();
    void rateLimitedChanged(bool limited);
    void tokenRejectedChanged(bool rejected);
    // The limit window has passed. Release lists can be fetched again.
    void rateLimitLifted();

private:
    explicit GitHub(QObject *parent = nullptr);

    void noteLimit(QNetworkReply *reply, bool announce);
    void noteRejection();
    void clearFailures();
    void armTimer();

    QString m_token;
    bool m_rateLimited = false;
    bool m_announcedLimit = false;
    bool m_tokenRejected = false;
    qint64 m_resetAt = 0;
    int m_limitGeneration = 0;
};
