#include "GitHub.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QSettings>
#include <QTimer>

Q_LOGGING_CATEGORY(GitHubLog, "github")

namespace
{
    constexpr auto ApiHost = "api.github.com";

    bool isApi(const QUrl &url)
    {
        return url.host() == ApiHost;
    }

    QString normalizedToken(QString token)
    {
        token = token.trimmed();
        if (token.startsWith("Bearer "_L1, Qt::CaseInsensitive))
            token = token.mid(7).trimmed();
        if (token.size() >= 2 && token.front() == '"'_L1 && token.back() == '"'_L1)
            token = token.mid(1, token.size() - 2).trimmed();
        return token;
    }

    qint64 resetEpoch(QNetworkReply *reply)
    {
        const auto now = QDateTime::currentSecsSinceEpoch();
        bool ok = false;
        if (const auto reset = reply->rawHeader("X-RateLimit-Reset"_ba).toLongLong(&ok); ok && reset > now)
            return reset;

        const auto retry = reply->rawHeader("Retry-After"_ba);
        if (const auto secs = retry.toLongLong(&ok); ok && secs > 0)
            return now + secs;
        if (const auto when = QDateTime::fromString(QString::fromLatin1(retry), Qt::RFC2822Date);
            when.isValid() && when.toSecsSinceEpoch() > now)
            return when.toSecsSinceEpoch();
        // No clock in the answer. A minute is long enough that a burst of retries won't extend an abuse block.
        return now + 60;
    }
} // namespace

GitHub::GitHub(QObject *parent)
    : QObject{parent}
{
    QSettings settings;
    settings.beginGroup("github"_L1);
    m_token = normalizedToken(settings.value("token"_L1).toString());
    if (!m_token.isEmpty() && !tokenUsable())
        qCWarning(GitHubLog) << "The GitHub token in Settings can't be sent, so Kaon is asking as a guest.";
}

GitHub *GitHub::instance()
{
    static auto github = new GitHub;
    return github;
}

GitHub *GitHub::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

bool GitHub::tokenUsable() const
{
    if (m_token.isEmpty())
        return false;
    for (const auto ch : m_token)
    {
        const auto code = ch.unicode();
        if (code <= 0x20 || code >= 0x7f)
            return false;
    }
    return true;
}

void GitHub::setToken(const QString &token)
{
    const auto trimmed = normalizedToken(token);
    if (trimmed == m_token)
        return;

    m_token = trimmed;
    QSettings settings;
    settings.beginGroup("github"_L1);
    if (m_token.isEmpty())
        settings.remove("token"_L1);
    else
        settings.setValue("token"_L1, m_token);
    if (!m_token.isEmpty() && !tokenUsable())
        qCWarning(GitHubLog) << "The GitHub token in Settings can't be sent, so Kaon is asking as a guest.";

    // The new token has its own budget. A limit or a rejection of the previous one must not block the next ask.
    clearFailures();
    emit tokenChanged();
}

QString GitHub::rateLimitDetail() const
{
    if (!m_rateLimited)
        return {};
    QString when;
    if (m_resetAt > 0)
        when = QLocale::system().toString(QDateTime::fromSecsSinceEpoch(m_resetAt), QLocale::ShortFormat);
    if (when.isEmpty())
        return "Kaon has used GitHub's hourly limit, so it will keep the version lists it already has and ask again "
               "later. A personal access token in Settings raises that limit."_L1;
    return "Kaon has used GitHub's hourly limit, so it will keep the version lists it already has and ask again "
           "around %1. A personal access token in Settings raises that limit."_L1.arg(when);
}

void GitHub::prepare(QNetworkRequest &request) const
{
    if (!isApi(request.url()))
        return;

    if (request.rawHeader("User-Agent"_ba).isEmpty())
    {
        const auto agent = "Kaon/"_L1 + QCoreApplication::applicationVersion() + " (+https://github.com/LorenDB/kaon)"_L1;
        request.setRawHeader("User-Agent"_ba, agent.toUtf8());
    }
    if (request.rawHeader("Accept"_ba).isEmpty())
        request.setRawHeader("Accept"_ba, "application/vnd.github+json"_ba);
    if (request.rawHeader("X-GitHub-Api-Version"_ba).isEmpty())
        request.setRawHeader("X-GitHub-Api-Version"_ba, "2022-11-28"_ba);
    if (tokenUsable() && request.rawHeader("Authorization"_ba).isEmpty())
        request.setRawHeader("Authorization"_ba, "Bearer "_ba + m_token.toUtf8());
}

QString GitHub::deferReason(const QUrl &url) const
{
    if (!isApi(url))
        return {};
    if (m_tokenRejected)
        return "GitHub rejected the token from Settings."_L1;
    if (m_rateLimited && (m_resetAt <= 0 || QDateTime::currentSecsSinceEpoch() < m_resetAt))
        return "GitHub's request limit was reached"_L1;
    return {};
}

QString GitHub::observe(QNetworkReply *reply, const QByteArray &errorBody)
{
    if (!reply || !isApi(reply->url()))
        return {};

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 200 || status == 304)
    {
        if (m_tokenRejected)
        {
            m_tokenRejected = false;
            emit tokenRejectedChanged(false);
        }
        // This answer was the last one the window allows. Don't spend the next calls on a 403.
        if (reply->rawHeader("X-RateLimit-Remaining"_ba) == "0")
            noteLimit(reply, true);
        else
            clearFailures();
        return {};
    }

    const auto doc = QJsonDocument::fromJson(errorBody);
    const auto apiMessage = doc.object().value("message"_L1).toString();
    const auto lower = apiMessage.toLower();
    const bool remainingZero = reply->rawHeader("X-RateLimit-Remaining"_ba) == "0";
    const bool hasRetry = !reply->rawHeader("Retry-After"_ba).isEmpty();
    if (status == 429 || remainingZero || hasRetry || lower.contains("rate limit"_L1))
    {
        noteLimit(reply, true);
        return rateLimitDetail();
    }

    if (status == 401 || lower.contains("bad credentials"_L1))
    {
        noteRejection();
        return "GitHub rejected the token from Settings."_L1;
    }

    return apiMessage;
}

QJsonDocument GitHub::readCachedList(const QString &path)
{
    QFile cached{path};
    if (!cached.open(QFile::ReadOnly))
        return {};

    const auto doc = QJsonDocument::fromJson(cached.readAll());
    if (doc.isArray())
        return doc;

    // A rate-limit body is JSON too. Left on disk, it would be read as "there are no releases".
    qCWarning(GitHubLog).noquote() << "Discarding" << path << "because it is not a release list.";
    cached.close();
    QFile::remove(path);
    QFile::remove(path + ".etag"_L1);
    return {};
}

void GitHub::noteLimit(QNetworkReply *reply, bool announce)
{
    m_resetAt = resetEpoch(reply);
    const bool became = !m_rateLimited;
    m_rateLimited = true;
    armTimer();
    if (became)
        emit rateLimitedChanged(true);
    if (!announce || m_announcedLimit)
        return;

    m_announcedLimit = true;
    qCWarning(GitHubLog).noquote() << "GitHub's request limit was reached. Cached release lists will be used until"
                                   << QDateTime::fromSecsSinceEpoch(m_resetAt).toString(Qt::ISODate);
}

void GitHub::noteRejection()
{
    if (m_tokenRejected)
        return;
    m_tokenRejected = true;
    qCWarning(GitHubLog) << "GitHub rejected the token from Settings.";
    emit tokenRejectedChanged(true);
}

void GitHub::clearFailures()
{
    ++m_limitGeneration;
    m_announcedLimit = false;
    if (m_rateLimited)
    {
        m_rateLimited = false;
        m_resetAt = 0;
        emit rateLimitedChanged(false);
    }
    if (m_tokenRejected)
    {
        m_tokenRejected = false;
        emit tokenRejectedChanged(false);
    }
}

void GitHub::armTimer()
{
    const auto generation = ++m_limitGeneration;
    const auto seconds = m_resetAt - QDateTime::currentSecsSinceEpoch();
    // A nonsense reset time must not schedule a timer days away. An hour is GitHub's real window.
    const int delay = seconds <= 0 ? 1000 : seconds >= 61 * 60 ? 61 * 60 * 1000 : static_cast<int>(seconds * 1000 + 1500);

    QTimer::singleShot(delay, this, [this, generation] {
        if (generation != m_limitGeneration || !m_rateLimited)
            return;
        if (QDateTime::currentSecsSinceEpoch() < m_resetAt)
        {
            armTimer();
            return;
        }
        m_rateLimited = false;
        m_resetAt = 0;
        m_announcedLimit = false;
        emit rateLimitedChanged(false);
        emit rateLimitLifted();
    });
}
