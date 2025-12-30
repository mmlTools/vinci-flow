// settings.cpp
#define LOG_TAG "[" PLUGIN_NAME "][settings]"
#include "settings.hpp"

#include "core.hpp"

#include <obs.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QKeySequenceEdit>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QTemporaryDir>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStyle>
#include <QTabWidget>
#include <QWidget>
#include <QSpinBox>
#include <QSlider>

#include <unzip.h>
#include <zip.h>
#include <algorithm>
#include <cstring>

namespace smart_lt::ui {

// ----------------------------
// unzip / zip helpers (kept)
// ----------------------------
static bool unzip_to_dir(const QString &zipPath, const QString &destDir, QString &htmlPath, QString &cssPath,
			 QString &jsPath, QString &jsonPath, QString &profilePath)
{
	htmlPath.clear();
	cssPath.clear();
	jsPath.clear();
	jsonPath.clear();
	profilePath.clear();

	unzFile zip = unzOpen(zipPath.toUtf8().constData());
	if (!zip)
		return false;

	if (unzGoToFirstFile(zip) != UNZ_OK) {
		unzClose(zip);
		return false;
	}

	do {
		char filename[512] = {0};
		unz_file_info info;

		if (unzGetCurrentFileInfo(zip, &info, filename, sizeof(filename), nullptr, 0, nullptr, 0) != UNZ_OK)
			break;

		const QString name = QString::fromUtf8(filename);
		const QString outPath = destDir + "/" + name;

		const QFileInfo fi(outPath);
		QDir().mkpath(fi.path());

		if (unzOpenCurrentFile(zip) != UNZ_OK)
			break;

		QFile out(outPath);
		if (!out.open(QIODevice::WriteOnly)) {
			unzCloseCurrentFile(zip);
			break;
		}

		constexpr int BUF = 8192;
		char buffer[BUF];

		int bytesRead = 0;
		while ((bytesRead = unzReadCurrentFile(zip, buffer, BUF)) > 0) {
			out.write(buffer, bytesRead);
		}

		out.close();
		unzCloseCurrentFile(zip);

		if (name == "template.html")
			htmlPath = outPath;
		else if (name == "template.css")
			cssPath = outPath;
		else if (name == "template.js")
			jsPath = outPath;
		else if (name == "template.json")
			jsonPath = outPath;
		else if (name.startsWith("profile.") || name.contains("profile", Qt::CaseInsensitive))
			profilePath = outPath;

	} while (unzGoToNextFile(zip) == UNZ_OK);

	unzClose(zip);
	return (!htmlPath.isEmpty() && !cssPath.isEmpty() && !jsonPath.isEmpty());
}

static bool zip_write_file(zipFile zf, const char *internalName, const QByteArray &data)
{
	zip_fileinfo zi;
	std::memset(&zi, 0, sizeof(zi));

	const int errOpen = zipOpenNewFileInZip(zf, internalName, &zi, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED,
						Z_BEST_COMPRESSION);
	if (errOpen != ZIP_OK)
		return false;

	if (!data.isEmpty()) {
		const int errWrite = zipWriteInFileInZip(zf, data.constData(), data.size());
		if (errWrite != ZIP_OK) {
			zipCloseFileInZip(zf);
			return false;
		}
	}

	zipCloseFileInZip(zf);
	return true;
}

// ----------------------------
// Dialog
// ----------------------------
LowerThirdSettingsDialog::LowerThirdSettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(tr("Lower Third Settings"));
	resize(820, 720);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(10, 10, 10, 10);
	root->setSpacing(10);

	// Content & Media
	{
		auto *contentBox = new QGroupBox(tr("Content && Media"), this);
		auto *g = new QGridLayout(contentBox);
		g->setSpacing(8);

		int row = 0;

		// Row 0: Title
		g->addWidget(new QLabel(tr("Title:"), this), row, 0);
		titleEdit = new QLineEdit(this);
		g->addWidget(titleEdit, row, 1, 1, 3);

		row++;
		// Row 1: Subtitle
		g->addWidget(new QLabel(tr("Subtitle:"), this), row, 0);
		subtitleEdit = new QLineEdit(this);
		g->addWidget(subtitleEdit, row, 1, 1, 3);

		row++;
		// Row 2: Profile Picture
		g->addWidget(new QLabel(tr("Profile picture:"), this), row, 0);
		auto *picRow = new QHBoxLayout();
		picRow->setContentsMargins(0, 0, 0, 0);

		profilePictureEdit = new QLineEdit(this);
		profilePictureEdit->setReadOnly(true);

		browseProfilePictureBtn = new QPushButton(this);
		browseProfilePictureBtn->setCursor(Qt::PointingHandCursor);
		browseProfilePictureBtn->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
		browseProfilePictureBtn->setToolTip(tr("Browse profile picture..."));
		browseProfilePictureBtn->setFixedWidth(32);

		picRow->addWidget(profilePictureEdit, 1);
		picRow->addWidget(browseProfilePictureBtn);
		g->addLayout(picRow, row, 1, 1, 3);

		row++;
		g->addWidget(new QLabel(tr("Hotkey:"), this), row, 0);

		auto *hkRow = new QHBoxLayout();
		hkRow->setContentsMargins(0, 0, 0, 0);
		hkRow->setSpacing(4);

		hotkeyEdit = new QKeySequenceEdit(this);

		clearHotkeyBtn = new QPushButton(this);
		clearHotkeyBtn->setToolTip(tr("Clear hotkey"));
		clearHotkeyBtn->setCursor(Qt::PointingHandCursor);
		clearHotkeyBtn->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
		clearHotkeyBtn->setFixedWidth(32);
		clearHotkeyBtn->setFocusPolicy(Qt::NoFocus);

		hkRow->addWidget(hotkeyEdit, 1);
		hkRow->addWidget(clearHotkeyBtn);
		g->addLayout(hkRow, row, 1, 1, 3);

		row++;
		g->addWidget(new QLabel(tr("Repeat every (sec):"), this), row, 0);
		repeatEverySpin = new QSpinBox(this);
		repeatEverySpin->setRange(0, 24 * 60 * 60); // up to 24h
		repeatEverySpin->setToolTip(tr("0 disables auto-repeat"));
		g->addWidget(repeatEverySpin, row, 1);

		g->addWidget(new QLabel(tr("Keep visible (sec):"), this), row, 2);
		repeatVisibleSpin = new QSpinBox(this);
		repeatVisibleSpin->setRange(0, 24 * 60 * 60);
		repeatVisibleSpin->setToolTip(tr("0 uses default (recommended: 3-8 sec)"));
		g->addWidget(repeatVisibleSpin, row, 3);

		root->addWidget(contentBox);

		// Connections
		connect(clearHotkeyBtn, &QPushButton::clicked, this,
			[this]() { hotkeyEdit->setKeySequence(QKeySequence()); });

		connect(browseProfilePictureBtn, &QPushButton::clicked, this,
			&LowerThirdSettingsDialog::onBrowseProfilePicture);
	}

	// Style
	{
		auto *styleBox = new QGroupBox(tr("Style"), this);
		auto *g = new QGridLayout(styleBox);

		int row = 0;

		g->addWidget(new QLabel(tr("Anim In:"), this), row, 0);
		animInCombo = new QComboBox(this);
		for (const auto &opt : AnimInOptions)
			animInCombo->addItem(tr(opt.label), QString::fromUtf8(opt.value));
		g->addWidget(animInCombo, row, 1);

		g->addWidget(new QLabel(tr("Anim Out:"), this), row, 2);
		animOutCombo = new QComboBox(this);
		for (const auto &opt : AnimOutOptions)
			animOutCombo->addItem(tr(opt.label), QString::fromUtf8(opt.value));
		g->addWidget(animOutCombo, row, 3);

		row++;
		customAnimInLabel = new QLabel(tr("Custom In class:"), this);
		customAnimInEdit = new QLineEdit(this);
		customAnimInEdit->setPlaceholderText(tr("e.g. myFadeIn"));

		customAnimOutLabel = new QLabel(tr("Custom Out class:"), this);
		customAnimOutEdit = new QLineEdit(this);
		customAnimOutEdit->setPlaceholderText(tr("e.g. myFadeOut"));

		g->addWidget(customAnimInLabel, row, 0);
		g->addWidget(customAnimInEdit, row, 1);
		g->addWidget(customAnimOutLabel, row, 2);
		g->addWidget(customAnimOutEdit, row, 3);

		row++;
		g->addWidget(new QLabel(tr("Font:"), this), row, 0);
		fontCombo = new QFontComboBox(this);
		fontCombo->setEditable(false);
		g->addWidget(fontCombo, row, 1);

		g->addWidget(new QLabel(tr("Position:"), this), row, 2);
		posCombo = new QComboBox(this);
		for (const auto &opt : LtPositionOptions)
			posCombo->addItem(tr(opt.label), QString::fromUtf8(opt.value));
		g->addWidget(posCombo, row, 3);

		row++;
		g->addWidget(new QLabel(tr("Background:"), this), row, 0);
		bgColorBtn = new QPushButton(tr("Pick"), this);
		g->addWidget(bgColorBtn, row, 1);

		g->addWidget(new QLabel(tr("Text color:"), this), row, 2);
		textColorBtn = new QPushButton(tr("Pick"), this);
		g->addWidget(textColorBtn, row, 3);

		row++;
		g->addWidget(new QLabel(tr("Opacity:"), this), row, 0);

		opacitySlider = new QSlider(Qt::Horizontal, this);
		opacitySlider->setRange(0, 100); // 0..100
		opacitySlider->setSingleStep(5); // 0.05 steps
		opacitySlider->setPageStep(10);
		opacitySlider->setToolTip(tr("0 = transparent, 100 = opaque"));
		g->addWidget(opacitySlider, row, 1);

		opacityValue = new QLabel(this);
		opacityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		g->addWidget(opacityValue, row, 2, 1, 2); // span 2 cols

		row++;
		g->addWidget(new QLabel(tr("Radius:"), this), row, 0);

		radiusSlider = new QSlider(Qt::Horizontal, this);
		radiusSlider->setRange(0, 100); // 0..100
		radiusSlider->setSingleStep(1);
		radiusSlider->setPageStep(5);
		radiusSlider->setToolTip(tr("Border radius percentage (0-100)"));
		g->addWidget(radiusSlider, row, 1);

		radiusValue = new QLabel(this);
		radiusValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		g->addWidget(radiusValue, row, 2, 1, 2);

		root->addWidget(styleBox);

		auto updateOpacityLabel = [this]() {
			const int v = opacitySlider ? opacitySlider->value() : 0;
			if (opacityValue)
				opacityValue->setText(QString("%1 Units").arg(v));
		};

		auto updateRadiusLabel = [this]() {
			const int v = radiusSlider ? radiusSlider->value() : 0;
			if (radiusValue)
				radiusValue->setText(QString("%1 Units").arg(v));
		};

		connect(opacitySlider, &QSlider::valueChanged, this,
			[updateOpacityLabel](int) { updateOpacityLabel(); });
		connect(radiusSlider, &QSlider::valueChanged, this, [updateRadiusLabel](int) { updateRadiusLabel(); });

		updateOpacityLabel();
		updateRadiusLabel();

		connect(bgColorBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onPickBgColor);
		connect(textColorBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onPickTextColor);

		connect(animInCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			&LowerThirdSettingsDialog::onAnimInChanged);
		connect(animOutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			&LowerThirdSettingsDialog::onAnimOutChanged);
	}

	// Templates (with Import/Export)
	{
		auto *tplCard = new QGroupBox(tr("Templates"), this);
		auto *tplLayout = new QVBoxLayout(tplCard);

		tplTabs = new QTabWidget(this);
		tplTabs->setDocumentMode(true);

		auto makeTab = [&](const QString &name, QPlainTextEdit *&edit) {
			auto *page = new QWidget(this);
			auto *v = new QVBoxLayout(page);
			v->setContentsMargins(0, 0, 0, 0);

			edit = new QPlainTextEdit(page);
			edit->setLineWrapMode(QPlainTextEdit::NoWrap);
			// Set a monospace font for code
			edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

			v->addWidget(edit);
			tplTabs->addTab(page, name);
		};

		makeTab(tr("HTML"), htmlEdit);
		makeTab(tr("CSS"), cssEdit);
		makeTab(tr("JS"), jsEdit);

		// 1. Create the button
		auto *expandBtn = new QPushButton(this);
		expandBtn->setFlat(true);
		expandBtn->setCursor(Qt::PointingHandCursor);
		expandBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
		expandBtn->setToolTip(tr("Open editor..."));
		expandBtn->setFixedSize(32, 28);

		// 2. Simply set it as the Corner Widget of the Tab Bar
		// Qt handles the absolute positioning in the top-right automatically
		tplTabs->setCornerWidget(expandBtn, Qt::TopRightCorner);

		// 3. Logic to open the correct editor based on the active tab
		connect(expandBtn, &QPushButton::clicked, this, [this]() {
			int idx = tplTabs->currentIndex();
			if (idx == 0)
				onOpenHtmlEditorDialog();
			else if (idx == 1)
				onOpenCssEditorDialog();
			else if (idx == 2)
				onOpenJsEditorDialog();
		});

		tplLayout->addWidget(tplTabs, 1);
		root->addWidget(tplCard, 1);
	}

	// Footer: Import/Export (left) + Cancel / Save&Apply (right)
	{
		auto *footer = new QHBoxLayout();
		footer->setContentsMargins(0, 0, 0, 0);
		footer->setSpacing(8);

		importBtn = new QPushButton(this);
		importBtn->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
		importBtn->setCursor(Qt::PointingHandCursor);
		importBtn->setToolTip(tr("Import template from ZIP..."));
		importBtn->setText(tr("Import"));

		exportBtn = new QPushButton(this);
		exportBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
		exportBtn->setCursor(Qt::PointingHandCursor);
		exportBtn->setToolTip(tr("Export template to ZIP..."));
		exportBtn->setText(tr("Export"));

		footer->addWidget(importBtn);
		footer->addWidget(exportBtn);
		footer->addStretch(1);

		buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
		auto *applyBtn = buttonBox->addButton(tr("Save && Apply"), QDialogButtonBox::AcceptRole);

		footer->addWidget(buttonBox);

		root->addLayout(footer);

		connect(importBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onImportTemplateClicked);
		connect(exportBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onExportTemplateClicked);

		connect(buttonBox, &QDialogButtonBox::rejected, this, &LowerThirdSettingsDialog::reject);
		connect(buttonBox, &QDialogButtonBox::accepted, this, &LowerThirdSettingsDialog::onSaveAndApply);
		connect(applyBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onSaveAndApply);
	}

	connect(customAnimInEdit, &QLineEdit::textChanged, this,
		[this](const QString &) { updateCustomAnimFieldsVisibility(); });
	connect(customAnimOutEdit, &QLineEdit::textChanged, this,
		[this](const QString &) { updateCustomAnimFieldsVisibility(); });

	updateCustomAnimFieldsVisibility();
}

LowerThirdSettingsDialog::~LowerThirdSettingsDialog()
{
	delete currentBgColor;
	delete currentTextColor;
}

void LowerThirdSettingsDialog::setLowerThirdId(const QString &id)
{
	currentId = id;
	loadFromState();
}

void LowerThirdSettingsDialog::loadFromState()
{
	if (currentId.isEmpty())
		return;

	auto *cfg = smart_lt::get_by_id(currentId.toStdString());
	if (!cfg)
		return;

	titleEdit->setText(QString::fromStdString(cfg->title));
	subtitleEdit->setText(QString::fromStdString(cfg->subtitle));

	auto setCombo = [](QComboBox *cb, const QString &v) {
		const int idx = cb->findData(v);
		cb->setCurrentIndex(idx >= 0 ? idx : 0);
	};

	setCombo(animInCombo, QString::fromStdString(cfg->anim_in));
	setCombo(animOutCombo, QString::fromStdString(cfg->anim_out));

	customAnimInEdit->setText(QString::fromStdString(cfg->custom_anim_in));
	customAnimOutEdit->setText(QString::fromStdString(cfg->custom_anim_out));

	if (!cfg->font_family.empty())
		fontCombo->setCurrentFont(QFont(QString::fromStdString(cfg->font_family)));

	setCombo(posCombo, QString::fromStdString(cfg->lt_position));

	hotkeyEdit->setKeySequence(QKeySequence(QString::fromStdString(cfg->hotkey)));
	repeatEverySpin->setValue(cfg->repeat_every_sec);
	repeatVisibleSpin->setValue(cfg->repeat_visible_sec);

	htmlEdit->setPlainText(QString::fromStdString(cfg->html_template));
	cssEdit->setPlainText(QString::fromStdString(cfg->css_template));
	jsEdit->setPlainText(QString::fromStdString(cfg->js_template));

	if (cfg->profile_picture.empty())
		profilePictureEdit->clear();
	else
		profilePictureEdit->setText(QString::fromStdString(cfg->profile_picture));

	delete currentBgColor;
	currentBgColor = nullptr;
	delete currentTextColor;
	currentTextColor = nullptr;

	QColor bg(QString::fromStdString(cfg->bg_color));
	QColor fg(QString::fromStdString(cfg->text_color));
	if (!bg.isValid())
		bg = QColor(17, 24, 39);
	if (!fg.isValid())
		fg = QColor(249, 250, 251);

	currentBgColor = new QColor(bg);
	currentTextColor = new QColor(fg);

	updateColorButton(bgColorBtn, bg);
	updateColorButton(textColorBtn, fg);

	// Defaults if missing (defensive)
	int op = 85;
	int rad = 18;

	// If you added these to cfg already:
	op = cfg->opacity;
	rad = cfg->radius;

	// Clamp defensively
	op = std::max(0, std::min(100, op));
	rad = std::max(0, std::min(100, rad));

	// Enforce 0.05 steps on UI (multiple of 5)
	op = (op / 5) * 5;

	if (opacitySlider)
		opacitySlider->setValue(op);
	if (radiusSlider)
		radiusSlider->setValue(rad);

	// Update labels
	if (opacityValue) {
		opacityValue->setText(QString("%1 Units").arg(op));
	}
	if (radiusValue) {
		radiusValue->setText(QString("%1 Units").arg(rad));
	}

	pendingProfilePicturePath.clear();
	updateCustomAnimFieldsVisibility();
}

void LowerThirdSettingsDialog::saveToState()
{
	if (currentId.isEmpty())
		return;

	auto *cfg = smart_lt::get_by_id(currentId.toStdString());
	if (!cfg)
		return;

	cfg->title = titleEdit->text().toStdString();
	cfg->subtitle = subtitleEdit->text().toStdString();

	cfg->anim_in = animInCombo->currentData().toString().toStdString();
	cfg->anim_out = animOutCombo->currentData().toString().toStdString();
	cfg->custom_anim_in = customAnimInEdit->text().toStdString();
	cfg->custom_anim_out = customAnimOutEdit->text().toStdString();

	cfg->font_family = fontCombo->currentFont().family().toStdString();
	cfg->lt_position = posCombo->currentData().toString().toStdString();

	if (currentBgColor)
		cfg->bg_color = currentBgColor->name(QColor::HexRgb).toStdString();
	if (currentTextColor)
		cfg->text_color = currentTextColor->name(QColor::HexRgb).toStdString();

	if (opacitySlider) {
		int op = opacitySlider->value();
		op = std::max(0, std::min(100, op));
		op = (op / 5) * 5;
		cfg->opacity = op;
	}

	if (radiusSlider) {
		int rad = radiusSlider->value();
		rad = std::max(0, std::min(100, rad));
		cfg->radius = rad;
	}

	cfg->hotkey = hotkeyEdit->keySequence().toString(QKeySequence::PortableText).toStdString();
	cfg->repeat_every_sec = repeatEverySpin->value();
	cfg->repeat_visible_sec = repeatVisibleSpin->value();

	cfg->html_template = htmlEdit->toPlainText().toStdString();
	cfg->css_template = cssEdit->toPlainText().toStdString();
	cfg->js_template = jsEdit->toPlainText().toStdString();

	// Copy profile picture into output dir
	if (!pendingProfilePicturePath.isEmpty() && smart_lt::has_output_dir()) {
		const QString outDir = QString::fromStdString(smart_lt::output_dir());
		QDir dir(outDir);

		if (!cfg->profile_picture.empty()) {
			const QString oldPath = dir.filePath(QString::fromStdString(cfg->profile_picture));
			if (QFile::exists(oldPath))
				QFile::remove(oldPath);
		}

		const QFileInfo fi(pendingProfilePicturePath);
		const QString ext = fi.suffix().toLower();
		const qint64 ts = QDateTime::currentMSecsSinceEpoch();

		QString newFileName = QString("%1_%2").arg(QString::fromStdString(cfg->id)).arg(ts);
		if (!ext.isEmpty())
			newFileName += "." + ext;

		const QString destPath = dir.filePath(newFileName);
		QFile::remove(destPath);

		if (QFile::copy(pendingProfilePicturePath, destPath)) {
			cfg->profile_picture = newFileName.toStdString();
			profilePictureEdit->setText(newFileName);
		} else {
			LOGW("Failed to copy profile picture '%s' -> '%s'",
			     pendingProfilePicturePath.toUtf8().constData(), destPath.toUtf8().constData());
		}

		pendingProfilePicturePath.clear();
	}

	smart_lt::save_state_json();
}

void LowerThirdSettingsDialog::onSaveAndApply()
{
	saveToState();

	// Save & Apply is a rewrite trigger
	smart_lt::rebuild_and_swap();

	accept();
}

void LowerThirdSettingsDialog::onBrowseProfilePicture()
{
	const QString filter = tr("Images (*.png *.jpg *.jpeg *.webp *.gif);;All Files (*.*)");
	const QString file = QFileDialog::getOpenFileName(this, tr("Select profile picture"), QString(), filter);
	if (file.isEmpty())
		return;

	pendingProfilePicturePath = file;
	profilePictureEdit->setText(file);
}

void LowerThirdSettingsDialog::onPickBgColor()
{
	QColor start = currentBgColor ? *currentBgColor : QColor(17, 24, 39);
	QColor c = QColorDialog::getColor(start, this, tr("Pick background color"));
	if (!c.isValid())
		return;

	if (!currentBgColor)
		currentBgColor = new QColor(c);
	else
		*currentBgColor = c;

	updateColorButton(bgColorBtn, c);
}

void LowerThirdSettingsDialog::onPickTextColor()
{
	QColor start = currentTextColor ? *currentTextColor : QColor(249, 250, 251);
	QColor c = QColorDialog::getColor(start, this, tr("Pick text color"));
	if (!c.isValid())
		return;

	if (!currentTextColor)
		currentTextColor = new QColor(c);
	else
		*currentTextColor = c;

	updateColorButton(textColorBtn, c);
}

void LowerThirdSettingsDialog::updateCustomAnimFieldsVisibility()
{
	const QString key = QStringLiteral("custom");
	const bool inCustom = (animInCombo->currentData().toString() == key);
	const bool outCustom = (animOutCombo->currentData().toString() == key);

	customAnimInLabel->setVisible(inCustom);
	customAnimInEdit->setVisible(inCustom);

	customAnimOutLabel->setVisible(outCustom);
	customAnimOutEdit->setVisible(outCustom);
}

void LowerThirdSettingsDialog::onAnimInChanged(int)
{
	updateCustomAnimFieldsVisibility();
}
void LowerThirdSettingsDialog::onAnimOutChanged(int)
{
	updateCustomAnimFieldsVisibility();
}

void LowerThirdSettingsDialog::updateColorButton(QPushButton *btn, const QColor &c)
{
	const QString hex = c.name(QColor::HexRgb);
	btn->setStyleSheet(
		QString("background-color:%1;border:1px solid rgba(255,255,255,0.20);border-radius:8px;padding:6px 10px;min-width:90px;")
			.arg(hex));
	btn->setText(hex);
}

void LowerThirdSettingsDialog::openTemplateEditorDialog(const QString &title, QPlainTextEdit *sourceEdit)
{
	if (!sourceEdit)
		return;

	QDialog dlg(this);
	dlg.setWindowTitle(title);
	dlg.resize(980, 760);

	auto *layout = new QVBoxLayout(&dlg);
	auto *big = new QPlainTextEdit(&dlg);
	big->setPlainText(sourceEdit->toPlainText());
	big->setLineWrapMode(QPlainTextEdit::NoWrap);

	auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	layout->addWidget(big, 1);
	layout->addWidget(box);

	connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	if (dlg.exec() == QDialog::Accepted)
		sourceEdit->setPlainText(big->toPlainText());
}

void LowerThirdSettingsDialog::onOpenHtmlEditorDialog()
{
	openTemplateEditorDialog(tr("Edit HTML Template"), htmlEdit);
}
void LowerThirdSettingsDialog::onOpenCssEditorDialog()
{
	openTemplateEditorDialog(tr("Edit CSS Template"), cssEdit);
}
void LowerThirdSettingsDialog::onOpenJsEditorDialog()
{
	openTemplateEditorDialog(tr("Edit JS Template"), jsEdit);
}

// ----------------------------
// Import / Export template ZIP
// ----------------------------
void LowerThirdSettingsDialog::onExportTemplateClicked()
{
	if (currentId.isEmpty()) {
		QMessageBox::warning(this, tr("Export"), tr("No lower third selected."));
		return;
	}

	auto *cfg = smart_lt::get_by_id(currentId.toStdString());
	if (!cfg)
		return;

	const QString outZip = QFileDialog::getSaveFileName(
		this, tr("Export template ZIP"), QString("slt-template-%1.zip").arg(QString::fromStdString(cfg->id)),
		tr("Template ZIP (*.zip)"));
	if (outZip.isEmpty())
		return;

	zipFile zf = zipOpen(outZip.toUtf8().constData(), APPEND_STATUS_CREATE);
	if (!zf) {
		QMessageBox::warning(this, tr("Export"), tr("Cannot create ZIP file."));
		return;
	}

	// template.html/css/js are editor content (not merged files)
	const QByteArray html = htmlEdit->toPlainText().toUtf8();
	const QByteArray css = cssEdit->toPlainText().toUtf8();
	const QByteArray js = jsEdit->toPlainText().toUtf8();

	// template.json
	QJsonObject o;
	o["title"] = QString::fromStdString(cfg->title);
	o["subtitle"] = QString::fromStdString(cfg->subtitle);
	o["anim_in"] = QString::fromStdString(cfg->anim_in);
	o["anim_out"] = QString::fromStdString(cfg->anim_out);
	o["custom_anim_in"] = QString::fromStdString(cfg->custom_anim_in);
	o["custom_anim_out"] = QString::fromStdString(cfg->custom_anim_out);
	o["font_family"] = QString::fromStdString(cfg->font_family);
	o["lt_position"] = QString::fromStdString(cfg->lt_position);
	o["bg_color"] = QString::fromStdString(cfg->bg_color);
	o["text_color"] = QString::fromStdString(cfg->text_color);
	o["opacity"] = cfg->opacity;
	o["radius"] = cfg->radius;
	o["hotkey"] = QString::fromStdString(cfg->hotkey);
	o["repeat_every_sec"] = cfg->repeat_every_sec;
	o["repeat_visible_sec"] = cfg->repeat_visible_sec;

	const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Indented);

	bool ok = true;
	ok = ok && zip_write_file(zf, "template.html", html);
	ok = ok && zip_write_file(zf, "template.css", css);
	ok = ok && zip_write_file(zf, "template.js", js);
	ok = ok && zip_write_file(zf, "template.json", json);

	// profile picture (optional) from output folder
	if (ok && smart_lt::has_output_dir() && !cfg->profile_picture.empty()) {
		const QString picPath = QDir(QString::fromStdString(smart_lt::output_dir()))
						.filePath(QString::fromStdString(cfg->profile_picture));
		QFile f(picPath);
		if (f.open(QIODevice::ReadOnly)) {
			const QByteArray picData = f.readAll();
			f.close();

			const QString ext = QFileInfo(picPath).suffix().toLower();
			const QString internal = ext.isEmpty() ? "profile" : QString("profile.%1").arg(ext);
			ok = ok && zip_write_file(zf, internal.toUtf8().constData(), picData);
		}
	}

	zipClose(zf, nullptr);

	if (!ok) {
		QMessageBox::warning(this, tr("Export"), tr("Failed to write one or more files into ZIP."));
		return;
	}

	QMessageBox::information(this, tr("Export"), tr("Template exported successfully."));
}

void LowerThirdSettingsDialog::onImportTemplateClicked()
{
	if (currentId.isEmpty()) {
		QMessageBox::warning(this, tr("Import"), tr("No lower third selected."));
		return;
	}

	const QString zipPath =
		QFileDialog::getOpenFileName(this, tr("Select template ZIP"), QString(), tr("Template ZIP (*.zip)"));
	if (zipPath.isEmpty())
		return;

	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		QMessageBox::warning(this, tr("Error"), tr("Unable to create temp directory."));
		return;
	}

	QString htmlPath, cssPath, jsPath, jsonPath, profilePicPath;
	if (!unzip_to_dir(zipPath, tempDir.path(), htmlPath, cssPath, jsPath, jsonPath, profilePicPath)) {
		QMessageBox::warning(this, tr("Error"),
				     tr("ZIP must contain template.html, template.css and template.json."));
		return;
	}

	QFile jsonFile(jsonPath);
	if (!jsonFile.open(QIODevice::ReadOnly)) {
		QMessageBox::warning(this, tr("Error"), tr("Cannot read template.json."));
		return;
	}

	QJsonParseError err;
	const QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll(), &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) {
		QMessageBox::warning(this, tr("Error"), tr("Invalid JSON file."));
		return;
	}

	auto *cfg = smart_lt::get_by_id(currentId.toStdString());
	if (!cfg)
		return;

	const QJsonObject obj = doc.object();

	cfg->title = obj.value("title").toString().toStdString();
	cfg->subtitle = obj.value("subtitle").toString().toStdString();
	cfg->anim_in = obj.value("anim_in").toString().toStdString();
	cfg->anim_out = obj.value("anim_out").toString().toStdString();
	cfg->custom_anim_in = obj.value("custom_anim_in").toString().toStdString();
	cfg->custom_anim_out = obj.value("custom_anim_out").toString().toStdString();
	cfg->font_family = obj.value("font_family").toString().toStdString();
	cfg->lt_position = obj.value("lt_position").toString().toStdString();
	cfg->bg_color = obj.value("bg_color").toString().toStdString();
	cfg->text_color = obj.value("text_color").toString().toStdString();
	cfg->opacity = obj.value("opacity").toInt(cfg->opacity);
	cfg->radius = obj.value("radius").toInt(cfg->radius);

	cfg->opacity = std::max(0, std::min(100, cfg->opacity));
	cfg->opacity = (cfg->opacity / 5) * 5;
	cfg->radius = std::max(0, std::min(100, cfg->radius));

	cfg->hotkey = obj.value("hotkey").toString().toStdString();
	cfg->repeat_every_sec = obj.value("repeat_every_sec").toInt(0);
	cfg->repeat_visible_sec = obj.value("repeat_visible_sec").toInt(0);

	{
		QFile f(htmlPath);
		if (f.open(QIODevice::ReadOnly))
			cfg->html_template = QString::fromUtf8(f.readAll()).toStdString();
	}
	{
		QFile f(cssPath);
		if (f.open(QIODevice::ReadOnly))
			cfg->css_template = QString::fromUtf8(f.readAll()).toStdString();
	}
	{
		if (!jsPath.isEmpty()) {
			QFile f(jsPath);
			if (f.open(QIODevice::ReadOnly))
				cfg->js_template = QString::fromUtf8(f.readAll()).toStdString();
			else
				cfg->js_template.clear();
		} else {
			cfg->js_template.clear();
		}
	}

	// Import profile picture into output dir
	if (!profilePicPath.isEmpty() && smart_lt::has_output_dir()) {
		const QString outDir = QString::fromStdString(smart_lt::output_dir());
		if (!outDir.isEmpty()) {
			QDir dir(outDir);

			if (!cfg->profile_picture.empty()) {
				const QString oldPath = dir.filePath(QString::fromStdString(cfg->profile_picture));
				if (QFile::exists(oldPath))
					QFile::remove(oldPath);
			}

			const QString ext = QFileInfo(profilePicPath).suffix().toLower();
			const QString newName =
				ext.isEmpty() ? QString("%1_profile").arg(QString::fromStdString(cfg->id))
					      : QString("%1_profile.%2").arg(QString::fromStdString(cfg->id)).arg(ext);

			const QString dest = dir.filePath(newName);
			QFile::remove(dest);

			if (QFile::copy(profilePicPath, dest)) {
				cfg->profile_picture = newName.toStdString();
			}
		}
	}

	smart_lt::save_state_json();

	// Import changes should not immediately rebuild unless user clicks Save&Apply.
	// But user expectation for import is "applies to editor" now; so we refresh UI fields.
	loadFromState();

	QMessageBox::information(this, tr("Imported"),
				 tr("Template imported successfully. Click 'Save & Apply' to rebuild files."));
}

} // namespace smart_lt::ui
