#include "settings.hpp"
#include "state.hpp"
#include "entities.hpp"
#include "log.hpp"
#include "slt_helpers.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <unzip.h>
#include <zip.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
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
#include <QTemporaryDir>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStyle>

using namespace smart_lt;

static bool unzip_to_dir(const QString &zipPath, const QString &destDir, QString &htmlPath, QString &cssPath,
			 QString &jsonPath, QString &profilePath)
{
	htmlPath.clear();
	cssPath.clear();
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
		char filename[512];
		unz_file_info info;

		if (unzGetCurrentFileInfo(zip, &info, filename, sizeof(filename), nullptr, 0, nullptr, 0) != UNZ_OK)
			break;

		QString name = QString::fromUtf8(filename);
		QString outPath = destDir + "/" + name;

		QFileInfo fi(outPath);
		QDir().mkpath(fi.path());

		if (unzOpenCurrentFile(zip) != UNZ_OK)
			break;

		QFile out(outPath);
		if (!out.open(QIODevice::WriteOnly))
			break;

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
		else if (name == "template.json")
			jsonPath = outPath;

		else if (name.startsWith("profile.") || name.contains("profile", Qt::CaseInsensitive))
			profilePath = outPath;

	} while (unzGoToNextFile(zip) == UNZ_OK);

	unzClose(zip);

	return (!htmlPath.isEmpty() && !cssPath.isEmpty() && !jsonPath.isEmpty());
}

void LowerThirdSettingsDialog::onExportTemplateClicked()
{
	if (currentId.isEmpty()) {
		QMessageBox::warning(this, tr("Export"), tr("No lower third selected to export."));
		return;
	}

	saveToState();

	auto *cfg = get_by_id(currentId.toStdString());
	if (!cfg) {
		QMessageBox::warning(this, tr("Export"), tr("Cannot find current lower third configuration."));
		return;
	}

	QString suggestedName = QString::fromStdString(cfg->id) + "-template.zip";
	QString zipPath =
		QFileDialog::getSaveFileName(this, tr("Export Template"), suggestedName, tr("Template ZIP (*.zip)"));

	if (zipPath.isEmpty())
		return;

	zipFile zf = zipOpen(zipPath.toUtf8().constData(), 0);
	if (!zf) {
		QMessageBox::warning(this, tr("Export"), tr("Failed to create ZIP file."));
		return;
	}

	auto writeFileToZip = [&](const char *internalName, const QByteArray &data) -> bool {
		zip_fileinfo zi;
		memset(&zi, 0, sizeof(zi));

		int err = zipOpenNewFileInZip(zf, internalName, &zi, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED,
					      Z_BEST_COMPRESSION);
		if (err != ZIP_OK)
			return false;

		if (!data.isEmpty()) {
			err = zipWriteInFileInZip(zf, data.constData(), data.size());
			if (err != ZIP_OK) {
				zipCloseFileInZip(zf);
				return false;
			}
		}

		zipCloseFileInZip(zf);
		return true;
	};

	QJsonObject obj;
	obj["id"] = QString::fromStdString(cfg->id);
	obj["title"] = QString::fromStdString(cfg->title);
	obj["subtitle"] = QString::fromStdString(cfg->subtitle);
	obj["anim_in"] = QString::fromStdString(cfg->anim_in);
	obj["anim_out"] = QString::fromStdString(cfg->anim_out);
	obj["custom_anim_in"] = QString::fromStdString(cfg->custom_anim_in);
	obj["custom_anim_out"] = QString::fromStdString(cfg->custom_anim_out);
	obj["font_family"] = QString::fromStdString(cfg->font_family);
	obj["bg_color"] = QString::fromStdString(cfg->bg_color);
	obj["text_color"] = QString::fromStdString(cfg->text_color);
	obj["bound_scene"] = QString::fromStdString(cfg->bound_scene);
	obj["visible"] = cfg->visible;
	obj["hotkey"] = QString::fromStdString(cfg->hotkey);
	obj["profile_picture"] = QString::fromStdString(cfg->profile_picture);

	QJsonDocument jsonDoc(obj);
	QByteArray jsonBytes = jsonDoc.toJson(QJsonDocument::Indented);

	if (!writeFileToZip("template.json", jsonBytes)) {
		zipClose(zf, nullptr);
		QMessageBox::warning(this, tr("Export"), tr("Failed to write template.json to ZIP."));
		return;
	}

	QByteArray htmlBytes = QString::fromStdString(cfg->html_template).toUtf8();
	if (!writeFileToZip("template.html", htmlBytes)) {
		zipClose(zf, nullptr);
		QMessageBox::warning(this, tr("Export"), tr("Failed to write template.html to ZIP."));
		return;
	}

	QByteArray cssBytes = QString::fromStdString(cfg->css_template).toUtf8();
	if (!writeFileToZip("template.css", cssBytes)) {
		zipClose(zf, nullptr);
		QMessageBox::warning(this, tr("Export"), tr("Failed to write template.css to ZIP."));
		return;
	}

	if (!cfg->profile_picture.empty()) {
		QString outDir = QString::fromStdString(output_dir());
		if (!outDir.isEmpty()) {
			QDir dir(outDir);
			QString picFileName = QString::fromStdString(cfg->profile_picture);
			QString srcPath = dir.filePath(picFileName);

			if (QFile::exists(srcPath)) {
				QFile picFile(srcPath);
				if (picFile.open(QIODevice::ReadOnly)) {
					QByteArray picBytes = picFile.readAll();

					QString ext = QFileInfo(picFileName).suffix();
					QString internalName = ext.isEmpty() ? QStringLiteral("profile")
									     : QStringLiteral("profile.%1").arg(ext);

					if (!writeFileToZip(internalName.toUtf8().constData(), picBytes)) {
						zipClose(zf, nullptr);
						QMessageBox::warning(this, tr("Export"),
								     tr("Failed to add profile picture to ZIP."));
						return;
					}
				}
			}
		}
	}

	zipClose(zf, nullptr);

	QMessageBox::information(this, tr("Export"), tr("Template exported successfully."));
}

LowerThirdSettingsDialog::LowerThirdSettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(tr("Lower Third Settings"));
	resize(720, 620);

	auto *root = new QVBoxLayout(this);

	auto *contentBox = new QGroupBox(tr("Content && Media"), this);
	auto *contentLayout = new QFormLayout(contentBox);

	titleEdit = new QLineEdit(this);
	contentLayout->addRow(tr("Title:"), titleEdit);

	subtitleEdit = new QLineEdit(this);
	contentLayout->addRow(tr("Subtitle:"), subtitleEdit);

	{
		auto *picRow = new QHBoxLayout();
		profilePictureEdit = new QLineEdit(this);
		profilePictureEdit->setReadOnly(true);

		browseProfilePictureBtn = new QPushButton(this);
		browseProfilePictureBtn->setCursor(Qt::PointingHandCursor);
		browseProfilePictureBtn->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
		browseProfilePictureBtn->setToolTip(tr("Browse profile picture..."));

		picRow->addWidget(profilePictureEdit, 1);
		picRow->addWidget(browseProfilePictureBtn);

		contentLayout->addRow(tr("Profile picture:"), picRow);
	}

	root->addWidget(contentBox);

	auto *styleBox = new QGroupBox(tr("Style"), this);
	auto *styleLayout = new QVBoxLayout(styleBox);

	{
		auto *row = new QHBoxLayout();

		auto *lblIn = new QLabel(tr("Anim In:"), this);
		animInCombo = new QComboBox(this);
		for (const auto &opt : smart_lt::AnimInOptions) {
			animInCombo->addItem(tr(opt.label), QString::fromUtf8(opt.value));
		}

		auto *lblOut = new QLabel(tr("Anim Out:"), this);
		animOutCombo = new QComboBox(this);
		for (const auto &opt : smart_lt::AnimOutOptions) {
			animOutCombo->addItem(tr(opt.label), QString::fromUtf8(opt.value));
		}

		auto *lblFont = new QLabel(tr("Font:"), this);
		fontCombo = new QFontComboBox(this);
		fontCombo->setEditable(false);

		row->addWidget(lblIn);
		row->addWidget(animInCombo);
		row->addSpacing(8);
		row->addWidget(lblOut);
		row->addWidget(animOutCombo);
		row->addSpacing(8);
		row->addWidget(lblFont);
		row->addWidget(fontCombo, 1);

		styleLayout->addLayout(row);
	}

	{
		auto *row = new QHBoxLayout();

		customAnimInLabel = new QLabel(tr("Custom In class:"), this);
		customAnimInEdit = new QLineEdit(this);
		customAnimInEdit->setPlaceholderText(tr("e.g. myFadeIn"));

		customAnimOutLabel = new QLabel(tr("Custom Out class:"), this);
		customAnimOutEdit = new QLineEdit(this);
		customAnimOutEdit->setPlaceholderText(tr("e.g. myFadeOut"));

		row->addWidget(customAnimInLabel);
		row->addWidget(customAnimInEdit);
		row->addSpacing(8);
		row->addWidget(customAnimOutLabel);
		row->addWidget(customAnimOutEdit);

		styleLayout->addLayout(row);
	}

	{
		auto *row = new QHBoxLayout();
		auto *lblBg = new QLabel(tr("Background:"), this);
		bgColorBtn = new QPushButton(tr("Pick"), this);
		auto *lblText = new QLabel(tr("Text color:"), this);
		textColorBtn = new QPushButton(tr("Pick"), this);

		row->addWidget(lblBg);
		row->addWidget(bgColorBtn);
		row->addSpacing(12);
		row->addWidget(lblText);
		row->addWidget(textColorBtn);
		row->addStretch(1);

		styleLayout->addLayout(row);

		connect(bgColorBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onPickBgColor);
		connect(textColorBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onPickTextColor);
	}

	root->addWidget(styleBox);

	auto *behaviorBox = new QGroupBox(tr("Behavior"), this);
	auto *behaviorLayout = new QFormLayout(behaviorBox);

	hotkeyEdit = new QKeySequenceEdit(this);
	behaviorLayout->addRow(tr("Hotkey:"), hotkeyEdit);

	sceneCombo = new QComboBox(this);
	behaviorLayout->addRow(tr("Bind to scene:"), sceneCombo);

	root->addWidget(behaviorBox);

	connect(sceneCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&LowerThirdSettingsDialog::onSceneBindingChanged);

	auto *tplBox = new QGroupBox(tr("Templates"), this);
	auto *tplLayout = new QVBoxLayout(tplBox);

	{
		auto *row = new QHBoxLayout();
		auto *lbl = new QLabel(tr("HTML Template:"), this);
		auto *expandHtmlBtn = new QPushButton(this);
		expandHtmlBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
		expandHtmlBtn->setCursor(Qt::PointingHandCursor);
		expandHtmlBtn->setToolTip(tr("Open HTML editor..."));
		expandHtmlBtn->setFlat(true);

		row->addWidget(lbl);
		row->addStretch(1);
		row->addWidget(expandHtmlBtn);

		tplLayout->addLayout(row);

		htmlEdit = new QPlainTextEdit(this);
		htmlEdit->setPlaceholderText(
			"<li id=\"{{ID}}\" class=\"lower-third animate__animated {{ANIM_IN}}\">\n"
			"  <div class=\"lt-inner\">\n"
			"    <!-- Optional avatar: <img class=\"lt-avatar\" src=\"{{PROFILE_PICTURE}}\" /> -->\n"
			"    <div class=\"lt-title\">{{TITLE}}</div>\n"
			"    <div class=\"lt-subtitle\">{{SUBTITLE}}</div>\n"
			"  </div>\n"
			"</li>\n");

		tplLayout->addWidget(htmlEdit, 1);

		connect(expandHtmlBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onOpenHtmlEditorDialog);
	}

	{
		auto *row = new QHBoxLayout();
		auto *lbl = new QLabel(tr("CSS Template:"), this);
		auto *expandCssBtn = new QPushButton(this);
		expandCssBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
		expandCssBtn->setCursor(Qt::PointingHandCursor);
		expandCssBtn->setToolTip(tr("Open CSS editor..."));
		expandCssBtn->setFlat(true);

		row->addWidget(lbl);
		row->addStretch(1);
		row->addWidget(expandCssBtn);

		tplLayout->addLayout(row);

		cssEdit = new QPlainTextEdit(this);
		cssEdit->setPlaceholderText("#{{ID}} .lt-inner {\n"
					    "  font-family: {{FONT_FAMILY}}, sans-serif;\n"
					    "  background: {{BG_COLOR}};\n"
					    "  color: {{TEXT_COLOR}};\n"
					    "}\n"
					    "#{{ID}} .lt-avatar {\n"
					    "  width: 56px;\n"
					    "  height: 56px;\n"
					    "  border-radius: 50%;\n"
					    "  margin-right: 12px;\n"
					    "}\n");

		tplLayout->addWidget(cssEdit, 1);

		connect(expandCssBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onOpenCssEditorDialog);
	}

	root->addWidget(tplBox, 1);

	buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
	auto *applyBtn = buttonBox->addButton(tr("Save && Apply"), QDialogButtonBox::AcceptRole);

	connect(buttonBox, &QDialogButtonBox::accepted, this, &LowerThirdSettingsDialog::onSaveAndApply);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &LowerThirdSettingsDialog::reject);
	connect(applyBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onSaveAndApply);

	auto *bottomRow = new QHBoxLayout();

	auto *importBtn = new QPushButton(this);
	importBtn->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
	importBtn->setCursor(Qt::PointingHandCursor);
	importBtn->setToolTip(tr("Import template from ZIP..."));

	auto *exportBtn = new QPushButton(this);
	exportBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
	exportBtn->setCursor(Qt::PointingHandCursor);
	exportBtn->setToolTip(tr("Export template to ZIP..."));

	bottomRow->addWidget(importBtn);
	bottomRow->addWidget(exportBtn);
	bottomRow->addStretch(1);
	bottomRow->addWidget(buttonBox);

	root->addLayout(bottomRow);

	connect(importBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onImportTemplateClicked);
	connect(exportBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onExportTemplateClicked);

	connect(titleEdit, &QLineEdit::textChanged, this, &LowerThirdSettingsDialog::onTitleChanged);
	connect(subtitleEdit, &QLineEdit::textChanged, this, &LowerThirdSettingsDialog::onSubtitleChanged);
	connect(animInCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&LowerThirdSettingsDialog::onAnimInChanged);
	connect(animOutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&LowerThirdSettingsDialog::onAnimOutChanged);

	connect(customAnimInEdit, &QLineEdit::textChanged, this, &LowerThirdSettingsDialog::onCustomAnimInChanged);
	connect(customAnimOutEdit, &QLineEdit::textChanged, this, &LowerThirdSettingsDialog::onCustomAnimOutChanged);

	updateCustomAnimFieldsVisibility();

	connect(fontCombo, &QFontComboBox::currentFontChanged, this, &LowerThirdSettingsDialog::onFontChanged);
	connect(hotkeyEdit, &QKeySequenceEdit::keySequenceChanged, this, &LowerThirdSettingsDialog::onHotkeyChanged);
	connect(browseProfilePictureBtn, &QPushButton::clicked, this,
		&LowerThirdSettingsDialog::onBrowseProfilePicture);
}

void LowerThirdSettingsDialog::onSceneBindingChanged(int)
{
	if (currentId.isEmpty())
		return;

	if (auto *cfg = get_by_id(currentId.toStdString())) {
		const QString data = sceneCombo->currentData().toString();
		cfg->bound_scene = data.toStdString();
	}

	smart_lt::save_state_json();
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

	LowerThirdConfig *cfg = get_by_id(currentId.toStdString());
	if (!cfg)
		return;

	titleEdit->setText(QString::fromStdString(cfg->title));
	subtitleEdit->setText(QString::fromStdString(cfg->subtitle));

	{
		const QString valueIn = QString::fromStdString(cfg->anim_in);
		int idxIn = -1;
		for (int i = 0; i < animInCombo->count(); ++i) {
			if (animInCombo->itemData(i).toString() == valueIn) {
				idxIn = i;
				break;
			}
		}
		if (idxIn >= 0)
			animInCombo->setCurrentIndex(idxIn);
	}

	{
		const QString valueOut = QString::fromStdString(cfg->anim_out);
		int idxOut = -1;
		for (int i = 0; i < animOutCombo->count(); ++i) {
			if (animOutCombo->itemData(i).toString() == valueOut) {
				idxOut = i;
				break;
			}
		}
		if (idxOut >= 0)
			animOutCombo->setCurrentIndex(idxOut);
	}

	customAnimInEdit->clear();
	customAnimOutEdit->clear();

	if (!cfg->custom_anim_in.empty())
		customAnimInEdit->setText(QString::fromStdString(cfg->custom_anim_in));
	if (!cfg->custom_anim_out.empty())
		customAnimOutEdit->setText(QString::fromStdString(cfg->custom_anim_out));

	updateCustomAnimFieldsVisibility();

	if (!cfg->font_family.empty())
		fontCombo->setCurrentFont(QFont(QString::fromStdString(cfg->font_family)));

	if (!cfg->hotkey.empty())
		hotkeyEdit->setKeySequence(QKeySequence(QString::fromStdString(cfg->hotkey)));

	htmlEdit->setPlainText(QString::fromStdString(cfg->html_template));
	cssEdit->setPlainText(QString::fromStdString(cfg->css_template));

	delete currentBgColor;
	delete currentTextColor;

	QColor bg(cfg->bg_color.c_str());
	QColor fg(cfg->text_color.c_str());

	if (!bg.isValid())
		bg = QColor(0, 0, 0, 200);
	if (!fg.isValid())
		fg = QColor(255, 255, 255);

	currentBgColor = new QColor(bg);
	currentTextColor = new QColor(fg);

	updateColorButton(bgColorBtn, *currentBgColor);
	updateColorButton(textColorBtn, *currentTextColor);

	if (cfg->profile_picture.empty()) {
		profilePictureEdit->clear();
	} else {
		profilePictureEdit->setText(QString::fromStdString(cfg->profile_picture));
	}

	if (sceneCombo)
		populate_scene_combo(sceneCombo, cfg->bound_scene);

	pendingProfilePicturePath.clear();
}

void LowerThirdSettingsDialog::saveToState()
{
	if (currentId.isEmpty())
		return;

	LowerThirdConfig *cfg = get_by_id(currentId.toStdString());
	if (!cfg)
		return;

	cfg->title = titleEdit->text().toStdString();
	cfg->subtitle = subtitleEdit->text().toStdString();

	cfg->anim_in = animInCombo->currentData().toString().toStdString();
	cfg->anim_out = animOutCombo->currentData().toString().toStdString();

	cfg->custom_anim_in = customAnimInEdit->text().toStdString();
	cfg->custom_anim_out = customAnimOutEdit->text().toStdString();

	cfg->font_family = fontCombo->currentFont().family().toStdString();

	if (currentBgColor)
		cfg->bg_color = currentBgColor->name(QColor::HexRgb).toStdString();
	if (currentTextColor)
		cfg->text_color = currentTextColor->name(QColor::HexRgb).toStdString();

	cfg->html_template = htmlEdit->toPlainText().toStdString();
	cfg->css_template = cssEdit->toPlainText().toStdString();
	cfg->hotkey = hotkeyEdit->keySequence().toString(QKeySequence::PortableText).toStdString();

	if (sceneCombo) {
		const QString data = sceneCombo->currentData().toString();
		cfg->bound_scene = data.toStdString();
	}

	if (!pendingProfilePicturePath.isEmpty()) {
		QString outDir = QString::fromStdString(smart_lt::output_dir());
		if (!outDir.isEmpty()) {
			QDir dir(outDir);

			if (!cfg->profile_picture.empty()) {
				QString oldFilePath = dir.filePath(QString::fromStdString(cfg->profile_picture));
				if (QFile::exists(oldFilePath)) {
					QFile::remove(oldFilePath);
				}
			}

			QFileInfo fi(pendingProfilePicturePath);
			QString ext = fi.suffix();
			qint64 ts = QDateTime::currentSecsSinceEpoch();

			QString baseId = QString::fromStdString(cfg->id);
			QString newFileName = QString("%1_%2").arg(baseId).arg(ts);
			if (!ext.isEmpty())
				newFileName += "." + ext.toLower();

			QString destPath = dir.filePath(newFileName);

			if (QFile::copy(pendingProfilePicturePath, destPath)) {
				cfg->profile_picture = newFileName.toStdString();
				profilePictureEdit->setText(newFileName);
			} else {
				LOGW("Failed to copy profile picture from '%s' to '%s'",
				     pendingProfilePicturePath.toUtf8().constData(), destPath.toUtf8().constData());
			}
		} else {
			LOGW("No output_dir set when saving profile picture for '%s'", cfg->id.c_str());
		}

		pendingProfilePicturePath.clear();
	}

	write_index_html();
	ensure_browser_source();
	refresh_browser_source();
	save_state_json();
}

void LowerThirdSettingsDialog::onPickBgColor()
{
	QColor start = currentBgColor ? *currentBgColor : QColor(0, 0, 0, 200);
	QColor c = QColorDialog::getColor(start, this, tr("Pick background color"), QColorDialog::ShowAlphaChannel);
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
	QColor start = currentTextColor ? *currentTextColor : QColor(255, 255, 255);
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
	QString filter = tr("Images (*.png *.jpg *.jpeg *.webp *.gif);;All Files (*.*)");
	QString file = QFileDialog::getOpenFileName(this, tr("Select profile picture"), QString(), filter);
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

void LowerThirdSettingsDialog::onTitleChanged(const QString &text)
{
	if (currentId.isEmpty())
		return;
	if (auto *cfg = get_by_id(currentId.toStdString())) {
		cfg->title = text.toStdString();
	}
}

void LowerThirdSettingsDialog::onSubtitleChanged(const QString &text)
{
	if (currentId.isEmpty())
		return;
	if (auto *cfg = get_by_id(currentId.toStdString())) {
		cfg->subtitle = text.toStdString();
	}
}

void LowerThirdSettingsDialog::updateCustomAnimFieldsVisibility()
{
	const QString customKey = QStringLiteral("custom");

	bool inCustom = (animInCombo->currentData().toString() == customKey);
	bool outCustom = (animOutCombo->currentData().toString() == customKey);

	if (customAnimInLabel)
		customAnimInLabel->setVisible(inCustom);
	if (customAnimInEdit)
		customAnimInEdit->setVisible(inCustom);
	if (customAnimOutLabel)
		customAnimOutLabel->setVisible(outCustom);
	if (customAnimOutEdit)
		customAnimOutEdit->setVisible(outCustom);
}

void LowerThirdSettingsDialog::onAnimInChanged(int)
{
	if (currentId.isEmpty())
		return;
	if (auto *cfg = get_by_id(currentId.toStdString())) {
		cfg->anim_in = animInCombo->currentData().toString().toStdString();
	}
	updateCustomAnimFieldsVisibility();
}

void LowerThirdSettingsDialog::onAnimOutChanged(int)
{
	if (currentId.isEmpty())
		return;
	if (auto *cfg = get_by_id(currentId.toStdString())) {
		cfg->anim_out = animOutCombo->currentData().toString().toStdString();
	}
	updateCustomAnimFieldsVisibility();
}

void LowerThirdSettingsDialog::onCustomAnimInChanged(const QString &text)
{
	if (currentId.isEmpty())
		return;
	if (auto *cfg = get_by_id(currentId.toStdString())) {
		cfg->custom_anim_in = text.toStdString();
	}
}

void LowerThirdSettingsDialog::onCustomAnimOutChanged(const QString &text)
{
	if (currentId.isEmpty())
		return;
	if (auto *cfg = get_by_id(currentId.toStdString())) {
		cfg->custom_anim_out = text.toStdString();
	}
}

void LowerThirdSettingsDialog::onFontChanged(const QFont &font)
{
	if (currentId.isEmpty())
		return;
	if (auto *cfg = get_by_id(currentId.toStdString())) {
		cfg->font_family = font.family().toStdString();
	}
}

void LowerThirdSettingsDialog::onHotkeyChanged(const QKeySequence &seq)
{
	if (currentId.isEmpty())
		return;
	if (auto *cfg = get_by_id(currentId.toStdString())) {
		cfg->hotkey = seq.toString(QKeySequence::PortableText).toStdString();
	}
}

void LowerThirdSettingsDialog::updateColorButton(QPushButton *btn, const QColor &color)
{
	QString hex = color.name(QColor::HexRgb);
	QString css = QString("background-color:%1;"
			      "border:1px solid #333;"
			      "min-width:64px;"
			      "padding:2px 4px;")
			      .arg(hex);

	btn->setStyleSheet(css);
	btn->setText(hex);
}

void LowerThirdSettingsDialog::onImportTemplateClicked()
{
	QString zipPath =
		QFileDialog::getOpenFileName(this, tr("Select template ZIP"), QString(), tr("Template ZIP (*.zip)"));

	if (zipPath.isEmpty())
		return;

	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		QMessageBox::warning(this, tr("Error"), tr("Unable to create temp directory."));
		return;
	}

	QString htmlPath, cssPath, jsonPath, profilePicPath;

	if (!unzip_to_dir(zipPath, tempDir.path(), htmlPath, cssPath, jsonPath, profilePicPath)) {
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
	QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll(), &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) {
		QMessageBox::warning(this, tr("Error"), tr("Invalid JSON file."));
		return;
	}

	QJsonObject obj = doc.object();
	LowerThirdConfig *cfg = get_by_id(currentId.toStdString());
	if (!cfg)
		return;

	cfg->title = obj["title"].toString().toStdString();
	cfg->subtitle = obj["subtitle"].toString().toStdString();
	cfg->anim_in = obj["anim_in"].toString().toStdString();
	cfg->anim_out = obj["anim_out"].toString().toStdString();
	cfg->custom_anim_in = obj["custom_anim_in"].toString().toStdString();
	cfg->custom_anim_out = obj["custom_anim_out"].toString().toStdString();
	cfg->font_family = obj["font_family"].toString().toStdString();
	cfg->bg_color = obj["bg_color"].toString().toStdString();
	cfg->text_color = obj["text_color"].toString().toStdString();
	cfg->bound_scene = obj["bound_scene"].toString().toStdString();
	cfg->visible = obj["visible"].toBool();
	cfg->hotkey = obj["hotkey"].toString().toStdString();
	cfg->profile_picture = obj["profile_picture"].toString().toStdString();

	{
		QFile f1(htmlPath);
		if (f1.open(QIODevice::ReadOnly)) {
			cfg->html_template = QString::fromUtf8(f1.readAll()).toStdString();
		}
	}

	{
		QFile f2(cssPath);
		if (f2.open(QIODevice::ReadOnly)) {
			cfg->css_template = QString::fromUtf8(f2.readAll()).toStdString();
		}
	}

	if (!profilePicPath.isEmpty()) {
		QString outDir = QString::fromStdString(output_dir());

		if (!outDir.isEmpty()) {
			QDir dir(outDir);

			QString ext = QFileInfo(profilePicPath).suffix();
			QString newName = QString("%1_profile.%2").arg(cfg->id.c_str()).arg(ext);
			QString dest = dir.filePath(newName);

			QFile::remove(dest);
			QFile::copy(profilePicPath, dest);

			cfg->profile_picture = newName.toStdString();
		}
	}

	loadFromState();

	save_state_json();
	write_index_html();
	refresh_browser_source();

	QMessageBox::information(this, tr("Imported"), tr("Template imported successfully."));
}

void LowerThirdSettingsDialog::openTemplateEditorDialog(const QString &title, QPlainTextEdit *sourceEdit)
{
	if (!sourceEdit)
		return;

	QDialog dlg(this);
	dlg.setWindowTitle(title);
	dlg.resize(900, 700);

	auto *layout = new QVBoxLayout(&dlg);
	auto *bigEdit = new QPlainTextEdit(&dlg);

	bigEdit->setPlainText(sourceEdit->toPlainText());
	bigEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
	QFont monoFont = bigEdit->font();
#if defined(Q_OS_WIN)
	monoFont.setFamily(QStringLiteral("Consolas"));
#elif defined(Q_OS_MACOS)
	monoFont.setFamily(QStringLiteral("Menlo"));
#else
	monoFont.setFamily(QStringLiteral("Monospace"));
#endif
	bigEdit->setFont(monoFont);

	auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);

	layout->addWidget(bigEdit, 1);
	layout->addWidget(box);

	connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	if (dlg.exec() == QDialog::Accepted) {
		sourceEdit->setPlainText(bigEdit->toPlainText());
	}
}

void LowerThirdSettingsDialog::onOpenHtmlEditorDialog()
{
	openTemplateEditorDialog(tr("Edit HTML Template"), htmlEdit);
}

void LowerThirdSettingsDialog::onOpenCssEditorDialog()
{
	openTemplateEditorDialog(tr("Edit CSS Template"), cssEdit);
}
