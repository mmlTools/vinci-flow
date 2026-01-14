#define LOG_TAG "[smart-lower-thirds][api]"

#include "headers/api.hpp"

#include <obs-module.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QImage>
#include <QImageReader>
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

#ifdef _WIN32
static std::wstring to_wide(const QString &s)
{
    return s.toStdWString();
}

struct HttpResult {
    int status = 0;
    QByteArray body;
    QString error;
};

static HttpResult winhttp_get(const QUrl &url, const wchar_t *extraHeaders)
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
        out.error = QStringLiteral("WinHttpOpen failed.");
        return out;
    }

    const INTERNET_PORT port = (INTERNET_PORT)(url.port(isHttps ? 443 : 80));
    HINTERNET hConnect = WinHttpConnect(hSession, to_wide(host).c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        out.error = QStringLiteral("WinHttpConnect failed.");
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
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        out.error = QStringLiteral("WinHttpOpenRequest failed.");
        return out;
    }

    WinHttpSetTimeouts(hRequest, 5000, 5000, 8000, 8000);

    BOOL ok = WinHttpSendRequest(
        hRequest,
        extraHeaders,
        (DWORD)-1L,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0);

    if (!ok) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        out.error = QStringLiteral("WinHttpSendRequest failed.");
        return out;
    }

    ok = WinHttpReceiveResponse(hRequest, nullptr);
    if (!ok) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        out.error = QStringLiteral("WinHttpReceiveResponse failed.");
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
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0)
            break;

        const int chunk = (int)avail;
        const int oldSize = buf.size();
        buf.resize(oldSize + chunk);

        DWORD read = 0;
        if (!WinHttpReadData(hRequest, buf.data() + oldSize, avail, &read) || read == 0) {
            buf.resize(oldSize);
            break;
        }

        if (read != avail)
            buf.resize(oldSize + (int)read);
    }

    out.body = buf;

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (out.status == 0 && !out.body.isEmpty())
        out.status = 200;

    return out;
}
#endif

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

    loadCacheFromDisk();

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!isCacheFresh(now)) {
        QTimer::singleShot(250, this, [this]() { fetchLowerThirds(false); });
    }
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
                if (targetPx > 0) {
                    img = img.scaled(targetPx, targetPx, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                }
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

#ifdef _WIN32
        const HttpResult r = winhttp_get(url, L"Accept: image/avif,image/webp,image/*,*/*\r\n");
        status = r.status;
        body   = r.body;
        err    = r.error;
#else
        err = QStringLiteral("No HTTPS backend available in this build.");
#endif

        QMetaObject::invokeMethod(this, [this, u, targetPx, diskPath, status, body, err]() {
            m_imgInFlight.remove(u);

            if (!err.isEmpty()) {
                emit imageFailed(u, err);
                return;
            }
            if (status < 200 || status >= 300 || body.isEmpty()) {
                emit imageFailed(u, tr("Image request failed (HTTP %1).\n").arg(status));
                return;
            }

            QImage img;
            img.loadFromData(body);
            if (img.isNull()) {
                emit imageFailed(u, tr("Unsupported image format."));
                return;
            }

            if (targetPx > 0) {
                img = img.scaled(targetPx, targetPx, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            }

            QPixmap pm = QPixmap::fromImage(img);
            if (!pm.isNull()) {
                m_pixCache.insert(u, pm);

                if (!diskPath.isEmpty()) {
                    QDir().mkpath(QFileInfo(diskPath).absolutePath());
                    QFile f(diskPath);
                    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        f.write(body);
                    }
                }

                emit imageReady(u, pm);
            } else {
                emit imageFailed(u, tr("Could not decode image."));
            }
        }, Qt::QueuedConnection);
    }).detach();
}

QVector<ResourceItem> ApiClient::lowerThirds() const
{
    return m_lowerThirds;
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
	    return;
    }

    m_fetchInFlight = true;

    const QUrl url = buildLowerThirdsUrl();

    std::thread([this, url]() {
        int status = 0;
        QByteArray body;
        QString err;

#ifdef _WIN32
        const HttpResult r = winhttp_get(url, L"Accept: application/json\r\n");
        status = r.status;
        body   = r.body;
        err    = r.error;
#else
        err = QStringLiteral("No HTTPS backend available in this build.");
#endif

        QMetaObject::invokeMethod(this, [this, status, body, err]() {
            m_fetchInFlight = false;

            if (!err.isEmpty()) {
                m_lastError = err;
                emit lowerThirdsFailed(m_lastError);
                return;
            }

            if (status < 200 || status >= 300) {
                m_lastError = tr("API request failed (HTTP %1).").arg(status);
                emit lowerThirdsFailed(m_lastError);
                return;
            }

            // Validate and parse JSON
            QJsonParseError jerr{};
            const QJsonDocument doc = QJsonDocument::fromJson(body, &jerr);
            if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
                m_lastError = tr("API returned invalid JSON.");
                emit lowerThirdsFailed(m_lastError);
                return;
            }

            const QJsonObject root = doc.object();
            if (!root.value(QStringLiteral("ok")).toBool(false)) {
                m_lastError = tr("API returned ok=false.");
                emit lowerThirdsFailed(m_lastError);
                return;
            }

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
	const QString p = obs_config_path("api-cache.json");
	return p;
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
    const qint64 fetchedAt = (qint64)root.value(QStringLiteral("fetched_at"))
                                 .toVariant()
                                 .toLongLong();

    const QByteArray payload = root.value(QStringLiteral("payload")).toString().toUtf8();
    if (payload.isEmpty())
        return;

    m_cacheFetchedAt = fetchedAt;
    m_lastRaw = payload;
    parseAndSet(payload);

    if (!m_lowerThirds.isEmpty()) {
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
    root.insert(QStringLiteral("payload"), QString::fromUtf8(rawJson));

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
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

	    if (it.title.trimmed().isEmpty())
		    continue;

	    if (it.url.trimmed().isEmpty() && !it.slug.trimmed().isEmpty()) {
		    it.url = QStringLiteral("https://obscountdown.com/r/") + it.slug;
	    }

	    out.push_back(it);
    }

    m_lowerThirds = out;
}

} // namespace smart_lt::api
