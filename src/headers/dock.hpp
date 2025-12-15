#pragma once

#include <QWidget>
#include <QString>
#include <QVector>

class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;
class QLineEdit;
class QCheckBox;
class QLabel;
class QShortcut;
class QToolButton;

struct LowerThirdRowUi {
	QString id;
	QWidget *row = nullptr;
	QCheckBox *visibleCheck = nullptr;
	QLabel *thumbnailLbl = nullptr;
	QLabel *labelLbl = nullptr;
	QPushButton *cloneBtn = nullptr;
	QPushButton *settingsBtn = nullptr;
	QPushButton *removeBtn = nullptr;
};

class LowerThirdDock : public QWidget {
	Q_OBJECT
public:
	explicit LowerThirdDock(QWidget *parent = nullptr);
	bool init();
	void updateFromState();

signals:
	void requestSave(); // emitted on operations that should persist state

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
	void onBrowseOutputFolder();
	void onAddLowerThird();
	void onEnsureBrowserSourceClicked();

private:
	void rebuildList();
	void updateRowActiveStyles();

	// Carousel (preview strip)
	void rebuildCarousel();
	void updateCarouselActiveStyles();

	void handleToggleVisible(const QString &id, bool hideOthers = true);
	void handleClone(const QString &id);
	void handleOpenSettings(const QString &id);
	void handleRemove(const QString &id);

	void clearShortcuts();
	void rebuildShortcuts();

private:
	QLineEdit *outputPathEdit = nullptr;
	QPushButton *outputBrowseBtn = nullptr;
	QPushButton *addBtn = nullptr;
	QPushButton *ensureSourceBtn = nullptr;

	// Carousel widgets
	QScrollArea *carouselArea = nullptr;
	QWidget *carouselContainer = nullptr;
	QHBoxLayout *carouselLayout = nullptr;
	QVector<QToolButton *> carouselButtons;

	// Main list
	QScrollArea *scrollArea = nullptr;
	QWidget *listContainer = nullptr;
	QVBoxLayout *listLayout = nullptr;

	QVector<LowerThirdRowUi> rows;
	QVector<QShortcut *> shortcuts_;
};

void LowerThird_create_dock();
void LowerThird_destroy_dock();
LowerThirdDock *LowerThird_get_dock();
