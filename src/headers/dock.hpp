#pragma once

#include <QWidget>
#include <QString>
#include <QVector>

class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QLineEdit;
class QCheckBox;
class QLabel;
class QFrame;
class QSpinBox;
class QShortcut;

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
	void requestSave();

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
	void onAddLowerThird();
	void onBrowseOutputFolder();
	void onToggleServer();

private:
	void rebuildList();
	void updateRowActiveStyles();
	void handleToggleVisible(const QString &id);
	void handleClone(const QString &id);
	void handleOpenSettings(const QString &id);
	void handleRemove(const QString &id);

	void clearShortcuts();
	void rebuildShortcuts();

	void updateServerUi();

private:
	QLineEdit *outputPathEdit = nullptr;
	QPushButton *outputBrowseBtn = nullptr;

	QFrame *serverStatusDot = nullptr;
	QSpinBox *serverPortSpin = nullptr;
	QPushButton *serverToggleBtn = nullptr;

	QPushButton *addBtn = nullptr;
	QScrollArea *scrollArea = nullptr;
	QWidget *listContainer = nullptr;
	QVBoxLayout *listLayout = nullptr;

	QVector<LowerThirdRowUi> rows;
	QVector<QShortcut *> shortcuts_;
};

void LowerThird_create_dock();
void LowerThird_destroy_dock();
LowerThirdDock *LowerThird_get_dock();
