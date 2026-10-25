#pragma once

#include <cstdint>

#include <QWidget>
#include <QVector>
#include <QHash>
#include <QSet>

struct obs_source;
typedef struct obs_source obs_source_t;
struct calldata;
typedef struct calldata calldata_t;
struct signal_handler;
typedef struct signal_handler signal_handler_t;

class QScrollArea;
class QVBoxLayout;
class QLineEdit;
class QPushButton;
class QFrame;
class QLabel;
class QToolButton;
class QShortcut;
class QEvent;
class QTimer;
class QComboBox;
class QSpinBox;
class QNetworkAccessManager;

namespace vflow {
struct core_event;
}

namespace vflow::ui {

struct LowerThirdRowUi {
	QString id;
	QFrame *row = nullptr;
	QLabel *labelLbl = nullptr;
	QLabel *subLbl = nullptr;
	QLabel *thumbnailLbl = nullptr;
	QLabel *liveLbl = nullptr;
	QString hotkeyText;
	QToolButton *upBtn = nullptr;
	QToolButton *downBtn = nullptr;
	QToolButton *cloneBtn = nullptr;
	QToolButton *settingsBtn = nullptr;
	QToolButton *removeBtn = nullptr;
};

class LowerThirdDock : public QWidget {
	Q_OBJECT
public:
	explicit LowerThirdDock(QWidget *parent = nullptr);
	~LowerThirdDock() override;

	bool init();
	void onSceneCollectionChanged();
	void refreshBrowserSources();
	void setUpdateAvailable(const QString &remoteVersion, const QString &localVersion, const QString &url);

	// Hotkey / automation entry points (UI thread)
	void toggleLowerThird(const QString &id);
	void toggleGroup(const QString &id);

	// Stops timers and group runs (OBS is exiting).
	void stopAll();

signals:
	void requestSave();

private slots:
	void onBrowseOutputFolder();
	void onAddLowerThird();
	void onManageGroups();
	void onCreateOverlaySource();

private:
	QFrame *updateFrame_ = nullptr;
	QLabel *updateLabel_ = nullptr;
	QPushButton *updateBtn_ = nullptr;
	QString updateRemote_;
	QString updateLocal_;
	QString updateUrl_;
	QNetworkAccessManager *net_ = nullptr;

	QFrame *statusFrame_ = nullptr;
	QLabel *statusLabel_ = nullptr;
	QPushButton *statusBtn_ = nullptr;
	QLabel *emptyStateLbl_ = nullptr;
	QWidget *setupPanel_ = nullptr;
	QToolButton *setupToggleBtn_ = nullptr;
	QPushButton *createSourceBtn_ = nullptr;
	QPushButton *matchCanvasBtn_ = nullptr;
	QPushButton *openFolderBtn_ = nullptr;

	QToolButton *manageGroupsBtn_ = nullptr;

	static QString formatCountdownMs(qint64 ms);
	void updateRowCountdowns();
	void updateRowCountdownFor(const LowerThirdRowUi &rowUi);

	void rebuildList();
	void updateRowActiveStyles();
	void updateSetupState();
	void applySceneCollection();
	void setSetupExpanded(bool expanded);
	void checkForUpdates();
	void applyDockStyle();

	void handleToggleVisible(const QString &id);
	void handleClone(const QString &id);
	void handleOpenSettings(const QString &id);
	void handleRemove(const QString &id);

	void ensureRepeatTimerStarted();
	void repeatTick();

	void populateBrowserSources(bool keepSelection = true);
	void onBrowserSourceChanged(int index);
	void onBrowserSizeChanged();

	void onCoreEvent(const vflow::core_event &ev);
	static void coreEventThunk(const vflow::core_event &ev, void *user);

	static void onObsSourceEvent(void *data, calldata_t *cd);
	void connectObsSignals();
	void disconnectObsSignals();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
	void changeEvent(QEvent *event) override;

private:
	QLineEdit *outputPathEdit = nullptr;
	QPushButton *outputBrowseBtn = nullptr;

	QComboBox *browserSourceCombo = nullptr;
	QPushButton *refreshSourcesBtn = nullptr;

	QSpinBox *browserWidthSpin = nullptr;
	QSpinBox *browserHeightSpin = nullptr;

	QToolButton *infoBtn = nullptr;
	QToolButton *addBtn = nullptr;

	QScrollArea *scrollArea = nullptr;
	QWidget *listContainer = nullptr;
	QVBoxLayout *listLayout = nullptr;

	QVector<LowerThirdRowUi> rows;

	QTimer *repeatTimer_ = nullptr;
	QHash<QString, qint64> nextOnMs_;
	QHash<QString, qint64> offAtMs_;
	QHash<QString, qint64> reShowAtMs_;

	bool populatingSources_ = false;
	bool collectionRefreshPending_ = false;
	signal_handler_t *obsSignalHandler_ = nullptr;
	bool obsSignalsConnected_ = false;

	uint64_t coreListenerToken_ = 0;

	// Theme handling (see applyDockStyle / changeEvent)
	bool applyingStyle_ = false;
	qint64 paletteKey_ = 0;
};

} // namespace vflow::ui

void LowerThird_create_dock();
void LowerThird_destroy_dock();

vflow::ui::LowerThirdDock *LowerThird_get_dock();