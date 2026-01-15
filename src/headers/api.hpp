// api.hpp
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QPixmap>

namespace smart_lt::api {

struct ResourceItem {
    QString guid;
    QString slug;
    QString url;
    QString title;
    QString shortDescription;
    QString typeLabel;
    QString downloadUrl;
    QString iconPublicUrl;
    QString coverPublicUrl;
    QString badgeValue;
};

class ApiClient final : public QObject {
    Q_OBJECT
public:
    static ApiClient &instance();

    // Call once after plugin startup (safe to call multiple times).
    void init();

    // Fetch Marketplace lower thirds list.
    // - force=false respects TTL and skips network when cache is fresh.
    // - force=true always attempts a network refresh.
    void fetchLowerThirds(bool force = false);

    // Fetch and decode an image with in-memory + disk caching.
    void requestImage(const QString &imageUrl, int targetPx = 48);

    QVector<ResourceItem> lowerThirds() const;
    // Optional value returned by the API root: "plugin_version"
    // Empty when not provided / not fetched.
    QString remotePluginVersion() const;
    QString lastError() const;

signals:
    // Emitted when m_lowerThirds has been populated/updated (from disk cache or network).
    void lowerThirdsUpdated();

    // Emitted on network/parse failures.
    // Important: callers should NOT clear existing UI items on this signal.
    void lowerThirdsFailed(const QString &err);

    void imageReady(const QString &imageUrl, const QPixmap &pixmap);
    void imageFailed(const QString &imageUrl, const QString &err);

private:
    explicit ApiClient(QObject *parent = nullptr);

    void loadCacheFromDisk();
    void saveCacheToDisk(const QByteArray &rawJson, qint64 fetchedAtEpochSec);
    bool isCacheFresh(qint64 nowEpochSec) const;
    QString cacheFilePath() const;

    void parseAndSet(const QByteArray &rawJson);
    QUrl buildLowerThirdsUrl() const;

    QString imageCacheDir() const;
    QString imageCachePathForUrl(const QString &imageUrl) const;

    // Retry/backoff for failed API calls.
    void scheduleRetry();
    void resetRetry();

private:
    QVector<ResourceItem> m_lowerThirds;
    QString m_lastError;
    QByteArray m_lastRaw;
    qint64 m_cacheFetchedAt = 0;

    QString m_remotePluginVersion;

    bool m_inited = false;
    bool m_fetchInFlight = false;

    // Retry state
    int m_retryCount = 0;
    bool m_retryScheduled = false;

    // Image caches
    QHash<QString, QPixmap> m_pixCache;
    QSet<QString> m_imgInFlight;
};

} // namespace smart_lt::api
