#define LOG_TAG "[" PLUGIN_NAME "][settings]"
#include "settings.hpp"
#include "core.hpp"

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
#include <QColor>
#include <QFontComboBox>
#include <QFont>
#include <QKeySequenceEdit>
#include <QKeySequence>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QStyle>
#include <QSignalBlocker>

// ctor/dtor
LowerThirdSettingsDialog::LowerThirdSettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(tr("Lower Third Settings"));
	resize(720, 620);

	auto *root = new QVBoxLayout(this);

	// Content box
	auto *contentBox = new QGroupBox(tr("Content && Media"), this);
	auto *contentLayout = new QGridLayout(contentBox);

	auto *titleLabel = new QLabel(tr("Title:"), this);
	titleEdit = new QLineEdit(this);

	auto *subtitleLabel = new QLabel(tr("Subtitle:"), this);
	subtitleEdit = new QLineEdit(this);

	auto *picLabel = new QLabel(tr("Profile picture:"), this);
	auto *picRow = new QHBoxLayout();
	profilePictureEdit = new QLineEdit(this);
	profilePictureEdit->setReadOnly(true);

	browseProfilePictureBtn = new QPushButton(this);
	browseProfilePictureBtn->setCursor(Qt::PointingHandCursor);
	browseProfilePictureBtn->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
	browseProfilePictureBtn->setToolTip(tr("Browse profile picture..."));

	picRow->addWidget(profilePictureEdit, 1);
	picRow->addWidget(browseProfilePictureBtn);

	int row = 0;
	contentLayout->addWidget(titleLabel, row, 0);
	contentLayout->addWidget(titleEdit, row, 1, 1, 3);

	row++;
	contentLayout->addWidget(subtitleLabel, row, 0);
	contentLayout->addWidget(subtitleEdit, row, 1, 1, 3);

	row++;
	contentLayout->addWidget(picLabel, row, 0);
	contentLayout->addLayout(picRow, row, 1, 1, 3);

	contentLayout->setColumnStretch(1, 1);
	root->addWidget(contentBox);

	// Style box
	auto *styleBox = new QGroupBox(tr("Style"), this);
	auto *styleGrid = new QGridLayout(styleBox);

	auto *lblIn = new QLabel(tr("Anim In:"), this);
	animInCombo = new QComboBox(this);
	for (const auto &opt : AnimInOptions)
		animInCombo->addItem(tr(opt.label), QString::fromUtf8(opt.value));

	auto *lblOut = new QLabel(tr("Anim Out:"), this);
	animOutCombo = new QComboBox(this);
	for (const auto &opt : AnimOutOptions)
		animOutCombo->addItem(tr(opt.label), QString::fromUtf8(opt.value));

	auto *lblFont = new QLabel(tr("Font:"), this);
	fontCombo = new QFontComboBox(this);
	fontCombo->setEditable(false);

	row = 0;
	styleGrid->addWidget(lblIn, row, 0);
	styleGrid->addWidget(animInCombo, row, 1);
	styleGrid->addWidget(lblOut, row, 2);
	styleGrid->addWidget(animOutCombo, row, 3);

	row++;
	styleGrid->addWidget(lblFont, row, 0);
	styleGrid->addWidget(fontCombo, row, 1);

	auto *lblPos = new QLabel(tr("Position:"), this);
	ltPosCombo = new QComboBox(this);
	for (const auto &opt : LtPositionOptions)
		ltPosCombo->addItem(tr(opt.label), QString::fromUtf8(opt.value));

	styleGrid->addWidget(lblPos, row, 2);
	styleGrid->addWidget(ltPosCombo, row, 3);

	row++;
	customAnimInLabel = new QLabel(tr("Custom In class:"), this);
	customAnimInEdit = new QLineEdit(this);
	customAnimInEdit->setPlaceholderText(tr("e.g. myFadeIn"));

	customAnimOutLabel = new QLabel(tr("Custom Out class:"), this);
	customAnimOutEdit = new QLineEdit(this);
	customAnimOutEdit->setPlaceholderText(tr("e.g. myFadeOut"));

	styleGrid->addWidget(customAnimInLabel, row, 0);
	styleGrid->addWidget(customAnimInEdit, row, 1);
	styleGrid->addWidget(customAnimOutLabel, row, 2);
	styleGrid->addWidget(customAnimOutEdit, row, 3);

	row++;
	auto *lblBg = new QLabel(tr("Background:"), this);
	bgColorBtn = new QPushButton(tr("Pick"), this);

	auto *lblText = new QLabel(tr("Text color:"), this);
	textColorBtn = new QPushButton(tr("Pick"), this);

	styleGrid->addWidget(lblBg, row, 0);
	styleGrid->addWidget(bgColorBtn, row, 1);
	styleGrid->addWidget(lblText, row, 2);
	styleGrid->addWidget(textColorBtn, row, 3);

	root->addWidget(styleBox);

	connect(bgColorBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onPickBgColor);
	connect(textColorBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onPickTextColor);
	connect(ltPosCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&LowerThirdSettingsDialog::onLtPosChanged);

	// Behavior box (scene binding removed)
	auto *behaviorBox = new QGroupBox(tr("Behavior"), this);
	auto *behaviorGrid = new QGridLayout(behaviorBox);

	auto *hotkeyLabel = new QLabel(tr("Hotkey:"), this);
	hotkeyEdit = new QKeySequenceEdit(this);

	clearHotkeyBtn = new QPushButton(this);
	clearHotkeyBtn->setCursor(Qt::PointingHandCursor);
	clearHotkeyBtn->setFlat(true);
	clearHotkeyBtn->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
	clearHotkeyBtn->setToolTip(tr("Clear hotkey"));
	clearHotkeyBtn->setFocusPolicy(Qt::NoFocus);

	auto *hotkeyRowLayout = new QHBoxLayout();
	hotkeyRowLayout->addWidget(hotkeyEdit, 1);
	hotkeyRowLayout->addWidget(clearHotkeyBtn);

	behaviorGrid->addWidget(hotkeyLabel, 0, 0);
	behaviorGrid->addLayout(hotkeyRowLayout, 0, 1);

	root->addWidget(behaviorBox);

	connect(clearHotkeyBtn, &QPushButton::clicked, this, [this]() {
		hotkeyEdit->setKeySequence(QKeySequence());
		onHotkeyChanged(hotkeyEdit->keySequence());
	});

	// HTML/CSS editors
	auto *tplRow = new QHBoxLayout();

	auto *htmlCard = new QGroupBox(tr("HTML Template"), this);
	auto *htmlLayout = new QVBoxLayout(htmlCard);
	htmlEdit = new QPlainTextEdit(this);
	htmlLayout->addWidget(htmlEdit, 1);
	tplRow->addWidget(htmlCard, 1);

	auto *cssCard = new QGroupBox(tr("CSS Template"), this);
	auto *cssLayout = new QVBoxLayout(cssCard);
	cssEdit = new QPlainTextEdit(this);
	cssLayout->addWidget(cssEdit, 1);
	tplRow->addWidget(cssCard, 1);

	root->addLayout(tplRow, 1);

	// bottom
	buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
	auto *applyBtn = buttonBox->addButton(tr("Save && Apply"), QDialogButtonBox::AcceptRole);

	connect(buttonBox, &QDialogButtonBox::accepted, this, &LowerThirdSettingsDialog::onSaveAndApply);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &LowerThirdSettingsDialog::reject);
	connect(applyBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onSaveAndApply);

	root->addWidget(buttonBox);

	// bindings
	connect(animInCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&LowerThirdSettingsDialog::onAnimInChanged);
	connect(animOutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&LowerThirdSettingsDialog::onAnimOutChanged);
	connect(customAnimInEdit, &QLineEdit::textChanged, this,
		[this](const QString &) { updateCustomAnimFieldsVisibility(); });
	connect(customAnimOutEdit, &QLineEdit::textChanged, this,
		[this](const QString &) { updateCustomAnimFieldsVisibility(); });
	connect(fontCombo, &QFontComboBox::currentFontChanged, this, &LowerThirdSettingsDialog::onFontChanged);
	connect(hotkeyEdit, &QKeySequenceEdit::keySequenceChanged, this, &LowerThirdSettingsDialog::onHotkeyChanged);
	connect(browseProfilePictureBtn, &QPushButton::clicked, this,
		&LowerThirdSettingsDialog::onBrowseProfilePicture);

	updateCustomAnimFieldsVisibility();
}

LowerThirdSettingsDialog::~LowerThirdSettingsDialog()
{
	delete currentBgColor;
	delete currentTextColor;
}

// load/save
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

	{
		const QString v = QString::fromStdString(cfg->anim_in);
		const int idx = animInCombo->findData(v);
		animInCombo->setCurrentIndex(idx >= 0 ? idx : 0);
	}
	{
		const QString v = QString::fromStdString(cfg->anim_out);
		const int idx = animOutCombo->findData(v);
		animOutCombo->setCurrentIndex(idx >= 0 ? idx : 0);
	}

	customAnimInEdit->setText(QString::fromStdString(cfg->custom_anim_in));
	customAnimOutEdit->setText(QString::fromStdString(cfg->custom_anim_out));
	updateCustomAnimFieldsVisibility();

	if (!cfg->font_family.empty())
		fontCombo->setCurrentFont(QFont(QString::fromStdString(cfg->font_family)));

	// hotkey (portable text)
	hotkeyEdit->setKeySequence(QKeySequence(QString::fromStdString(cfg->hotkey)));

	htmlEdit->setPlainText(QString::fromStdString(cfg->html_template));
	cssEdit->setPlainText(QString::fromStdString(cfg->css_template));

	delete currentBgColor;
	delete currentTextColor;
	currentBgColor = nullptr;
	currentTextColor = nullptr;

	QColor bg(QString::fromStdString(cfg->bg_color));
	QColor fg(QString::fromStdString(cfg->text_color));
	if (!bg.isValid())
		bg = QColor(17, 24, 39);
	if (!fg.isValid())
		fg = QColor(249, 250, 251);

	currentBgColor = new QColor(bg);
	currentTextColor = new QColor(fg);

	updateColorButton(bgColorBtn, *currentBgColor);
	updateColorButton(textColorBtn, *currentTextColor);

	{
		const QString v = QString::fromStdString(cfg->lt_position);
		const int idx = ltPosCombo->findData(v);
		ltPosCombo->setCurrentIndex(idx >= 0 ? idx : 0);
	}

	if (cfg->profile_picture.empty())
		profilePictureEdit->clear();
	else
		profilePictureEdit->setText(QString::fromStdString(cfg->profile_picture));

	pendingProfilePicturePath.clear();
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
	cfg->hotkey = hotkeyEdit->keySequence().toString(QKeySequence::PortableText).toStdString();

	cfg->lt_position = ltPosCombo->currentData().toString().toStdString();

	if (currentBgColor)
		cfg->bg_color = currentBgColor->name(QColor::HexRgb).toStdString();
	if (currentTextColor)
		cfg->text_color = currentTextColor->name(QColor::HexRgb).toStdString();

	cfg->html_template = htmlEdit->toPlainText().toStdString();
	cfg->css_template = cssEdit->toPlainText().toStdString();

	// profile picture copy into output_dir (Major because it affects rendered HTML image)
	if (!pendingProfilePicturePath.isEmpty() && smart_lt::has_output_dir()) {
		const QString outDir = QString::fromStdString(smart_lt::output_dir());
		QDir dir(outDir);

		// remove old
		if (!cfg->profile_picture.empty()) {
			const QString oldPath = dir.filePath(QString::fromStdString(cfg->profile_picture));
			if (QFile::exists(oldPath))
				QFile::remove(oldPath);
		}

		const QFileInfo fi(pendingProfilePicturePath);
		const QString ext = fi.suffix().toLower();
		const qint64 ts = QDateTime::currentSecsSinceEpoch();

		QString newFileName = QString("%1_%2").arg(QString::fromStdString(cfg->id)).arg(ts);
		if (!ext.isEmpty())
			newFileName += "." + ext;

		const QString destPath = dir.filePath(newFileName);

		if (QFile::copy(pendingProfilePicturePath, destPath)) {
			cfg->profile_picture = newFileName.toStdString();
			profilePictureEdit->setText(newFileName);
		} else {
			LOGW("Failed to copy profile picture '%s' -> '%s'",
			     pendingProfilePicturePath.toUtf8().constData(), destPath.toUtf8().constData());
		}

		pendingProfilePicturePath.clear();
	}

	// Major changes: template/style/position/media -> rev++ + html rewrite + json save
	smart_lt::apply_changes(smart_lt::ApplyMode::HtmlAndJsonRev);
}

// actions
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

void LowerThirdSettingsDialog::onBrowseProfilePicture()
{
	const QString filter = tr("Images (*.png *.jpg *.jpeg *.webp *.gif);;All Files (*.*)");
	const QString file = QFileDialog::getOpenFileName(this, tr("Select profile picture"), QString(), filter);
	if (file.isEmpty())
		return;

	pendingProfilePicturePath = file;
	profilePictureEdit->setText(file);
}

void LowerThirdSettingsDialog::onSaveAndApply()
{
	saveToState();
	accept();
}

// change handlers
void LowerThirdSettingsDialog::updateCustomAnimFieldsVisibility()
{
	const QString customKey = QStringLiteral("custom");

	const bool inCustom = (animInCombo->currentData().toString() == customKey);
	const bool outCustom = (animOutCombo->currentData().toString() == customKey);

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

void LowerThirdSettingsDialog::onFontChanged(const QFont &)
{
	// no-op here; persisted on Save
}

void LowerThirdSettingsDialog::onHotkeyChanged(const QKeySequence &)
{
	// no-op here; persisted on Save
}

void LowerThirdSettingsDialog::onLtPosChanged(int)
{
	// Position impacts rendered HTML class usage -> Major
	if (currentId.isEmpty())
		return;

	if (auto *cfg = smart_lt::get_by_id(currentId.toStdString())) {
		cfg->lt_position = ltPosCombo->currentData().toString().toStdString();
	}

	smart_lt::apply_changes(smart_lt::ApplyMode::HtmlAndJsonRev);
}

void LowerThirdSettingsDialog::updateColorButton(QPushButton *btn, const QColor &color)
{
	const QString hex = color.name(QColor::HexRgb);
	btn->setStyleSheet(
		QString("background-color:%1;border:1px solid #333;min-width:64px;padding:2px 4px;").arg(hex));
	btn->setText(hex);
}
