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
};

class ApiClient final : public QObject {
    Q_OBJECT
public:
	static ApiClient &instance();
	void init();
	void fetchLowerThirds(bool force = false);
	void requestImage(const QString &imageUrl, int targetPx = 48);

	QVector<ResourceItem> lowerThirds() const;
	QString lastError() const;

signals:
    void lowerThirdsUpdated();
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

private:
    QVector<ResourceItem> m_lowerThirds;
    QString m_lastError;
    QByteArray m_lastRaw;
    qint64 m_cacheFetchedAt = 0;
    bool m_inited = false;
    bool m_fetchInFlight = false;
    QHash<QString, QPixmap> m_pixCache;
    QSet<QString> m_imgInFlight;
};

} // namespace smart_lt::api
