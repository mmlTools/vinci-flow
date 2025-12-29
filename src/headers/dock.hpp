// dock.hpp
#pragma once

#include <QWidget>
#include <QVector>
#include <QHash>
#include <QSet>

class QScrollArea;
class QVBoxLayout;
class QLineEdit;
class QPushButton;
class QFrame;
class QLabel;
class QCheckBox;
class QShortcut;
class QEvent;
class QTimer;

namespace smart_lt::ui {

struct LowerThirdRowUi {
	QString id;
	QFrame *row = nullptr;
	QLabel *labelLbl = nullptr;
	QLabel *subLbl = nullptr;
	QLabel *thumbnailLbl = nullptr;
	QCheckBox *visibleCheck = nullptr;
	QPushButton *cloneBtn = nullptr;
	QPushButton *settingsBtn = nullptr;
	QPushButton *removeBtn = nullptr;
};

class LowerThirdDock : public QWidget {
	Q_OBJECT
public:
	explicit LowerThirdDock(QWidget *parent = nullptr);
	bool init();

signals:
	void requestSave();

private slots:
	void onBrowseOutputFolder();
	void onEnsureBrowserSourceClicked();
	void onAddLowerThird();

private:
	static QString formatCountdownMs(qint64 ms);
	void updateRowCountdowns();
	void updateRowCountdownFor(const LowerThirdRowUi &rowUi);

	void rebuildList();
	void updateRowActiveStyles();

	void clearShortcuts();
	void rebuildShortcuts();

	void handleToggleVisible(const QString &id);
	void handleClone(const QString &id);
	void handleOpenSettings(const QString &id);
	void handleRemove(const QString &id);

	void ensureRepeatTimerStarted();
	void repeatTick();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	QLineEdit *outputPathEdit = nullptr;
	QPushButton *outputBrowseBtn = nullptr;
	QPushButton *ensureSourceBtn = nullptr;
	QPushButton *addBtn = nullptr;

	QScrollArea *scrollArea = nullptr;
	QWidget *listContainer = nullptr;
	QVBoxLayout *listLayout = nullptr;

	QVector<LowerThirdRowUi> rows;
	QVector<QShortcut *> shortcuts_;

	QTimer *repeatTimer_ = nullptr;
	QHash<QString, qint64> nextOnMs_;
	QHash<QString, qint64> offAtMs_;
	qint64 lastVisibleMtimeMs_ = 0;
};

} // namespace smart_lt::ui

void LowerThird_create_dock();
void LowerThird_destroy_dock();
smart_lt::ui::LowerThirdDock *LowerThird_get_dock();
