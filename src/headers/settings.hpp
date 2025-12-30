// settings.hpp
#pragma once

#include <QDialog>
#include <QString>
#include <vector>

class QLineEdit;
class QPlainTextEdit;
class QComboBox;
class QPushButton;
class QDialogButtonBox;
class QFontComboBox;
class QKeySequenceEdit;
class QLabel;
class QTabWidget;
class QSpinBox;
class QSlider;

namespace smart_lt::ui {

struct CbxOption {
	const char *label;
	const char *value;
};
inline const std::vector<CbxOption> AnimInOptions = {
	// --- Fading Entrances ---
	{"Fade In", "animate__fadeIn"},
	{"Fade In Up", "animate__fadeInUp"},
	{"Fade In Down", "animate__fadeInDown"},
	{"Fade In Left", "animate__fadeInLeft"},
	{"Fade In Right", "animate__fadeInRight"},
	{"Fade In Up Big", "animate__fadeInUpBig"},
	{"Fade In Down Big", "animate__fadeInDownBig"},
	{"Fade In Left Big", "animate__fadeInLeftBig"},
	{"Fade In Right Big", "animate__fadeInRightBig"},
	{"Fade In Top Left", "animate__fadeInTopLeft"},
	{"Fade In Top Right", "animate__fadeInTopRight"},
	{"Fade In Bottom Left", "animate__fadeInBottomLeft"},
	{"Fade In Bottom Right", "animate__fadeInBottomRight"},

	// --- Back Entrances ---
	{"Back In Up", "animate__backInUp"},
	{"Back In Down", "animate__backInDown"},
	{"Back In Left", "animate__backInLeft"},
	{"Back In Right", "animate__backInRight"},

	// --- Bouncing Entrances ---
	{"Bounce In", "animate__bounceIn"},
	{"Bounce In Up", "animate__bounceInUp"},
	{"Bounce In Down", "animate__bounceInDown"},
	{"Bounce In Left", "animate__bounceInLeft"},
	{"Bounce In Right", "animate__bounceInRight"},

	// --- Zooming Entrances ---
	{"Zoom In", "animate__zoomIn"},
	{"Zoom In Up", "animate__zoomInUp"},
	{"Zoom In Down", "animate__zoomInDown"},
	{"Zoom In Left", "animate__zoomInLeft"},
	{"Zoom In Right", "animate__zoomInRight"},

	// --- Sliding Entrances ---
	{"Slide In Up", "animate__slideInUp"},
	{"Slide In Down", "animate__slideInDown"},
	{"Slide In Left", "animate__slideInLeft"},
	{"Slide In Right", "animate__slideInRight"},

	// --- Rotating Entrances ---
	{"Rotate In", "animate__rotateIn"},
	{"Rotate In Down Left", "animate__rotateInDownLeft"},
	{"Rotate In Down Right", "animate__rotateInDownRight"},
	{"Rotate In Up Left", "animate__rotateInUpLeft"},
	{"Rotate In Up Right", "animate__rotateInUpRight"},

	// --- Flippers & Specials ---
	{"Flip In X", "animate__flipInX"},
	{"Flip In Y", "animate__flipInY"},
	{"Light Speed In Left", "animate__lightSpeedInLeft"},
	{"Light Speed In Right", "animate__lightSpeedInRight"},
	{"Jack In The Box", "animate__jackInTheBox"},
	{"Roll In", "animate__rollIn"},

	// --- Attention Seekers ---
	{"Bounce", "animate__bounce"},
	{"Flash", "animate__flash"},
	{"Pulse", "animate__pulse"},
	{"Rubber Band", "animate__rubberBand"},
	{"Shake X", "animate__shakeX"},
	{"Shake Y", "animate__shakeY"},
	{"Head Shake", "animate__headShake"},
	{"Swing", "animate__swing"},
	{"Tada", "animate__tada"},
	{"Wobble", "animate__wobble"},
	{"Jello", "animate__jello"},
	{"Heart Beat", "animate__heartBeat"},

	{"Custom (CSS class)", "custom"},
};

inline const std::vector<CbxOption> AnimOutOptions = {
	// --- Fading Exits ---
	{"Fade Out", "animate__fadeOut"},
	{"Fade Out Up", "animate__fadeOutUp"},
	{"Fade Out Down", "animate__fadeOutDown"},
	{"Fade Out Left", "animate__fadeOutLeft"},
	{"Fade Out Right", "animate__fadeOutRight"},
	{"Fade Out Up Big", "animate__fadeOutUpBig"},
	{"Fade Out Down Big", "animate__fadeOutDownBig"},
	{"Fade Out Left Big", "animate__fadeOutLeftBig"},
	{"Fade Out Right Big", "animate__fadeOutRightBig"},
	{"Fade Out Top Left", "animate__fadeOutTopLeft"},
	{"Fade Out Top Right", "animate__fadeOutTopRight"},
	{"Fade Out Bottom Left", "animate__fadeOutBottomLeft"},
	{"Fade Out Bottom Right", "animate__fadeOutBottomRight"},

	// --- Back Exits ---
	{"Back Out Up", "animate__backOutUp"},
	{"Back Out Down", "animate__backOutDown"},
	{"Back Out Left", "animate__backOutLeft"},
	{"Back Out Right", "animate__backOutRight"},

	// --- Bouncing Exits ---
	{"Bounce Out", "animate__bounceOut"},
	{"Bounce Out Up", "animate__bounceOutUp"},
	{"Bounce Out Down", "animate__bounceOutDown"},
	{"Bounce Out Left", "animate__bounceOutLeft"},
	{"Bounce Out Right", "animate__bounceOutRight"},

	// --- Zooming Exits ---
	{"Zoom Out", "animate__zoomOut"},
	{"Zoom Out Up", "animate__zoomOutUp"},
	{"Zoom Out Down", "animate__zoomOutDown"},
	{"Zoom Out Left", "animate__zoomOutLeft"},
	{"Zoom Out Right", "animate__zoomOutRight"},

	// --- Sliding Exits ---
	{"Slide Out Up", "animate__slideOutUp"},
	{"Slide Out Down", "animate__slideOutDown"},
	{"Slide Out Left", "animate__slideOutLeft"},
	{"Slide Out Right", "animate__slideOutRight"},

	// --- Rotating Exits ---
	{"Rotate Out", "animate__rotateOut"},
	{"Rotate Out Down Left", "animate__rotateOutDownLeft"},
	{"Rotate Out Down Right", "animate__rotateOutDownRight"},
	{"Rotate Out Up Left", "animate__rotateOutUpLeft"},
	{"Rotate Out Up Right", "animate__rotateOutUpRight"},

	// --- Flippers & Specials ---
	{"Flip Out X", "animate__flipOutX"},
	{"Flip Out Y", "animate__flipOutY"},
	{"Light Speed Out Left", "animate__lightSpeedOutLeft"},
	{"Light Speed Out Right", "animate__lightSpeedOutRight"},
	{"Hinge", "animate__hinge"},
	{"Roll Out", "animate__rollOut"},

	{"Custom (CSS class)", "custom"},
};

inline const std::vector<CbxOption> LtPositionOptions = {
	{"Bottom Left", "lt-pos-bottom-left"}, {"Bottom Right", "lt-pos-bottom-right"}, {"Top Left", "lt-pos-top-left"},
	{"Top Right", "lt-pos-top-right"},     {"Screen Center", "lt-pos-center"},
};

class LowerThirdSettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit LowerThirdSettingsDialog(QWidget *parent = nullptr);
	~LowerThirdSettingsDialog() override;

	void setLowerThirdId(const QString &id);

private slots:
	void onSaveAndApply();
	void onBrowseProfilePicture();
	void onPickBgColor();
	void onPickTextColor();

	void onImportTemplateClicked();
	void onExportTemplateClicked();

	void onOpenHtmlEditorDialog();
	void onOpenCssEditorDialog();
	void onOpenJsEditorDialog();

	void onAnimInChanged(int);
	void onAnimOutChanged(int);

private:
	void loadFromState();
	void saveToState();

	void openTemplateEditorDialog(const QString &title, QPlainTextEdit *sourceEdit);
	void updateCustomAnimFieldsVisibility();
	void updateColorButton(QPushButton *btn, const QColor &c);

private:
	QString currentId;
	QString pendingProfilePicturePath;

	QLineEdit *titleEdit = nullptr;
	QLineEdit *subtitleEdit = nullptr;

	QLineEdit *profilePictureEdit = nullptr;
	QPushButton *browseProfilePictureBtn = nullptr;

	QComboBox *animInCombo = nullptr;
	QComboBox *animOutCombo = nullptr;
	QLabel *customAnimInLabel = nullptr;
	QLabel *customAnimOutLabel = nullptr;
	QLineEdit *customAnimInEdit = nullptr;
	QLineEdit *customAnimOutEdit = nullptr;

	QFontComboBox *fontCombo = nullptr;
	QComboBox *posCombo = nullptr;

	QPushButton *bgColorBtn = nullptr;
	QPushButton *textColorBtn = nullptr;

	QKeySequenceEdit *hotkeyEdit = nullptr;
	QPushButton *clearHotkeyBtn = nullptr;

	QTabWidget *tplTabs = nullptr;
	QPlainTextEdit *htmlEdit = nullptr;
	QPlainTextEdit *cssEdit = nullptr;
	QPlainTextEdit *jsEdit = nullptr;

	QPushButton *importBtn = nullptr;
	QPushButton *exportBtn = nullptr;

	QDialogButtonBox *buttonBox = nullptr;

	QColor *currentBgColor = nullptr;
	QColor *currentTextColor = nullptr;

	QSlider *opacitySlider = nullptr;
	QLabel *opacityValue = nullptr;

	QSlider *radiusSlider = nullptr;
	QLabel *radiusValue = nullptr;

	QSpinBox *repeatEverySpin = nullptr;
	QSpinBox *repeatVisibleSpin = nullptr;
};

} // namespace smart_lt::ui
