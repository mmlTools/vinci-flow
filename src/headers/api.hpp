#pragma once

#include <QObject>
#include <QPixmap>
#include <QVector>
#include <QString>
#include <QHash>
#include <QSet>

namespace vflow::api {

struct ResourceItem {
	QString guid;
	QString type;
	QString slug;
	QString url;
	QString title;
	QString shortDescription;
	QString metaTitle;
	QString metaDescription;

	QString iconPath;
	QString iconPublicUrl;
	QString coverPath;
	QString coverPublicUrl;

	QString buttonUrl;
	QString buttonLabel;
	QString priceUsd;

	int memberOnly = 0;
	QString requiredRole;

	QString publisherGuid;
	QString publisherNickname;
	QString publisherPublicToken;
	QString publisherAvatarUrl;

	QString publishedAt;
	QString createdAt;
	QString updatedAt;

	int views = 0;
	int likes = 0;
	int saves = 0;
	int shares = 0;
	int downloads = 0;
};

class ApiClient final : public QObject {
	Q_OBJECT
public:
	static ApiClient &instance();

	explicit ApiClient(QObject *parent = nullptr);

	void init();
	void fetchLowerThirds(bool force = false);
	void requestImage(const QString &imageUrl, int targetPx = 0);

	QVector<ResourceItem> lowerThirds() const;
	QString remotePluginVersion() const;
	QString lastError() const;

signals:
	void lowerThirdsUpdated();
	void lowerThirdsFailed(const QString &err);
	void imageReady(const QString &url, const QPixmap &pm);
	void imageFailed(const QString &url, const QString &err);

private:
	bool isCacheFresh(qint64 nowEpochSec) const;
	QString cacheFilePath() const;
	QString imageCacheDir() const;
	QString imageCachePathForUrl(const QString &imageUrl) const;
	void loadCacheFromDisk();
	void saveCacheToDisk(const QByteArray &rawJson, qint64 fetchedAtEpochSec);
	QUrl buildLowerThirdsUrl() const;
	void parseAndSet(const QByteArray &rawJson);
	void scheduleRetry();
	void resetRetry();

private:
	bool m_inited = false;
	bool m_fetchInFlight = false;
	bool m_retryScheduled = false;
	int m_retryCount = 0;
	qint64 m_cacheFetchedAt = 0;

	QString m_remotePluginVersion;
	QString m_lastError;
	QByteArray m_lastRaw;

	QVector<ResourceItem> m_lowerThirds;
	QHash<QString, QPixmap> m_pixCache;
	QSet<QString> m_imgInFlight;
};

} // namespace vflow::api