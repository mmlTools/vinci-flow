// settings.hpp
#pragma once
#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;
class QLineEdit;
class QComboBox;
class QPlainTextEdit;
class QColor;
class QDialogButtonBox;
class QFontComboBox;
class QFont;
class QKeySequenceEdit;
class QKeySequence;

class LowerThirdSettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit LowerThirdSettingsDialog(QWidget *parent = nullptr);
	~LowerThirdSettingsDialog() override;

	void setLowerThirdId(const QString &id);

private slots:
	void onPickBgColor();
	void onPickTextColor();
	void onSaveAndApply();

	void onTitleChanged(const QString &text);
	void onSubtitleChanged(const QString &text);
	void onAnimInChanged(int index);
	void onAnimOutChanged(int index);
	void updateCustomAnimFieldsVisibility();
	void onCustomAnimInChanged(const QString &text);
	void onCustomAnimOutChanged(const QString &text);
	void onFontChanged(const QFont &font);
	void onHotkeyChanged(const QKeySequence &seq);
	void onBrowseProfilePicture();

	void onSceneBindingChanged(int index);

	void onImportTemplateClicked();
	void onExportTemplateClicked();

	void onOpenHtmlEditorDialog();
	void onOpenCssEditorDialog();

private:
	void loadFromState();
	void saveToState();
	void updateColorButton(QPushButton *btn, const QColor &color);
    void openTemplateEditorDialog(const QString &title, QPlainTextEdit *sourceEdit);

private:
	QString currentId;

	QLineEdit *titleEdit = nullptr;
	QLineEdit *subtitleEdit = nullptr;
	QComboBox *animInCombo = nullptr;
	QComboBox *animOutCombo = nullptr;
	QLineEdit *customAnimInEdit = nullptr;
	QLineEdit *customAnimOutEdit = nullptr;
	QLabel *customAnimInLabel = nullptr;
	QLabel *customAnimOutLabel = nullptr;

	QFontComboBox *fontCombo = nullptr;
	QKeySequenceEdit *hotkeyEdit = nullptr;

	QComboBox *sceneCombo = nullptr;

	QPushButton *bgColorBtn = nullptr;
	QPushButton *textColorBtn = nullptr;

	QPlainTextEdit *htmlEdit = nullptr;
	QPlainTextEdit *cssEdit = nullptr;

	QDialogButtonBox *buttonBox = nullptr;

	QColor *currentBgColor = nullptr;
	QColor *currentTextColor = nullptr;

	QLineEdit *profilePictureEdit = nullptr;
	QPushButton *browseProfilePictureBtn = nullptr;
	QString pendingProfilePicturePath;
};
