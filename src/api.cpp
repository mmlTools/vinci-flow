#define LOG_TAG "[smart-lower-thirds][api]"

#include "headers/api.hpp"

#include <obs-module.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QImage>
#include <QPixmap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QMetaObject>

#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace {

static inline void log_line(int level, const QString &msg)
{
    blog(level, "%s %s", LOG_TAG, msg.toUtf8().constData());
}

static inline QString sanitize_preview(QByteArray bytes, int maxLen)
{
    bytes = bytes.left(maxLen);
    for (int i = 0; i < bytes.size(); ++i) {
        const char c = bytes.at(i);
        if (c == '\r' || c == '\n' || c == '\t')
            bytes[i] = ' ';
        else if ((unsigned char)c < 0x20)
            bytes[i] = ' ';
    }
    return QString::fromUtf8(bytes);
}

#ifdef _WIN32
static std::wstring to_wide(const QString &s)
{
    return s.toStdWString();
}

struct HttpResult {
    int status = 0;
    QByteArray body;
    QString error;
    DWORD winErr = 0;
};

// NOTE: Keep this small for safety. If the API returns HTML or a WAF page, it can be huge.
static constexpr DWORD kMaxApiBytes   = 2u * 1024u * 1024u;   // 2 MiB
static constexpr DWORD kMaxImageBytes = 10u * 1024u * 1024u;  // 10 MiB

static HttpResult winhttp_get(const QUrl &url, const wchar_t *extraHeaders, DWORD maxBytes)
{
    HttpResult out;

    if (!url.isValid()) {
        out.error = QStringLiteral("Invalid URL.");
        return out;
    }

    const bool isHttps = (url.scheme().toLower() == QStringLiteral("https"));
    const QString host = url.host();
    const QString path = url.path(QUrl::FullyEncoded);
    const QString query = url.query(QUrl::FullyEncoded);
    const QString fullPath = query.isEmpty() ? path : (path + QStringLiteral("?") + query);

    if (host.trimmed().isEmpty()) {
        out.error = QStringLiteral("URL missing host.");
        return out;
    }

    HINTERNET hSession = WinHttpOpen(
        L"obs-plugin/smart-lower-thirds",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!hSession) {
        out.winErr = GetLastError();
        out.error = QStringLiteral("WinHttpOpen failed (%1)." ).arg((qulonglong)out.winErr);
        return out;
    }

    const INTERNET_PORT port = (INTERNET_PORT)(url.port(isHttps ? 443 : 80));
    HINTERNET hConnect = WinHttpConnect(hSession, to_wide(host).c_str(), port, 0);
    if (!hConnect) {
        out.winErr = GetLastError();
        WinHttpCloseHandle(hSession);
        out.error = QStringLiteral("WinHttpConnect failed (%1)." ).arg((qulonglong)out.winErr);
        return out;
    }

    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (isHttps)
        flags |= WINHTTP_FLAG_SECURE;

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        to_wide(fullPath).c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);

    if (!hRequest) {
        out.winErr = GetLastError();
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        out.error = QStringLiteral("WinHttpOpenRequest failed (%1)." ).arg((qulonglong)out.winErr);
        return out;
    }

    // Timeouts: resolve/connect/send/receive (ms)
    WinHttpSetTimeouts(hRequest, 5000, 5000, 10000, 10000);

    // Attempt decompression for gzip/deflate (supported on modern Windows).
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));

    BOOL ok = WinHttpSendRequest(
        hRequest,
        extraHeaders,
        (DWORD)-1L,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0);

    if (!ok) {
        out.winErr = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        out.error = QStringLiteral("WinHttpSendRequest failed (%1)." ).arg((qulonglong)out.winErr);
        return out;
    }

    ok = WinHttpReceiveResponse(hRequest, nullptr);
    if (!ok) {
        out.winErr = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        out.error = QStringLiteral("WinHttpReceiveResponse failed (%1)." ).arg((qulonglong)out.winErr);
        return out;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (WinHttpQueryHeaders(hRequest,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &statusCode,
                            &statusSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        out.status = (int)statusCode;
    }

    QByteArray buf;
    buf.reserve(32 * 1024);

    DWORD total = 0;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0)
            break;

        // Enforce a hard cap.
        if (total + avail > maxBytes) {
            out.error = QStringLiteral("Response exceeded size limit (%1 bytes)." ).arg((qulonglong)maxBytes);
            break;
        }

        const int chunk = (int)avail;
        const int oldSize = buf.size();
        buf.resize(oldSize + chunk);

        DWORD read = 0;
        if (!WinHttpReadData(hRequest, buf.data() + oldSize, avail, &read) || read == 0) {
            const DWORD e = GetLastError();
            if (e != 0) {
                out.winErr = e;
            }
            buf.resize(oldSize);
            break;
        }

        total += read;
        if (read != avail)
            buf.resize(oldSize + (int)read);
    }

    out.body = buf;

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (out.status == 0 && !out.body.isEmpty() && out.error.isEmpty())
        out.status = 200;

    return out;
}
#endif

static void log_http_result(const char *kind, const QUrl &url, int status, const QByteArray &body, const QString &err, quint64 winErr = 0)
{
    const QString u = url.toString();
    log_line(LOG_INFO, QStringLiteral("%1 url=%2 status=%3 winErr=%4 bytes=%5 err=%6")
                        .arg(QString::fromUtf8(kind))
                        .arg(u)
                        .arg(status)
                        .arg(winErr)
                        .arg(body.size())
                        .arg(err));

    if (!body.isEmpty()) {
        const QString preview = sanitize_preview(body, 800);
        log_line(LOG_INFO, QStringLiteral("%1 body_preview=%2").arg(QString::fromUtf8(kind), preview));
    }
}

static int retry_delay_ms(int attempt)
{
    // 5s, 15s, 30s, 60s, 5m
    static const int table[] = { 5000, 15000, 30000, 60000, 300000 };
    if (attempt < 0)
        attempt = 0;
    if (attempt > 4)
        attempt = 4;
    return table[attempt];
}

} // namespace

namespace smart_lt::api {

namespace {

static constexpr const char *kBaseEndpoint = "https://obscountdown.com/api/resources";
static constexpr int kDefaultLimit = 8;
static constexpr qint64 kCacheTtlSec = 3600;

static QString obs_config_path(const char *file)
{
    char *p = obs_module_config_path(file);
    if (!p)
        return {};
    QString out = QString::fromUtf8(p);
    bfree(p);
    return out;
}

} // namespace

ApiClient &ApiClient::instance()
{
    static ApiClient inst;
    return inst;
}

ApiClient::ApiClient(QObject *parent) : QObject(parent) {}

void ApiClient::init()
{
    if (m_inited)
        return;
    m_inited = true;

    qRegisterMetaType<QPixmap>("QPixmap");
	// Load cache (if available) and immediately inform UI.
	loadCacheFromDisk();

	// Always refresh on startup so version checks and marketplace data are up-to-date.
	// The on-disk cache is only used as an offline fallback.
	QTimer::singleShot(250, this, [this]() { fetchLowerThirds(true); });
}

void ApiClient::requestImage(const QString &imageUrl, int targetPx)
{
    const QString u = imageUrl.trimmed();
    if (u.isEmpty())
        return;

    if (m_pixCache.contains(u)) {
        emit imageReady(u, m_pixCache.value(u));
        return;
    }

    if (m_imgInFlight.contains(u))
        return;
    m_imgInFlight.insert(u);

    const QString diskPath = imageCachePathForUrl(u);
    if (!diskPath.isEmpty()) {
        QFile f(diskPath);
        if (f.exists() && f.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = f.readAll();
            QImage img;
            img.loadFromData(bytes);
            if (!img.isNull()) {
                if (targetPx > 0)
                    img = img.scaled(targetPx, targetPx, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

                QPixmap pm = QPixmap::fromImage(img);
                m_pixCache.insert(u, pm);
                m_imgInFlight.remove(u);
                emit imageReady(u, pm);
                return;
            }
        }
    }

    const QUrl url(u);

    std::thread([this, url, u, targetPx, diskPath]() {
        int status = 0;
        QByteArray body;
        QString err;
        quint64 winErr = 0;

#ifdef _WIN32
        const HttpResult r = winhttp_get(url, L"Accept: image/avif,image/webp,image/*,*/*\r\n", kMaxImageBytes);
        status = r.status;
        body   = r.body;
        err    = r.error;
        winErr = (quint64)r.winErr;
#else
        err = QStringLiteral("No HTTPS backend available in this build.");
#endif

        QMetaObject::invokeMethod(this, [this, u, targetPx, diskPath, status, body, err, url, winErr]() {
            m_imgInFlight.remove(u);

            if (!err.isEmpty()) {
                log_http_result("[img]", url, status, body, err, winErr);
                emit imageFailed(u, err);
                return;
            }

            if (status < 200 || status >= 300 || body.isEmpty()) {
                log_http_result("[img]", url, status, body, tr("HTTP %1").arg(status), winErr);
                emit imageFailed(u, tr("Image request failed (HTTP %1).").arg(status));
                return;
            }

            QImage img;
            img.loadFromData(body);
            if (img.isNull()) {
                emit imageFailed(u, tr("Unsupported image format."));
                return;
            }

            if (targetPx > 0)
                img = img.scaled(targetPx, targetPx, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

            const QPixmap pm = QPixmap::fromImage(img);
            if (pm.isNull()) {
                emit imageFailed(u, tr("Could not decode image."));
                return;
            }

            m_pixCache.insert(u, pm);

            if (!diskPath.isEmpty()) {
                QDir().mkpath(QFileInfo(diskPath).absolutePath());
                QFile f(diskPath);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    f.write(body);
                }
            }

            emit imageReady(u, pm);
        }, Qt::QueuedConnection);
    }).detach();
}

QVector<ResourceItem> ApiClient::lowerThirds() const
{
    return m_lowerThirds;
}

QString ApiClient::remotePluginVersion() const
{
    return m_remotePluginVersion;
}

QString ApiClient::lastError() const
{
    return m_lastError;
}

void ApiClient::fetchLowerThirds(bool force)
{
    if (m_fetchInFlight)
        return;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!force && isCacheFresh(now) && !m_lowerThirds.isEmpty()) {
        // Cache is fresh and we already have items.
        return;
    }

    m_fetchInFlight = true;

    const QUrl url = buildLowerThirdsUrl();

    std::thread([this, url]() {
        int status = 0;
        QByteArray body;
        QString err;
        quint64 winErr = 0;

#ifdef _WIN32
        const HttpResult r = winhttp_get(url, L"Accept: application/json\r\n", kMaxApiBytes);
        status = r.status;
        body   = r.body;
        err    = r.error;
        winErr = (quint64)r.winErr;
#else
        err = QStringLiteral("No HTTPS backend available in this build.");
#endif

        QMetaObject::invokeMethod(this, [this, status, body, err, url, winErr]() {
            m_fetchInFlight = false;

            // Always log failures and unexpected responses.
            if (!err.isEmpty() || status < 200 || status >= 300) {
                log_http_result("[api]", url, status, body, err.isEmpty() ? tr("HTTP %1").arg(status) : err, winErr);
            }

            if (!err.isEmpty()) {
                // Fallback: keep whatever we already have (cache may be stale).
                m_lastError = err;
                emit lowerThirdsFailed(m_lastError);
                scheduleRetry();
                return;
            }

            if (status < 200 || status >= 300) {
                m_lastError = tr("API request failed (HTTP %1)." ).arg(status);
                emit lowerThirdsFailed(m_lastError);
                scheduleRetry();
                return;
            }

            // Validate and parse JSON
            QJsonParseError jerr{};
            const QJsonDocument doc = QJsonDocument::fromJson(body, &jerr);
            if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
                log_line(LOG_WARNING, QStringLiteral("json_parse_error=%1 offset=%2 msg=%3")
                                       .arg((int)jerr.error)
                                       .arg((qlonglong)jerr.offset)
                                       .arg(jerr.errorString()));

                m_lastError = tr("API returned invalid JSON.");
                emit lowerThirdsFailed(m_lastError);
                scheduleRetry();
                return;
            }

            const QJsonObject root = doc.object();
            if (!root.value(QStringLiteral("ok")).toBool(false)) {
                m_lastError = tr("API returned ok=false.");
                emit lowerThirdsFailed(m_lastError);
                scheduleRetry();
                return;
            }

            // Success
            resetRetry();
            m_lastError.clear();
            m_lastRaw = body;
            m_cacheFetchedAt = QDateTime::currentSecsSinceEpoch();

            parseAndSet(body);
            saveCacheToDisk(body, m_cacheFetchedAt);

            emit lowerThirdsUpdated();
        }, Qt::QueuedConnection);
    }).detach();
}

bool ApiClient::isCacheFresh(qint64 nowEpochSec) const
{
    if (m_cacheFetchedAt <= 0)
        return false;
    return (nowEpochSec - m_cacheFetchedAt) < kCacheTtlSec;
}

QString ApiClient::cacheFilePath() const
{
    return obs_config_path("api-cache.json");
}

QString ApiClient::imageCacheDir() const
{
    const QString cacheFile = cacheFilePath();
    if (cacheFile.isEmpty())
        return {};

    const QFileInfo fi(cacheFile);
    return fi.dir().filePath(QStringLiteral("api-images"));
}

QString ApiClient::imageCachePathForUrl(const QString &imageUrl) const
{
    const QString dir = imageCacheDir();
    if (dir.isEmpty() || imageUrl.trimmed().isEmpty())
        return {};

    const QByteArray h = QCryptographicHash::hash(imageUrl.toUtf8(), QCryptographicHash::Md5).toHex();
    return QDir(dir).filePath(QString::fromUtf8(h) + QStringLiteral(".bin"));
}

void ApiClient::loadCacheFromDisk()
{
    const QString path = cacheFilePath();
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return;

    const QByteArray raw = f.readAll();
    QJsonParseError jerr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &jerr);
    if (jerr.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject root = doc.object();
    const qint64 fetchedAt = (qint64)root.value(QStringLiteral("fetched_at")).toVariant().toLongLong();

    // Backward compatible payload handling:
    // - New: payload_b64 (lossless)
    // - Old: payload (UTF-8 string)
    QByteArray payload;
    if (root.contains(QStringLiteral("payload_b64"))) {
        payload = QByteArray::fromBase64(root.value(QStringLiteral("payload_b64")).toString().toLatin1());
    } else {
        payload = root.value(QStringLiteral("payload")).toString().toUtf8();
    }

    if (payload.isEmpty())
        return;

    m_cacheFetchedAt = fetchedAt;
    m_lastRaw = payload;
    parseAndSet(payload);

    // Immediately inform UI (even if stale). We emit when either:
    // - we have cached items to display, OR
    // - we have a cached plugin_version for update checks.
    if (!m_lowerThirds.isEmpty() || !m_remotePluginVersion.trimmed().isEmpty()) {
        emit lowerThirdsUpdated();
    }
}

void ApiClient::saveCacheToDisk(const QByteArray &rawJson, qint64 fetchedAtEpochSec)
{
    const QString path = cacheFilePath();
    if (path.isEmpty())
        return;

    QJsonObject root;
    root.insert(QStringLiteral("fetched_at"), (qint64)fetchedAtEpochSec);
    root.insert(QStringLiteral("payload_b64"), QString::fromLatin1(rawJson.toBase64()));

    // Atomic-ish write: write to temp then replace.
    const QString tmp = path + QStringLiteral(".tmp");

    {
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.flush();
    }

    QFile::remove(path);
    QFile::rename(tmp, path);
}

QUrl ApiClient::buildLowerThirdsUrl() const
{
    QUrl url(QString::fromUtf8(kBaseEndpoint));
    QUrlQuery q(url);

    q.addQueryItem(QStringLiteral("type"), QStringLiteral("lower-thirds-templates"));
    q.addQueryItem(QStringLiteral("limit"), QString::number(kDefaultLimit));

    url.setQuery(q);
    return url;
}

void ApiClient::parseAndSet(const QByteArray &rawJson)
{
    QJsonParseError jerr{};
    const QJsonDocument doc = QJsonDocument::fromJson(rawJson, &jerr);
    if (jerr.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject root = doc.object();
    // Optional: plugin version for update checks (may be missing on older cached payloads)
    m_remotePluginVersion = root.value(QStringLiteral("plugin_version")).toString().trimmed();
    const QJsonArray arr = root.value(QStringLiteral("data")).toArray();

    QVector<ResourceItem> out;
    out.reserve(arr.size());

    for (QJsonValue v : arr) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();

        ResourceItem it;
        it.guid = o.value(QStringLiteral("guid")).toString();
        it.slug = o.value(QStringLiteral("slug")).toString();
        it.url = o.value(QStringLiteral("url")).toString();
        it.title = o.value(QStringLiteral("title")).toString();
        it.shortDescription = o.value(QStringLiteral("short_description")).toString();
        it.typeLabel = o.value(QStringLiteral("type_label")).toString();

        // Optional extras (your API includes these)
        it.downloadUrl = o.value(QStringLiteral("download_url")).toString();
        it.iconPublicUrl = o.value(QStringLiteral("icon_public_url")).toString();
        it.coverPublicUrl = o.value(QStringLiteral("cover_public_url")).toString();
        it.badgeValue = o.value(QStringLiteral("badge_value")).toString();

        if (it.title.trimmed().isEmpty())
            continue;

        if (it.url.trimmed().isEmpty() && !it.slug.trimmed().isEmpty())
            it.url = QStringLiteral("https://obscountdown.com/r/") + it.slug;

        out.push_back(it);
    }

    m_lowerThirds = out;
}

void ApiClient::scheduleRetry()
{
    if (m_retryScheduled)
        return;

    // If we have cached data, do not spam retries; still retry, but with backoff.
    const int delay = retry_delay_ms(m_retryCount);
    m_retryCount++;
    m_retryScheduled = true;

    log_line(LOG_INFO, QStringLiteral("Scheduling retry in %1 ms (attempt %2)").arg(delay).arg(m_retryCount));

    QTimer::singleShot(delay, this, [this]() {
        m_retryScheduled = false;
        // Force refresh (even if cache is fresh now). If network is still blocked, we back off further.
        fetchLowerThirds(true);
    });
}

void ApiClient::resetRetry()
{
    m_retryCount = 0;
    m_retryScheduled = false;
}

} // namespace smart_lt::api
