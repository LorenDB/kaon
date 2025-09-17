#include "Aptabase.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator64>
#include <QSettings>

Aptabase::Aptabase()
    : QObject{nullptr}
{
    m_sessionId = QString::number(QDateTime::currentDateTime().currentSecsSinceEpoch()) +
                  QString::number(QRandomGenerator64::global()->generate64());

    QSettings settings;
    settings.beginGroup("Aptabase"_L1);
    m_enabled = settings.value("enabled"_L1, true).toBool();
}

void Aptabase::init(const QString &host, const QString &key)
{
    instance()->m_host = host;
    instance()->m_key = key;
}

Aptabase *Aptabase::instance()
{
    static auto a = new Aptabase;
    return a;
}

Aptabase *Aptabase::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    return instance();
}

void Aptabase::setEnabled(bool state)
{
    m_enabled = state;
    emit enabledChanged(state);

    QSettings settings;
    settings.beginGroup("Aptabase"_L1);
    settings.setValue("enabled"_L1, m_enabled);
}

void Aptabase::track(const QString &event, const QJsonObject &properties, bool blocking) const
{
    if (!m_enabled)
        return;

    static QNetworkAccessManager net;
    net.setAutoDeleteReplies(true);

    QNetworkRequest req{"https://%1/api/v0/events"_L1.arg(m_host)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"_L1);
    req.setRawHeader("App-Key"_ba, m_key.toLatin1());

    QJsonObject body;
    body["timestamp"_L1] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    body["sessionId"_L1] = m_sessionId;
    body["eventName"_L1] = event;

    QJsonObject systemProperties;
    systemProperties["locale"_L1] = QLocale::system().name(QLocale::TagSeparator::Dash);
#if defined(Q_OS_LINUX)
    systemProperties["osName"_L1] = "Linux"_L1;
#elif defined(Q_OS_WIN)
    systemProperties["osName"_L1] = "Windows"_L1;
#elif defined(Q_OS_DARWIN)
    systemProperties["osName"_L1] = "macOS"_L1;
#endif
#if defined(_DEBUG) || !defined(NDEBUG)
    systemProperties["isDebug"_L1] = true;
#else
    systemProperties["isDebug"_L1] = false;
#endif
    systemProperties["appVersion"_L1] = qApp->applicationVersion();
    systemProperties["sdkVersion"_L1] = "aptabase-kaon@%1"_L1.arg(qApp->applicationVersion());

    body["systemProps"_L1] = systemProperties;
    body["props"_L1] = properties;

    auto rep = net.post(req, QJsonDocument{QJsonArray{body}}.toJson(QJsonDocument::Compact));

    if (blocking)
    {
        auto loop = new QEventLoop;
        connect(rep, &QNetworkReply::finished, loop, &QEventLoop::quit);
        loop->exec();
    }
}
