#define LOG_TAG "[" PLUGIN_NAME "][settings]"
#include "settings.hpp"

#include "headers/api.hpp"

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
#include <QSettings>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QProcess>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStyle>
#include <QTabWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <QClipboard>
#include <QApplication>
#include <QFrame>
#include <QWidget>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSlider>

#include <QDesktopServices>
#include <QListWidget>
#include <QUrl>

#include <QListWidget>
#include <QListWidgetItem>
#include <QListView>
#include <QStackedWidget>
#include <QAbstractItemView>

#include <unzip.h>
#include <zip.h>
#include <algorithm>
#include <cstring>

namespace smart_lt::ui {

static bool unzip_to_dir(const QString &zipPath, const QString &destDir, QString &htmlPath, QString &cssPath,
			 QString &jsPath, QString &jsonPath, QString &profilePath, QString &soundInPath,
			 QString &soundOutPath)
{
	htmlPath.clear();
	cssPath.clear();
	jsPath.clear();
	jsonPath.clear();
	profilePath.clear();
	soundInPath.clear();
	soundOutPath.clear();

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
		else if (name.startsWith("soundIn.", Qt::CaseInsensitive) ||
			 name.contains("soundin", Qt::CaseInsensitive))
			soundInPath = outPath;
		else if (name.startsWith("soundOut.", Qt::CaseInsensitive) ||
			 name.contains("soundout", Qt::CaseInsensitive))
			soundOutPath = outPath;

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

LowerThirdSettingsDialog::LowerThirdSettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(tr("Lower Third Settings"));
	resize(820, 720);

	setWindowModality(Qt::NonModal);
	setModal(false);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(10, 10, 10, 10);
	root->setSpacing(10);

	auto *body = new QHBoxLayout();
	body->setContentsMargins(0, 0, 0, 0);
	body->setSpacing(10);

	auto *nav = new QListWidget(this);
	nav->setObjectName("ltSettingsNav");
	nav->setSelectionMode(QAbstractItemView::SingleSelection);
	nav->setMovement(QListView::Static);
	nav->setFlow(QListView::TopToBottom);
	nav->setSpacing(6);
	nav->setUniformItemSizes(true);
	nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	nav->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	nav->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	nav->setFixedWidth(200);

	auto addNavItem = [&](const QString &text) {
		auto *it = new QListWidgetItem(text, nav);
		it->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
		it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
		return it;
	};

	addNavItem(tr("Content & Media"));
	addNavItem(tr("Style & Anim"));
	addNavItem(tr("Color"));
	addNavItem(tr("Layout"));
	addNavItem(tr("Templates Gallery"));

	auto *stack = new QStackedWidget(this);
	stack->setObjectName("ltSettingsStack");
	stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	auto makeScrollPage = [&](QVBoxLayout *&outLayout) -> QScrollArea * {
		auto *inner = new QWidget(this);
		outLayout = new QVBoxLayout(inner);
		outLayout->setContentsMargins(0, 0, 0, 0);
		outLayout->setSpacing(10);

		auto *sa = new QScrollArea(this);
		sa->setFrameShape(QFrame::NoFrame);
		sa->setWidgetResizable(true);
		sa->setWidget(inner);
		return sa;
	};

	QVBoxLayout *contentPageLayout = nullptr;
	QVBoxLayout *stylePageLayout = nullptr;
	QVBoxLayout *colorsPageLayout = nullptr;
	QVBoxLayout *templatesPageLayout = nullptr;
	QVBoxLayout *templatesGalleryLayout = nullptr;

	auto *contentPageSa = makeScrollPage(contentPageLayout);
	auto *stylePageSa = makeScrollPage(stylePageLayout);
	auto *colorsPageSa = makeScrollPage(colorsPageLayout);
	auto *templatesPageSa = makeScrollPage(templatesPageLayout);
	auto *templatesGallerySa = makeScrollPage(templatesGalleryLayout);

	stack->addWidget(contentPageSa);
	stack->addWidget(stylePageSa);
	stack->addWidget(colorsPageSa);
	stack->addWidget(templatesPageSa);
	stack->addWidget(templatesGallerySa);

	body->addWidget(nav);
	body->addWidget(stack, 1);

	root->addLayout(body, /*stretch*/ 1);

	nav->setCurrentRow(0);
	stack->setCurrentIndex(0);

	connect(nav, &QListWidget::currentRowChanged, this, [stack](int row) {
		if (row >= 0 && row < stack->count())
			stack->setCurrentIndex(row);
	});

	{
		auto *contentBox = new QGroupBox(tr("Content && Media"), this);
		contentBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

		auto *v = new QVBoxLayout(contentBox);
		v->setContentsMargins(8, 8, 8, 8);
		v->setSpacing(0);

		auto *g = new QGridLayout();
		g->setSpacing(8);
		g->setContentsMargins(0, 0, 0, 0);

		int row = 0;

		g->addWidget(new QLabel(tr("Dock Label:"), this), row, 0);
		labelEdit = new QLineEdit(this);
		labelEdit->setToolTip(tr("Display-only label used in the dock list"));
		g->addWidget(labelEdit, row, 1, 1, 3);

		row++;
		g->addWidget(new QLabel(tr("Title:"), this), row, 0);
		titleEdit = new QLineEdit(this);
		g->addWidget(titleEdit, row, 1, 1, 3);

		row++;
		g->addWidget(new QLabel(tr("Subtitle:"), this), row, 0);
		subtitleEdit = new QLineEdit(this);
		g->addWidget(subtitleEdit, row, 1, 1, 3);

		row++;
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

		deleteProfilePictureBtn = new QPushButton(this);
		deleteProfilePictureBtn->setCursor(Qt::PointingHandCursor);
		{
			QIcon del = QIcon::fromTheme(QStringLiteral("edit-delete"));
			if (del.isNull())
				del = style()->standardIcon(QStyle::SP_DialogCloseButton);
			deleteProfilePictureBtn->setIcon(del);
		}
		deleteProfilePictureBtn->setToolTip(tr("Remove profile picture"));
		deleteProfilePictureBtn->setFixedWidth(32);

		picRow->addWidget(profilePictureEdit, 1);
		picRow->addWidget(browseProfilePictureBtn);
		picRow->addWidget(deleteProfilePictureBtn);

		g->addLayout(picRow, row, 1, 1, 3);

		row++;
		g->addWidget(new QLabel(tr("Sound (Animation In):"), this), row, 0);

		auto *sndInRow = new QHBoxLayout();
		sndInRow->setContentsMargins(0, 0, 0, 0);

		animInSoundEdit = new QLineEdit(this);
		animInSoundEdit->setReadOnly(true);

		browseAnimInSoundBtn = new QPushButton(this);
		browseAnimInSoundBtn->setCursor(Qt::PointingHandCursor);
		browseAnimInSoundBtn->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
		browseAnimInSoundBtn->setToolTip(tr("Browse sound for animation in..."));
		browseAnimInSoundBtn->setFixedWidth(32);

		deleteAnimInSoundBtn = new QPushButton(this);
		deleteAnimInSoundBtn->setCursor(Qt::PointingHandCursor);
		{
			QIcon del = QIcon::fromTheme(QStringLiteral("edit-delete"));
			if (del.isNull())
				del = style()->standardIcon(QStyle::SP_DialogCloseButton);
			deleteAnimInSoundBtn->setIcon(del);
		}
		deleteAnimInSoundBtn->setToolTip(tr("Remove sound for animation in"));
		deleteAnimInSoundBtn->setFixedWidth(32);

		sndInRow->addWidget(animInSoundEdit, 1);
		sndInRow->addWidget(browseAnimInSoundBtn);
		sndInRow->addWidget(deleteAnimInSoundBtn);

		g->addLayout(sndInRow, row, 1, 1, 3);

		row++;
		g->addWidget(new QLabel(tr("Sound (Animation Out):"), this), row, 0);

		auto *sndOutRow = new QHBoxLayout();
		sndOutRow->setContentsMargins(0, 0, 0, 0);

		animOutSoundEdit = new QLineEdit(this);
		animOutSoundEdit->setReadOnly(true);

		browseAnimOutSoundBtn = new QPushButton(this);
		browseAnimOutSoundBtn->setCursor(Qt::PointingHandCursor);
		browseAnimOutSoundBtn->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
		browseAnimOutSoundBtn->setToolTip(tr("Browse sound for animation out..."));
		browseAnimOutSoundBtn->setFixedWidth(32);

		deleteAnimOutSoundBtn = new QPushButton(this);
		deleteAnimOutSoundBtn->setCursor(Qt::PointingHandCursor);
		{
			QIcon del = QIcon::fromTheme(QStringLiteral("edit-delete"));
			if (del.isNull())
				del = style()->standardIcon(QStyle::SP_DialogCloseButton);
			deleteAnimOutSoundBtn->setIcon(del);
		}
		deleteAnimOutSoundBtn->setToolTip(tr("Remove sound for animation out"));
		deleteAnimOutSoundBtn->setFixedWidth(32);

		sndOutRow->addWidget(animOutSoundEdit, 1);
		sndOutRow->addWidget(browseAnimOutSoundBtn);
		sndOutRow->addWidget(deleteAnimOutSoundBtn);

		g->addLayout(sndOutRow, row, 1, 1, 3);

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
		repeatEverySpin->setRange(0, 24 * 60 * 60);
		repeatEverySpin->setToolTip(tr("0 disables auto-repeat"));
		g->addWidget(repeatEverySpin, row, 1);

		g->addWidget(new QLabel(tr("Keep visible (sec):"), this), row, 2);

		repeatVisibleSpin = new QSpinBox(this);
		repeatVisibleSpin->setRange(0, 24 * 60 * 60);
		repeatVisibleSpin->setToolTip(tr("0 uses default (recommended: 3-8 sec)"));
		g->addWidget(repeatVisibleSpin, row, 3);

		g->setColumnStretch(0, 0);
		g->setColumnStretch(1, 1);
		g->setColumnStretch(2, 0);
		g->setColumnStretch(3, 1);

		v->addLayout(g);
		v->addStretch(1);

		contentPageLayout->addWidget(contentBox);

		connect(clearHotkeyBtn, &QPushButton::clicked, this,
			[this]() { hotkeyEdit->setKeySequence(QKeySequence()); });

		connect(browseProfilePictureBtn, &QPushButton::clicked, this,
			&LowerThirdSettingsDialog::onBrowseProfilePicture);
		connect(deleteProfilePictureBtn, &QPushButton::clicked, this,
			&LowerThirdSettingsDialog::onDeleteProfilePicture);

		connect(browseAnimInSoundBtn, &QPushButton::clicked, this,
			&LowerThirdSettingsDialog::onBrowseAnimInSound);
		connect(deleteAnimInSoundBtn, &QPushButton::clicked, this,
			&LowerThirdSettingsDialog::onDeleteAnimInSound);

		connect(browseAnimOutSoundBtn, &QPushButton::clicked, this,
			&LowerThirdSettingsDialog::onBrowseAnimOutSound);
		connect(deleteAnimOutSoundBtn, &QPushButton::clicked, this,
			&LowerThirdSettingsDialog::onDeleteAnimOutSound);
	}

	{
		auto *styleBox = new QGroupBox(tr("Style"), this);
		styleBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

		auto *v = new QVBoxLayout(styleBox);
		v->setContentsMargins(8, 8, 8, 8);
		v->setSpacing(0);

		auto *g = new QGridLayout();
		g->setSpacing(8);
		g->setContentsMargins(0, 0, 0, 0);

		int row = 0;

		g->addWidget(new QLabel(tr("Anim In:"), this), row, 0);
		animInCombo = new QComboBox(this);
		for (const auto &opt : AnimInOptions)
			animInCombo->addItem(tr(opt.label), QString::fromUtf8(opt.value));
		g->addWidget(animInCombo, row, 1);

		row++;
		g->addWidget(new QLabel(tr("Anim Out:"), this), row, 0);
		animOutCombo = new QComboBox(this);
		for (const auto &opt : AnimOutOptions)
			animOutCombo->addItem(tr(opt.label), QString::fromUtf8(opt.value));
		g->addWidget(animOutCombo, row, 1);

		row++;
		g->addWidget(new QLabel(tr("Font:"), this), row, 0);
		fontCombo = new QFontComboBox(this);
		fontCombo->setEditable(false);
		g->addWidget(fontCombo, row, 1);

		row++;
		g->addWidget(new QLabel(tr("Position:"), this), row, 0);
		posCombo = new QComboBox(this);
		for (const auto &opt : LtPositionOptions)
			posCombo->addItem(tr(opt.label), QString::fromUtf8(opt.value));
		g->addWidget(posCombo, row, 1);

		row++;
		g->addWidget(new QLabel(tr("Subtitle size (px):"), this), row, 0);
		subtitleSizeSpin = new QSpinBox(this);
		subtitleSizeSpin->setRange(6, 200);
		subtitleSizeSpin->setToolTip(tr("Font size in pixels for {{SUBTITLE_SIZE}} placeholder"));
		g->addWidget(subtitleSizeSpin, row, 1);

		row++;
		g->addWidget(new QLabel(tr("Title size (px):"), this), row, 0);
		titleSizeSpin = new QSpinBox(this);
		titleSizeSpin->setRange(6, 200);
		titleSizeSpin->setToolTip(tr("Font size in pixels for {{TITLE_SIZE}} placeholder"));
		g->addWidget(titleSizeSpin, row, 1);

		row++;
		g->addWidget(new QLabel(tr("Avatar height (px):"), this), row, 0);
		avatarHeightSpin = new QSpinBox(this);
		avatarHeightSpin->setRange(10, 400);
		avatarHeightSpin->setToolTip(tr("Avatar height in pixels for {{AVATAR_HEIGHT}} placeholder"));
		g->addWidget(avatarHeightSpin, row, 1);

		row++;
		g->addWidget(new QLabel(tr("Avatar width (px):"), this), row, 0);
		avatarWidthSpin = new QSpinBox(this);
		avatarWidthSpin->setRange(10, 400);
		avatarWidthSpin->setToolTip(tr("Avatar width in pixels for {{AVATAR_WIDTH}} placeholder"));
		g->addWidget(avatarWidthSpin, row, 1);


		row++;
		g->addWidget(new QLabel(tr("Opacity:"), this), row, 0);
		opacitySlider = new QSlider(Qt::Horizontal, this);
		opacitySlider->setRange(0, 100);
		opacitySlider->setSingleStep(5);
		opacitySlider->setPageStep(10);
		opacitySlider->setToolTip(tr("0 = transparent, 100 = opaque"));
		g->addWidget(opacitySlider, row, 1);

		opacityValue = new QLabel(this);
		opacityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		g->addWidget(opacityValue, row, 2, 1, 2);

		row++;
		g->addWidget(new QLabel(tr("Radius:"), this), row, 0);
		radiusSlider = new QSlider(Qt::Horizontal, this);
		radiusSlider->setRange(0, 100);
		radiusSlider->setSingleStep(1);
		radiusSlider->setPageStep(5);
		radiusSlider->setToolTip(tr("Border radius percentage (0-100)"));
		g->addWidget(radiusSlider, row, 1);

		radiusValue = new QLabel(this);
		radiusValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		g->addWidget(radiusValue, row, 2, 1, 2);

		g->setColumnStretch(0, 0);
		g->setColumnStretch(1, 1);
		g->setColumnStretch(2, 0);
		g->setColumnStretch(3, 0);

		v->addLayout(g);
		v->addStretch(1);

		stylePageLayout->addWidget(styleBox);

		auto updateOpacityLabel = [this]() {
			const int val = opacitySlider ? opacitySlider->value() : 0;
			if (opacityValue)
				opacityValue->setText(QString("%1 Units").arg(val));
		};

		auto updateRadiusLabel = [this]() {
			const int val = radiusSlider ? radiusSlider->value() : 0;
			if (radiusValue)
				radiusValue->setText(QString("%1 Units").arg(val));
		};

		connect(opacitySlider, &QSlider::valueChanged, this,
			[updateOpacityLabel](int) { updateOpacityLabel(); });
		connect(radiusSlider, &QSlider::valueChanged, this, [updateRadiusLabel](int) { updateRadiusLabel(); });

		updateOpacityLabel();
		updateRadiusLabel();

		connect(animInCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			&LowerThirdSettingsDialog::onAnimInChanged);
		connect(animOutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			&LowerThirdSettingsDialog::onAnimOutChanged);
	}


	{
		auto *colorsBox = new QGroupBox(tr("Colors"), this);
		colorsBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

		auto *v = new QVBoxLayout(colorsBox);
		v->setContentsMargins(8, 8, 8, 8);
		v->setSpacing(0);

		auto *g = new QGridLayout();
		g->setContentsMargins(0, 0, 0, 0);
		g->setHorizontalSpacing(10);
		g->setVerticalSpacing(10);

		int row = 0;

		g->addWidget(new QLabel(tr("Primary color:"), this), row, 0);
		primaryColorBtn = new QPushButton(tr("Pick"), this);
		primaryColorBtn->setToolTip(tr("Used for {{PRIMARY_COLOR}} (and legacy {{BG_COLOR}} fallback)"));
		g->addWidget(primaryColorBtn, row, 1);

		row++;
		g->addWidget(new QLabel(tr("Secondary color:"), this), row, 0);
		secondaryColorBtn = new QPushButton(tr("Pick"), this);
		secondaryColorBtn->setToolTip(tr("Used for {{SECONDARY_COLOR}}"));
		g->addWidget(secondaryColorBtn, row, 1);

		row++;
		g->addWidget(new QLabel(tr("Title color:"), this), row, 0);
		titleColorBtn = new QPushButton(tr("Pick"), this);
		titleColorBtn->setToolTip(tr("Used for {{TITLE_COLOR}} (and legacy {{TEXT_COLOR}} fallback)"));
		g->addWidget(titleColorBtn, row, 1);

		row++;
		g->addWidget(new QLabel(tr("Subtitle color:"), this), row, 0);
		subtitleColorBtn = new QPushButton(tr("Pick"), this);
		subtitleColorBtn->setToolTip(tr("Used for {{SUBTITLE_COLOR}}"));
		g->addWidget(subtitleColorBtn, row, 1);

		g->setColumnStretch(0, 0);
		g->setColumnStretch(1, 1);

		v->addLayout(g);
		v->addStretch(1);

		colorsPageLayout->addWidget(colorsBox);

		connect(primaryColorBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onPickPrimaryColor);
		connect(secondaryColorBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onPickSecondaryColor);
		connect(titleColorBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onPickTitleColor);
		connect(subtitleColorBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onPickSubtitleColor);
	}
	{
		auto *editorsBox = new QGroupBox(tr("Template Editors"), this);
		editorsBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

		auto *editorsV = new QVBoxLayout(editorsBox);
		editorsV->setSpacing(8);
		editorsV->setContentsMargins(8, 8, 8, 8);

		tplTabs = new QTabWidget(editorsBox);
		tplTabs->setDocumentMode(true);
		tplTabs->setObjectName("ltTplTabs");
		tplTabs->tabBar()->setObjectName("ltTplTabBar");
		tplTabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		tplTabs->setMinimumHeight(360);

		auto makeTab = [&](const QString &name, QPlainTextEdit *&edit) {
			auto *page = new QWidget(tplTabs);
			auto *v = new QVBoxLayout(page);
			v->setContentsMargins(0, 0, 0, 0);
			v->setSpacing(0);

			edit = new QPlainTextEdit(page);
			edit->setLineWrapMode(QPlainTextEdit::NoWrap);
			edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
			edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

			v->addWidget(edit, 1);
			tplTabs->addTab(page, name);
		};

		makeTab(tr("HTML"), htmlEdit);
		makeTab(tr("CSS"), cssEdit);
		makeTab(tr("JS"), jsEdit);

		auto *expandBtn = new QPushButton(tplTabs);
		expandBtn->setFlat(true);
		expandBtn->setCursor(Qt::PointingHandCursor);
		expandBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
		expandBtn->setToolTip(tr("Open editor..."));
		expandBtn->setFixedSize(32, 28);
		tplTabs->setCornerWidget(expandBtn, Qt::TopRightCorner);

		connect(expandBtn, &QPushButton::clicked, this, [this]() {
			const int idx = tplTabs->currentIndex();
			if (idx == 0)
				onOpenHtmlEditorDialog();
			else if (idx == 1)
				onOpenCssEditorDialog();
			else if (idx == 2)
				onOpenJsEditorDialog();
		});

		editorsV->addWidget(tplTabs, 1);

		editorsV->addStretch(0);

		auto *phBox = new QGroupBox(tr("Template Placeholders"), this);
		phBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

		auto *phV = new QVBoxLayout(phBox);
		phV->setContentsMargins(8, 8, 8, 8);
		phV->setSpacing(6);

		auto *phText = new QPlainTextEdit(phBox);
		phText->setReadOnly(true);
		phText->setLineWrapMode(QPlainTextEdit::NoWrap);
		phText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
		phText->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		phText->setMinimumHeight(150);
		phText->setMaximumHeight(220);

		phText->setPlainText("Text\n"
				     "  {{TITLE}}            Title text\n"
				     "  {{SUBTITLE}}         Subtitle text\n"
				     "  {{TITLE_SIZE}}       Title font size (px)\n"
				     "  {{SUBTITLE_SIZE}}    Subtitle font size (px)\n"
				     "\n"
				     "Avatar\n"
				     "  {{PROFILE_PICTURE}}  Profile image URL/path (may be empty)\n"
				     "  {{AVATAR_WIDTH}}     Avatar width (px)\n"
				     "  {{AVATAR_HEIGHT}}    Avatar height (px)\n"
				     "\n"
				     				     "Colors\n"
				     "  {{PRIMARY_COLOR}}    Primary color\n"
				     "  {{SECONDARY_COLOR}}  Secondary color\n"
				     "  {{TITLE_COLOR}}      Title color\n"
				     "  {{SUBTITLE_COLOR}}   Subtitle color\n"
				     "  {{BG_COLOR}}         (Legacy) maps to PRIMARY_COLOR\n"
				     "  {{TEXT_COLOR}}       (Legacy) maps to TITLE_COLOR\n"
				     "\n"
				     "  {{OPACITY}}          Opacity (0-100 or normalized based on your templater)\n"
				     "  {{RADIUS}}           Radius (0-100)\n"
				     "\n"
				     "Behavior\n"
				     "  {{ANIM_IN}}          Animate.css in class\n"
				     "  {{ANIM_OUT}}         Animate.css out class\n"
				     "  {{POSITION}}         Screen position key\n");

		phV->addWidget(phText);

		phV->addStretch(1);

		templatesPageLayout->addWidget(editorsBox, /*stretch*/ 1);
		templatesPageLayout->addWidget(phBox, /*stretch*/ 0);
	}

	{
		auto *marketBox = new QGroupBox(tr("Lower Thirds Gallery"), this);
		marketBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

		auto *mv = new QVBoxLayout(marketBox);
		mv->setSpacing(8);
		mv->setContentsMargins(8, 8, 8, 8);

		auto *hero = new QFrame(marketBox);
		hero->setObjectName(QStringLiteral("oc_marketHero"));
		hero->setFrameShape(QFrame::NoFrame);
		hero->setStyleSheet(QStringLiteral("#oc_marketHero {"
						   "  border: 1px solid rgba(255,255,255,0.10);"
						   "  border-radius: 10px;"
						   "  background: rgba(255,255,255,0.04);"
						   "  padding: 10px;"
						   "}"
						   "#oc_marketHero QLabel { background: transparent; }"));

		auto *heroRow = new QHBoxLayout(hero);
		heroRow->setContentsMargins(8, 8, 8, 8);
		heroRow->setSpacing(10);

		auto *heroIcon = new QLabel(hero);
		heroIcon->setFixedSize(40, 40);
		heroIcon->setPixmap(style()->standardIcon(QStyle::SP_DirOpenIcon).pixmap(36, 36));
		heroIcon->setAlignment(Qt::AlignCenter);
		heroRow->addWidget(heroIcon);

		auto *heroTextCol = new QVBoxLayout();
		heroTextCol->setContentsMargins(0, 0, 0, 0);
		heroTextCol->setSpacing(2);

		auto *heroTitle = new QLabel(tr("Get new Lower Thirds instantly"), hero);
		heroTitle->setTextFormat(Qt::PlainText);
		heroTitle->setStyleSheet(QStringLiteral("font-weight: 700;"));
		heroTextCol->addWidget(heroTitle);

		marketStatus = new QLabel(tr("Loading recommendations…"), hero);
		marketStatus->setWordWrap(true);
		marketStatus->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.85);"));
		heroTextCol->addWidget(marketStatus);

		heroRow->addLayout(heroTextCol, 1);

		seeAllLowerThirdsBtn = new QPushButton(tr("See all lower thirds"), hero);
		seeAllLowerThirdsBtn->setCursor(Qt::PointingHandCursor);
		seeAllLowerThirdsBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
		seeAllLowerThirdsBtn->setToolTip(tr("Open the Lower Thirds marketplace in your browser"));
		seeAllLowerThirdsBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
		heroRow->addWidget(seeAllLowerThirdsBtn);

		mv->addWidget(hero);

		marketList = new QListWidget(marketBox);
		marketList->setObjectName(QStringLiteral("oc_marketList"));
		marketList->setSelectionMode(QAbstractItemView::SingleSelection);
		marketList->setUniformItemSizes(false);
		marketList->setMinimumHeight(280);
		marketList->setSpacing(6);

		marketList->setFrameShape(QFrame::NoFrame);
		marketList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		marketList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

		marketList->setStyleSheet(QStringLiteral(
			"#oc_marketList { border: none; background: transparent; }"
			"#oc_marketList::item { border: none; padding: 0px; margin: 0px; background: transparent; }"
			"#oc_marketList::item:selected { background: transparent; }"
			"#oc_marketList::item:hover { background: transparent; }"));

		mv->addWidget(marketList, 1);

		templatesGalleryLayout->addWidget(marketBox);

		connect(marketList, &QListWidget::itemActivated, this, [](QListWidgetItem *it) {
			if (!it)
				return;
			const QString url = it->data(Qt::UserRole).toString();
			if (!url.isEmpty())
				QDesktopServices::openUrl(QUrl(url));
		});

		connect(seeAllLowerThirdsBtn, &QPushButton::clicked, this, []() {
			QDesktopServices::openUrl(
				QUrl(QStringLiteral("https://obscountdown.com/?type=lower-thirds-templates")));
		});

		marketList->viewport()->installEventFilter(this);

		auto &api = smart_lt::api::ApiClient::instance();
		connect(&api, &smart_lt::api::ApiClient::lowerThirdsUpdated, this,
			&LowerThirdSettingsDialog::onMarketplaceUpdated);
		connect(&api, &smart_lt::api::ApiClient::lowerThirdsFailed, this,
			&LowerThirdSettingsDialog::onMarketplaceFailed);
		connect(&api, &smart_lt::api::ApiClient::imageReady, this,
			&LowerThirdSettingsDialog::onMarketplaceImageReady);
		connect(&api, &smart_lt::api::ApiClient::imageFailed, this,
			&LowerThirdSettingsDialog::onMarketplaceImageFailed);

		rebuildMarketplaceList();
	}

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

		auto *applyBtn = buttonBox->addButton(tr("Save && Apply"), QDialogButtonBox::ApplyRole);
		applyBtn->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
		applyBtn->setAutoDefault(false);
		applyBtn->setDefault(false);

		if (auto *cancelBtn = buttonBox->button(QDialogButtonBox::Cancel)) {
			cancelBtn->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
		}

		footer->addWidget(buttonBox);

		root->addLayout(footer);

		connect(importBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onImportTemplateClicked);
		connect(exportBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onExportTemplateClicked);

		if (infoBtn)
			connect(infoBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onInfoClicked);

		connect(buttonBox, &QDialogButtonBox::rejected, this, &LowerThirdSettingsDialog::reject,
			Qt::UniqueConnection);
		connect(applyBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onSaveAndApply,
			Qt::UniqueConnection);
	}

	this->setStyleSheet(R"QSS(
QListWidget#ltSettingsNav {
  border: none;
  border-radius: 6px;
  background: #21232a;
  padding: 6px;
}

QListWidget#ltSettingsNav::item {
  border: none;
  border-radius: 6px;
  background: rgba(255,255,255,0.03);
  color: rgba(255,255,255,0.78);
  padding: 10px 10px;
  margin: 2px 0;
  font-weight: 600;
}

QListWidget#ltSettingsNav::item:hover {
  background: rgba(255,255,255,0.06);
  color: rgba(255,255,255,0.92);
}

QListWidget#ltSettingsNav::item:selected {
  background: rgba(90,140,255,0.22);
  border-color: rgba(90,140,255,0.45);
  color: rgba(255,255,255,0.98);
}
)QSS");
}

LowerThirdSettingsDialog::~LowerThirdSettingsDialog()
{
	delete currentPrimaryColor;
	delete currentSecondaryColor;
	delete currentTitleColor;
	delete currentSubtitleColor;
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

	// More contextual window title (guarded for empty label/title)
	{
		QString lbl = QString::fromStdString(cfg->label).trimmed();
		if (lbl.isEmpty())
			lbl = QString::fromStdString(cfg->title).trimmed();
		if (lbl.isEmpty())
			lbl = currentId;
		setWindowTitle(tr("You are editing lt: %1").arg(lbl));
	}

	titleEdit->setText(QString::fromStdString(cfg->title));
	if (labelEdit)
		labelEdit->setText(QString::fromStdString(cfg->label));
	subtitleEdit->setText(QString::fromStdString(cfg->subtitle));

	if (titleSizeSpin)
		titleSizeSpin->setValue(cfg->title_size);
	if (subtitleSizeSpin)
		subtitleSizeSpin->setValue(cfg->subtitle_size);
	if (avatarWidthSpin)
		avatarWidthSpin->setValue(cfg->avatar_width);
	if (avatarHeightSpin)
		avatarHeightSpin->setValue(cfg->avatar_height);

	auto setCombo = [](QComboBox *cb, const QString &v) {
		const int idx = cb->findData(v);
		cb->setCurrentIndex(idx >= 0 ? idx : 0);
	};

	setCombo(animInCombo, QString::fromStdString(cfg->anim_in));
	setCombo(animOutCombo, QString::fromStdString(cfg->anim_out));

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

	if (cfg->anim_in_sound.empty())
		animInSoundEdit->clear();
	else
		animInSoundEdit->setText(QString::fromStdString(cfg->anim_in_sound));

	if (cfg->anim_out_sound.empty())
		animOutSoundEdit->clear();
	else
		animOutSoundEdit->setText(QString::fromStdString(cfg->anim_out_sound));

	delete currentPrimaryColor;
	currentPrimaryColor = nullptr;
	delete currentSecondaryColor;
	currentSecondaryColor = nullptr;
	delete currentTitleColor;
	currentTitleColor = nullptr;
	delete currentSubtitleColor;
	currentSubtitleColor = nullptr;

	QColor primary(QString::fromStdString(cfg->primary_color));
	QColor secondary(QString::fromStdString(cfg->secondary_color));
	QColor title(QString::fromStdString(cfg->title_color));
	QColor subtitle(QString::fromStdString(cfg->subtitle_color));

	if (!primary.isValid())
		primary = QColor(17, 24, 39);
	if (!secondary.isValid())
		secondary = QColor(31, 41, 55);
	if (!title.isValid())
		title = QColor(249, 250, 251);
	if (!subtitle.isValid())
		subtitle = QColor(209, 213, 219);

	currentPrimaryColor = new QColor(primary);
	currentSecondaryColor = new QColor(secondary);
	currentTitleColor = new QColor(title);
	currentSubtitleColor = new QColor(subtitle);

	updateColorButton(primaryColorBtn, primary);
	updateColorButton(secondaryColorBtn, secondary);
	updateColorButton(titleColorBtn, title);
	updateColorButton(subtitleColorBtn, subtitle);

	int op = 85;
	int rad = 18;

	op = cfg->opacity;
	rad = cfg->radius;

	op = std::max(0, std::min(100, op));
	rad = std::max(0, std::min(100, rad));

	op = (op / 5) * 5;

	if (opacitySlider)
		opacitySlider->setValue(op);
	if (radiusSlider)
		radiusSlider->setValue(rad);

	if (opacityValue) {
		opacityValue->setText(QString("%1 Units").arg(op));
	}
	if (radiusValue) {
		radiusValue->setText(QString("%1 Units").arg(rad));
	}

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
	if (labelEdit)
		cfg->label = labelEdit->text().toStdString();
	cfg->subtitle = subtitleEdit->text().toStdString();

	cfg->anim_in = animInCombo->currentData().toString().toStdString();
	cfg->anim_out = animOutCombo->currentData().toString().toStdString();

	cfg->font_family = fontCombo->currentFont().family().toStdString();
	cfg->lt_position = posCombo->currentData().toString().toStdString();

	if (currentPrimaryColor)
		cfg->primary_color = currentPrimaryColor->name(QColor::HexRgb).toStdString();
	if (currentSecondaryColor)
		cfg->secondary_color = currentSecondaryColor->name(QColor::HexRgb).toStdString();
	if (currentTitleColor)
		cfg->title_color = currentTitleColor->name(QColor::HexRgb).toStdString();
	if (currentSubtitleColor)
		cfg->subtitle_color = currentSubtitleColor->name(QColor::HexRgb).toStdString();

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

	if (titleSizeSpin)
		cfg->title_size = titleSizeSpin->value();
	if (subtitleSizeSpin)
		cfg->subtitle_size = subtitleSizeSpin->value();
	if (avatarWidthSpin)
		cfg->avatar_width = avatarWidthSpin->value();
	if (avatarHeightSpin)
		cfg->avatar_height = avatarHeightSpin->value();

	cfg->html_template = htmlEdit->toPlainText().toStdString();
	cfg->css_template = cssEdit->toPlainText().toStdString();
	cfg->js_template = jsEdit->toPlainText().toStdString();

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

	if (!pendingAnimInSoundPath.isEmpty() && smart_lt::has_output_dir()) {
		const QString outDir = QString::fromStdString(smart_lt::output_dir());
		QDir dir(outDir);

		if (!cfg->anim_in_sound.empty()) {
			const QString oldPath = dir.filePath(QString::fromStdString(cfg->anim_in_sound));
			if (QFile::exists(oldPath))
				QFile::remove(oldPath);
		}

		const QFileInfo fi(pendingAnimInSoundPath);
		const QString ext = fi.suffix().toLower();
		const qint64 ts = QDateTime::currentMSecsSinceEpoch();

		QString newFileName = QString("%1_in_%2").arg(QString::fromStdString(cfg->id)).arg(ts);
		if (!ext.isEmpty())
			newFileName += "." + ext;

		const QString destPath = dir.filePath(newFileName);
		QFile::remove(destPath);

		if (QFile::copy(pendingAnimInSoundPath, destPath)) {
			cfg->anim_in_sound = newFileName.toStdString();
			animInSoundEdit->setText(newFileName);
		} else {
			LOGW("Failed to copy anim-in sound '%s' -> '%s'", pendingAnimInSoundPath.toUtf8().constData(),
			     destPath.toUtf8().constData());
		}

		pendingAnimInSoundPath.clear();
	}

	if (!pendingAnimOutSoundPath.isEmpty() && smart_lt::has_output_dir()) {
		const QString outDir = QString::fromStdString(smart_lt::output_dir());
		QDir dir(outDir);

		if (!cfg->anim_out_sound.empty()) {
			const QString oldPath = dir.filePath(QString::fromStdString(cfg->anim_out_sound));
			if (QFile::exists(oldPath))
				QFile::remove(oldPath);
		}

		const QFileInfo fi(pendingAnimOutSoundPath);
		const QString ext = fi.suffix().toLower();
		const qint64 ts = QDateTime::currentMSecsSinceEpoch();

		QString newFileName = QString("%1_out_%2").arg(QString::fromStdString(cfg->id)).arg(ts);
		if (!ext.isEmpty())
			newFileName += "." + ext;

		const QString destPath = dir.filePath(newFileName);
		QFile::remove(destPath);

		if (QFile::copy(pendingAnimOutSoundPath, destPath)) {
			cfg->anim_out_sound = newFileName.toStdString();
			animOutSoundEdit->setText(newFileName);
		} else {
			LOGW("Failed to copy anim-out sound '%s' -> '%s'", pendingAnimOutSoundPath.toUtf8().constData(),
			     destPath.toUtf8().constData());
		}

		pendingAnimOutSoundPath.clear();
	}

	smart_lt::save_state_json();
}

void LowerThirdSettingsDialog::onSaveAndApply()
{
	saveToState();

	// Rebuild/swap updates the Browser Source, but the dock UI still needs a
	// model refresh. Emit a core list-change event so any listeners re-sync.
	if (smart_lt::rebuild_and_swap()) {
		smart_lt::notify_list_updated(currentId.toStdString());
	}
	close();
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

void LowerThirdSettingsDialog::onDeleteProfilePicture()
{
	if (currentId.isEmpty())
		return;

	auto *cfg = smart_lt::get_by_id(currentId.toStdString());
	if (!cfg)
		return;

	if (cfg->profile_picture.empty() && pendingProfilePicturePath.isEmpty()) {
		profilePictureEdit->clear();
		return;
	}

	const QMessageBox::StandardButton btn =
		QMessageBox::question(this, tr("Remove profile picture"), tr("Delete the current profile picture?"),
				      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

	if (btn != QMessageBox::Yes)
		return;

	if (!cfg->profile_picture.empty() && smart_lt::has_output_dir()) {
		QDir dir(QString::fromStdString(smart_lt::output_dir()));
		const QString oldPath = dir.filePath(QString::fromStdString(cfg->profile_picture));
		if (QFile::exists(oldPath))
			QFile::remove(oldPath);
	}

	cfg->profile_picture.clear();
	pendingProfilePicturePath.clear();
	profilePictureEdit->clear();

	smart_lt::save_state_json();
}

void LowerThirdSettingsDialog::onBrowseAnimInSound()
{
	const QString filter = tr("Audio (*.mp3 *.wav);;All Files (*.*)");
	const QString file = QFileDialog::getOpenFileName(this, tr("Select sound for animation in"), QString(), filter);
	if (file.isEmpty())
		return;

	pendingAnimInSoundPath = file;
	animInSoundEdit->setText(file);
}

void LowerThirdSettingsDialog::onDeleteAnimInSound()
{
	if (currentId.isEmpty())
		return;

	auto *cfg = smart_lt::get_by_id(currentId.toStdString());
	if (!cfg)
		return;

	if (!cfg->anim_in_sound.empty() && smart_lt::has_output_dir()) {
		const QString outDir = QString::fromStdString(smart_lt::output_dir());
		QDir dir(outDir);
		const QString oldPath = dir.filePath(QString::fromStdString(cfg->anim_in_sound));
		if (QFile::exists(oldPath))
			QFile::remove(oldPath);
	}

	cfg->anim_in_sound.clear();
	pendingAnimInSoundPath.clear();
	animInSoundEdit->clear();

	smart_lt::save_state_json();
}

void LowerThirdSettingsDialog::onBrowseAnimOutSound()
{
	const QString filter = tr("Audio (*.mp3 *.wav);;All Files (*.*)");
	const QString file =
		QFileDialog::getOpenFileName(this, tr("Select sound for animation out"), QString(), filter);
	if (file.isEmpty())
		return;

	pendingAnimOutSoundPath = file;
	animOutSoundEdit->setText(file);
}

void LowerThirdSettingsDialog::onDeleteAnimOutSound()
{
	if (currentId.isEmpty())
		return;

	auto *cfg = smart_lt::get_by_id(currentId.toStdString());
	if (!cfg)
		return;

	if (!cfg->anim_out_sound.empty() && smart_lt::has_output_dir()) {
		const QString outDir = QString::fromStdString(smart_lt::output_dir());
		QDir dir(outDir);
		const QString oldPath = dir.filePath(QString::fromStdString(cfg->anim_out_sound));
		if (QFile::exists(oldPath))
			QFile::remove(oldPath);
	}

	cfg->anim_out_sound.clear();
	pendingAnimOutSoundPath.clear();
	animOutSoundEdit->clear();

	smart_lt::save_state_json();
}


void LowerThirdSettingsDialog::onPickPrimaryColor()
{
	QColor start = currentPrimaryColor ? *currentPrimaryColor : QColor(17, 24, 39);
	QColor c = QColorDialog::getColor(start, this, tr("Pick primary color"));
	if (!c.isValid())
		return;

	if (!currentPrimaryColor)
		currentPrimaryColor = new QColor(c);
	else
		*currentPrimaryColor = c;

	updateColorButton(primaryColorBtn, c);
}

void LowerThirdSettingsDialog::onPickSecondaryColor()
{
	QColor start = currentSecondaryColor ? *currentSecondaryColor : QColor(31, 41, 55);
	QColor c = QColorDialog::getColor(start, this, tr("Pick secondary color"));
	if (!c.isValid())
		return;

	if (!currentSecondaryColor)
		currentSecondaryColor = new QColor(c);
	else
		*currentSecondaryColor = c;

	updateColorButton(secondaryColorBtn, c);
}

void LowerThirdSettingsDialog::onPickTitleColor()
{
	QColor start = currentTitleColor ? *currentTitleColor : QColor(249, 250, 251);
	QColor c = QColorDialog::getColor(start, this, tr("Pick title color"));
	if (!c.isValid())
		return;

	if (!currentTitleColor)
		currentTitleColor = new QColor(c);
	else
		*currentTitleColor = c;

	updateColorButton(titleColorBtn, c);
}

void LowerThirdSettingsDialog::onPickSubtitleColor()
{
	QColor start = currentSubtitleColor ? *currentSubtitleColor : QColor(209, 213, 219);
	QColor c = QColorDialog::getColor(start, this, tr("Pick subtitle color"));
	if (!c.isValid())
		return;

	if (!currentSubtitleColor)
		currentSubtitleColor = new QColor(c);
	else
		*currentSubtitleColor = c;

	updateColorButton(subtitleColorBtn, c);
}



void LowerThirdSettingsDialog::onAnimInChanged(int) {}
void LowerThirdSettingsDialog::onAnimOutChanged(int) {}

void LowerThirdSettingsDialog::onMarketplaceUpdated()
{
	rebuildMarketplaceList();
}

void LowerThirdSettingsDialog::onMarketplaceFailed(const QString &err)
{
	if (marketStatus) {
		QString msg = err.trimmed();
		if (msg.isEmpty())
			msg = tr("Could not load recommendations.");
		marketStatus->setText(tr("Recommendations unavailable: %1").arg(msg));
	}

	rebuildMarketplaceList();
}

void LowerThirdSettingsDialog::onMarketplaceImageReady(const QString &url, const QPixmap &pm)
{
	const QString u = url.trimmed();
	if (u.isEmpty())
		return;

	auto range = marketIconByUrl.equal_range(u);
	for (auto it = range.first; it != range.second; ++it) {
		QLabel *lab = it.value();
		if (!lab)
			continue;
		lab->setPixmap(pm);
	}
}

void LowerThirdSettingsDialog::onMarketplaceImageFailed(const QString &url, const QString &err)
{
	Q_UNUSED(url);
	Q_UNUSED(err);
}

void LowerThirdSettingsDialog::rebuildMarketplaceList()
{
    if (!marketList)
        return;

    marketList->clear();
    marketIconByUrl.clear();

    auto &api = smart_lt::api::ApiClient::instance();
    const auto items = api.lowerThirds();

    if (marketStatus) {
        if (!items.isEmpty()) {
            marketStatus->setText(tr("Custom templates library FREE & Paid."));
        } else {
            const QString err = api.lastError().trimmed();
            marketStatus->setText(err.isEmpty()
                                  ? tr("No recommendations yet.")
                                  : tr("No recommendations yet. %1").arg(err));
        }
    }

    const int iconPx = 60;

    auto trunc = [](const QString &s, int maxChars) -> QString {
	    QString t = s.trimmed();
	    if (maxChars <= 0)
		    return QString();
	    if (t.size() <= maxChars)
		    return t;
	    return t.left(maxChars - 1).trimmed() + QStringLiteral("…");
    };

    const int rowW = marketList->viewport()->width();

    for (const auto &r : items) {
	    const QString title = r.title.trimmed().isEmpty() ? r.slug : r.title.trimmed();
	    const QString desc = r.shortDescription.trimmed();
	    const QString url = r.url.trimmed();
	    const QString ico = r.iconPublicUrl.trimmed();
	    const QString dl = r.downloadUrl.trimmed();
	    const QString badge = r.badgeValue.trimmed();

	    auto *rowItem = new QListWidgetItem(marketList);
	    rowItem->setData(Qt::UserRole, url);
	    rowItem->setFlags(rowItem->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled);

	    auto *card = new QFrame(marketList);
	    card->setObjectName(QStringLiteral("oc_marketCard"));
	    card->setFrameShape(QFrame::NoFrame);
	    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	    card->setMinimumWidth(rowW);

	    card->setStyleSheet(QStringLiteral("#oc_marketCard {"
					       "  border: none;"
					       "  border-radius: 10px;"
					       "  background: rgba(255,255,255,0.04);"
					       "}"
					       "#oc_marketCard QLabel { background: transparent; }"));

	    auto *h = new QHBoxLayout(card);
	    h->setContentsMargins(8, 8, 8, 8);
	    h->setSpacing(10);

	    auto *icon = new QLabel(card);
	    icon->setFixedSize(iconPx, iconPx);
	    icon->setAlignment(Qt::AlignCenter);
	    icon->setStyleSheet(QStringLiteral("border: 1px solid rgba(255,255,255,0.10);"
					       "border-radius: 20px;"
					       "background: rgba(0,0,0,0.10);"));
	    icon->setPixmap(style()->standardIcon(QStyle::SP_FileIcon).pixmap(iconPx - 8, iconPx - 8));
	    h->addWidget(icon);

	    if (!ico.isEmpty()) {
		    marketIconByUrl.insert(ico, icon);
		    api.requestImage(ico, iconPx);
	    }

	    auto *textCol = new QVBoxLayout();
	    textCol->setContentsMargins(0, 0, 0, 0);
	    textCol->setSpacing(2);

	    auto *t = new QLabel(trunc(title, 62), card);
	    t->setTextFormat(Qt::PlainText);
	    t->setStyleSheet(QStringLiteral("font-weight: 700;"));
	    t->setWordWrap(false);
	    t->setToolTip(title);
	    textCol->addWidget(t);

	    if (!desc.isEmpty()) {
		    auto *d = new QLabel(trunc(desc, 110), card);
		    d->setTextFormat(Qt::PlainText);
		    d->setWordWrap(false);
		    d->setToolTip(desc);
		    d->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.85);"));
		    textCol->addWidget(d);
	    }

	    h->addLayout(textCol, 1);

	    auto *ctaCol = new QVBoxLayout();
	    ctaCol->setContentsMargins(0, 0, 0, 0);
	    ctaCol->setSpacing(6);

	    ctaCol->addStretch(1);

	    const int ctaWidth = 160;
	    auto *previewBtn = new QPushButton(tr("Get Template"), card);
	    previewBtn->setCursor(Qt::PointingHandCursor);
	    previewBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
	    previewBtn->setToolTip(tr("Open the template page"));
	    previewBtn->setFixedWidth(ctaWidth);
	    previewBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

	    ctaCol->addWidget(previewBtn, 0, Qt::AlignHCenter);

	    if (!badge.isEmpty()) {
		    auto *badgeLab = new QLabel(badge, card);
		    badgeLab->setAlignment(Qt::AlignCenter);
		    badgeLab->setFixedWidth(ctaWidth);
		    badgeLab->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		    badgeLab->setTextFormat(Qt::PlainText);

		    const bool isFree = (badge.compare(QStringLiteral("FREE"), Qt::CaseInsensitive) == 0);
		    badgeLab->setStyleSheet(isFree ? QStringLiteral("padding: 3px;"
								    "border-radius: 3px;"
								    "border: 1px solid rgba(34,197,94,0.55);"
								    "background: rgba(34,197,94,0.16);"
								    "color: rgba(255,255,255,0.96);"
								    "font-weight: 800;"
								    "letter-spacing: 0.4px;"
								    "min-height: 14px;")
						   : QStringLiteral("padding: 3px;"
								    "border-radius: 3px;"
								    "border: 1px solid rgba(11, 66, 245, 0.55);"
								    "background: rgba(11, 15, 245, 0.14);"
								    "color: rgba(255,255,255,0.96);"
								    "font-weight: 800;"
								    "letter-spacing: 0.4px;"
								    "min-height: 14px;"));

		    ctaCol->addWidget(badgeLab, 0, Qt::AlignHCenter);
	    }

	    ctaCol->addStretch(1);

	    h->addLayout(ctaCol);

	    connect(previewBtn, &QPushButton::clicked, this, [url]() {
		    if (!url.isEmpty())
			    QDesktopServices::openUrl(QUrl(url));
	    });

	    QString tip = title;
	    if (!desc.isEmpty())
		    tip += QStringLiteral("\n\n") + desc;
	    if (!dl.isEmpty())
		    tip += QStringLiteral("\n\n") + tr("Download: %1").arg(dl);
	    rowItem->setToolTip(tip);

	    rowItem->setSizeHint(QSize(rowW, badge.isEmpty() ? 96 : 112));

	    marketList->addItem(rowItem);
	    marketList->setItemWidget(rowItem, card);
    }
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

	QSettings s(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("MMLTech"),
		    QStringLiteral("smart-lower-thirds"));
	const int savedMode = s.value(QStringLiteral("slt/template_editor/mode"), 0).toInt();
	const QString savedPath = s.value(QStringLiteral("slt/template_editor/path"), QString()).toString();

	auto *layout = new QVBoxLayout(&dlg);

	auto *topRow = new QHBoxLayout();
	topRow->setContentsMargins(0, 0, 0, 0);

	auto *modeLbl = new QLabel(tr("Open with:"), &dlg);
	auto *modeCbx = new QComboBox(&dlg);
	modeCbx->addItem(tr("Built-in editor"), 0);
	modeCbx->addItem(tr("System default"), 1);
	modeCbx->addItem(tr("Custom app"), 2);
	const int modeIndex = qMax(0, qMin(2, savedMode));
	modeCbx->setCurrentIndex(modeIndex);

	auto *pathEdit = new QLineEdit(&dlg);
	pathEdit->setPlaceholderText(tr("Select an application (optional)"));
	pathEdit->setText(savedPath);

	auto *browseBtn = new QPushButton(tr("Browse"), &dlg);
	browseBtn->setCursor(Qt::PointingHandCursor);

	auto *openBtn = new QPushButton(tr("Open"), &dlg);
	openBtn->setCursor(Qt::PointingHandCursor);

	topRow->addWidget(modeLbl);
	topRow->addWidget(modeCbx);
	topRow->addWidget(pathEdit, 1);
	topRow->addWidget(browseBtn);
	topRow->addWidget(openBtn);
	layout->addLayout(topRow);

	auto *big = new QPlainTextEdit(&dlg);
	big->setPlainText(sourceEdit->toPlainText());
	big->setLineWrapMode(QPlainTextEdit::NoWrap);
	layout->addWidget(big, 1);

	auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
	layout->addWidget(box);

	auto syncUi = [&]() {
		const int m = modeCbx->currentData().toInt();
		const bool custom = (m == 2);
		pathEdit->setEnabled(custom);
		browseBtn->setEnabled(custom);
		openBtn->setEnabled(m != 0);
	};

	syncUi();
	connect(modeCbx, qOverload<int>(&QComboBox::currentIndexChanged), &dlg, [&](int) { syncUi(); });

	connect(modeCbx, qOverload<int>(&QComboBox::currentIndexChanged), &dlg,
		[&](int) { s.setValue(QStringLiteral("slt/template_editor/mode"), modeCbx->currentData().toInt()); });

	connect(pathEdit, &QLineEdit::textChanged, &dlg, [&](const QString &v) {
		if (modeCbx->currentData().toInt() == 2)
			s.setValue(QStringLiteral("slt/template_editor/path"), v.trimmed());
	});

	connect(browseBtn, &QPushButton::clicked, &dlg, [&]() {
		QString f = QFileDialog::getOpenFileName(&dlg, tr("Select editor application"));
		if (f.isEmpty())
			return;
		pathEdit->setText(f);
	});

	QTemporaryDir tmpDir;
	tmpDir.setAutoRemove(true);

	auto makeTmpPath = [&]() -> QString {
		QString ext = QStringLiteral(".txt");
		if (title.contains(QStringLiteral("HTML"), Qt::CaseInsensitive))
			ext = QStringLiteral(".html");
		else if (title.contains(QStringLiteral("CSS"), Qt::CaseInsensitive))
			ext = QStringLiteral(".css");
		else if (title.contains(QStringLiteral("JS"), Qt::CaseInsensitive))
			ext = QStringLiteral(".js");
		return tmpDir.path() + QLatin1Char('/') + QStringLiteral("template") + ext;
	};

	QString tmpPath;

	auto writeTmp = [&]() -> bool {
		if (tmpPath.isEmpty())
			tmpPath = makeTmpPath();
		QFile f(tmpPath);
		if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
			return false;
		const QByteArray bytes = big->toPlainText().toUtf8();
		if (f.write(bytes) != bytes.size())
			return false;
		return true;
	};

	auto readTmp = [&]() -> void {
		if (tmpPath.isEmpty())
			return;
		QFile f(tmpPath);
		if (!f.open(QIODevice::ReadOnly))
			return;
		const QString text = QString::fromUtf8(f.readAll());
		if (text != big->toPlainText()) {
			const int v = big->verticalScrollBar() ? big->verticalScrollBar()->value() : 0;
			const int h = big->horizontalScrollBar() ? big->horizontalScrollBar()->value() : 0;
			big->setPlainText(text);
			if (big->verticalScrollBar())
				big->verticalScrollBar()->setValue(v);
			if (big->horizontalScrollBar())
				big->horizontalScrollBar()->setValue(h);
		}
	};

	QFileSystemWatcher watcher(&dlg);
	QTimer poll(&dlg);
	poll.setInterval(750);

	connect(&watcher, &QFileSystemWatcher::fileChanged, &dlg, [&]() {
		readTmp();
		if (!tmpPath.isEmpty() && QFileInfo::exists(tmpPath) && !watcher.files().contains(tmpPath))
			watcher.addPath(tmpPath);
	});

	connect(&poll, &QTimer::timeout, &dlg, [&]() { readTmp(); });

	connect(openBtn, &QPushButton::clicked, &dlg, [&]() {
		const int m = modeCbx->currentData().toInt();
		const QString appPath = pathEdit->text().trimmed();

		s.setValue(QStringLiteral("slt/template_editor/mode"), m);
		s.setValue(QStringLiteral("slt/template_editor/path"), appPath);

		if (!writeTmp())
			return;

		if (!watcher.files().contains(tmpPath))
			watcher.addPath(tmpPath);
		if (!poll.isActive())
			poll.start();

		const QUrl u = QUrl::fromLocalFile(tmpPath);

		if (m == 1) {
			QDesktopServices::openUrl(u);
			return;
		}

		if (m == 2 && !appPath.isEmpty()) {
#if defined(Q_OS_MACOS)
			QString program = appPath;
			QStringList args;
			if (program.endsWith(QStringLiteral(".app"), Qt::CaseInsensitive)) {
				program = QStringLiteral("open");
				args << QStringLiteral("-a") << appPath << tmpPath;
			} else {
				args << tmpPath;
			}
			const bool ok = QProcess::startDetached(program, args);
#else
			const bool ok = QProcess::startDetached(appPath, QStringList{tmpPath});
#endif
			if (ok)
				return;

			QDesktopServices::openUrl(u);
		}
	});

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

	const QByteArray html = htmlEdit->toPlainText().toUtf8();
	const QByteArray css = cssEdit->toPlainText().toUtf8();
	const QByteArray js = jsEdit->toPlainText().toUtf8();

	QJsonObject o;
	o["title"] = QString::fromStdString(cfg->title);
	o["subtitle"] = QString::fromStdString(cfg->subtitle);
	o["title_size"] = cfg->title_size;
	o["subtitle_size"] = cfg->subtitle_size;
	o["avatar_width"] = cfg->avatar_width;
	o["avatar_height"] = cfg->avatar_height;
	o["anim_in"] = QString::fromStdString(cfg->anim_in);
	o["anim_out"] = QString::fromStdString(cfg->anim_out);

	{
		QString sin, sout;
		if (smart_lt::has_output_dir() && !cfg->anim_in_sound.empty()) {
			const QString p = QDir(QString::fromStdString(smart_lt::output_dir()))
						  .filePath(QString::fromStdString(cfg->anim_in_sound));
			const QString ext = QFileInfo(p).suffix().toLower();
			sin = ext.isEmpty() ? "soundIn" : QString("soundIn.%1").arg(ext);
		}
		if (smart_lt::has_output_dir() && !cfg->anim_out_sound.empty()) {
			const QString p = QDir(QString::fromStdString(smart_lt::output_dir()))
						  .filePath(QString::fromStdString(cfg->anim_out_sound));
			const QString ext = QFileInfo(p).suffix().toLower();
			sout = ext.isEmpty() ? "soundOut" : QString("soundOut.%1").arg(ext);
		}
		o["sound_in"] = sin;
		o["sound_out"] = sout;
	}
	o["font_family"] = QString::fromStdString(cfg->font_family);
	o["lt_position"] = QString::fromStdString(cfg->lt_position);
	o["primary_color"] = QString::fromStdString(cfg->primary_color);
	o["secondary_color"] = QString::fromStdString(cfg->secondary_color);
	o["title_color"] = QString::fromStdString(cfg->title_color);
	o["subtitle_color"] = QString::fromStdString(cfg->subtitle_color);

	// Legacy keys for backward compatibility
	o["bg_color"] = QString::fromStdString(cfg->primary_color);
	o["text_color"] = QString::fromStdString(cfg->title_color);
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

	if (ok && smart_lt::has_output_dir()) {
		const QString outDir = QString::fromStdString(smart_lt::output_dir());
		QDir dir(outDir);

		auto writeSound = [&](const std::string &fname, const char *zipBase) {
			if (fname.empty())
				return;

			const QString p = dir.filePath(QString::fromStdString(fname));
			QFile f(p);
			if (!f.open(QIODevice::ReadOnly))
				return;

			const QByteArray data = f.readAll();
			f.close();

			const QString ext = QFileInfo(p).suffix().toLower();
			const QString internal = ext.isEmpty() ? QString("%1").arg(zipBase)
							       : QString("%1.%2").arg(zipBase).arg(ext);
			ok = ok && zip_write_file(zf, internal.toUtf8().constData(), data);
		};

		writeSound(cfg->anim_in_sound, "soundIn");
		writeSound(cfg->anim_out_sound, "soundOut");
	}

	zipClose(zf, nullptr);

	if (!ok) {
		QMessageBox::warning(this, tr("Export"), tr("Failed to write one or more files into ZIP."));
		return;
	}

	QMessageBox::information(this, tr("Export"), tr("Template exported successfully."));
}

void LowerThirdSettingsDialog::onInfoClicked()
{
	QString text;
	text += tr("Placeholders you can use inside your HTML/CSS/JS templates:") + "\n\n";

	text += "  {{ID}}\n    " + tr("The unique <li> element id for this lower third (used for scoping).") + "\n\n";
	text += "  {{TITLE}}\n    " + tr("Replaced with the Title field value.") + "\n\n";
	text += "  {{SUBTITLE}}\n    " + tr("Replaced with the Subtitle field value.") + "\n\n";
	text += "  {{PROFILE_PICTURE_URL}}\n    " +
		tr("Resolved file URL for the selected profile picture (empty when none).") + "\n\n";

	text += "  {{PRIMARY_COLOR}}\n    " + tr("Primary color (hex).") + "\n\n";
	text += "  {{SECONDARY_COLOR}}\n    " + tr("Secondary color (hex).") + "\n\n";
	text += "  {{TITLE_COLOR}}\n    " + tr("Title text color (hex).") + "\n\n";
	text += "  {{SUBTITLE_COLOR}}\n    " + tr("Subtitle text color (hex).") + "\n\n";
	text += "  {{BG_COLOR}}\n    " + tr("(Legacy) Maps to {{PRIMARY_COLOR}} for older templates.") + "\n\n";
	text += "  {{TEXT_COLOR}}\n    " + tr("(Legacy) Maps to {{TITLE_COLOR}} for older templates.") + "\n\n";
	text += "  {{OPACITY}}\n    " + tr("Opacity value (0..100). Typically used with rgba/alpha in CSS.") + "\n\n";
	text += "  {{RADIUS}}\n    " + tr("Border radius value (0..100).") + "\n\n";

	text += "  {{FONT_FAMILY}}\n    " + tr("Selected font family name.") + "\n\n";
	text += "  {{TITLE_SIZE}}\n    " + tr("Title font size in pixels.") + "\n\n";
	text += "  {{SUBTITLE_SIZE}}\n    " + tr("Subtitle font size in pixels.") + "\n\n";
	text += "  {{AVATAR_WIDTH}}\n    " + tr("Avatar width in pixels.") + "\n\n";
	text += "  {{AVATAR_HEIGHT}}\n    " + tr("Avatar height in pixels.") + "\n\n";

	text += tr("Notes:") + "\n";
	text += tr("- HTML templates render inside the <li id=\"{{ID}}\"> container.") + "\n";
	text += tr("- CSS templates are auto-scoped to the lower third id when possible.") + "\n";
	text += tr("- JS templates run with a 'root' variable pointing to the <li> element.") + "\n";

	auto *dlg = new QDialog(this);
	dlg->setAttribute(Qt::WA_DeleteOnClose, true);
	dlg->setWindowTitle(tr("Template Placeholders"));
	dlg->setModal(true);
	dlg->resize(720, 560);

	auto *root = new QVBoxLayout(dlg);
	root->setContentsMargins(14, 14, 14, 14);
	root->setSpacing(10);

	auto *lead = new QLabel(
		tr("Copy / paste placeholders into your templates. They are replaced at runtime per lower third."),
		dlg);
	lead->setWordWrap(true);
	lead->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.80);"));
	root->addWidget(lead);

	auto *box = new QPlainTextEdit(dlg);
	box->setPlainText(text);
	box->setReadOnly(true);
	box->setLineWrapMode(QPlainTextEdit::NoWrap);
	box->setStyleSheet(QStringLiteral("QPlainTextEdit {"
					  "  background: rgba(255,255,255,0.06);"
					  "  border: 1px solid rgba(255,255,255,0.10);"
					  "  border-radius: 10px;"
					  "  padding: 10px;"
					  "  font-family: 'Consolas','Courier New',monospace;"
					  "  font-size: 12px;"
					  "}"));
	root->addWidget(box, 1);

	auto *btnRow = new QHBoxLayout();
	btnRow->setContentsMargins(0, 0, 0, 0);
	btnRow->setSpacing(8);

	auto *copyBtn = new QPushButton(tr("Copy All"), dlg);
	copyBtn->setCursor(Qt::PointingHandCursor);
	copyBtn->setMinimumHeight(30);
	copyBtn->setStyleSheet(QStringLiteral("QPushButton {"
					      "  background: rgba(255,255,255,0.10);"
					      "  border: 1px solid rgba(255,255,255,0.12);"
					      "  border-radius: 10px;"
					      "  padding: 6px 12px;"
					      "  font-weight: 700;"
					      "}"
					      "QPushButton:hover { background: rgba(255,255,255,0.14); }"
					      "QPushButton:pressed { background: rgba(255,255,255,0.18); }"));
	QObject::connect(copyBtn, &QPushButton::clicked, dlg,
			 [box]() { QApplication::clipboard()->setText(box->toPlainText()); });

	btnRow->addWidget(copyBtn);
	btnRow->addStretch(1);

	auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
	QObject::connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::close);
	btnRow->addWidget(bb);

	root->addLayout(btnRow);

	dlg->setStyleSheet(QStringLiteral("QDialog { background: #141416; color: white; }"));

	dlg->show();
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

	QString htmlPath, cssPath, jsPath, jsonPath, profilePicPath, soundInFilePath, soundOutFilePath;
	if (!unzip_to_dir(zipPath, tempDir.path(), htmlPath, cssPath, jsPath, jsonPath, profilePicPath, soundInFilePath,
			  soundOutFilePath)) {
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
	cfg->title_size = obj.value("title_size").toInt(cfg->title_size);
	cfg->subtitle_size = obj.value("subtitle_size").toInt(cfg->subtitle_size);
	cfg->title_size = std::max(6, std::min(200, cfg->title_size));
	cfg->subtitle_size = std::max(6, std::min(200, cfg->subtitle_size));
	cfg->avatar_width = obj.value("avatar_width").toInt(cfg->avatar_width);
	cfg->avatar_height = obj.value("avatar_height").toInt(cfg->avatar_height);
	cfg->avatar_width = std::max(16, std::min(512, cfg->avatar_width));
	cfg->avatar_height = std::max(16, std::min(512, cfg->avatar_height));
	cfg->anim_in = obj.value("anim_in").toString().toStdString();
	cfg->anim_out = obj.value("anim_out").toString().toStdString();
	{
		const QString sin = obj.value("sound_in").toString();
		const QString sout = obj.value("sound_out").toString();
		const QString legacyIn = obj.value("anim_in_sound").toString();
		const QString legacyOut = obj.value("anim_out_sound").toString();
		Q_UNUSED(sin);
		Q_UNUSED(sout);
		Q_UNUSED(legacyIn);
		Q_UNUSED(legacyOut);
	}

	cfg->font_family = obj.value("font_family").toString().toStdString();
	cfg->lt_position = obj.value("lt_position").toString().toStdString();

	cfg->primary_color = obj.value("primary_color").toString().toStdString();
	cfg->secondary_color = obj.value("secondary_color").toString().toStdString();
	cfg->title_color = obj.value("title_color").toString().toStdString();
	cfg->subtitle_color = obj.value("subtitle_color").toString().toStdString();

	if (cfg->primary_color.empty()) cfg->primary_color = obj.value("bg_color").toString().toStdString();
	if (cfg->title_color.empty()) cfg->title_color = obj.value("text_color").toString().toStdString();
	if (cfg->secondary_color.empty()) cfg->secondary_color = cfg->primary_color;
	if (cfg->subtitle_color.empty()) cfg->subtitle_color = cfg->title_color;
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

			if (smart_lt::has_output_dir()) {
				const QString outDir = QString::fromStdString(smart_lt::output_dir());
				if (!outDir.isEmpty()) {
					QDir dir(outDir);

					auto importSound = [&](const QString &srcPath, bool isIn) {
						if (srcPath.isEmpty())
							return;

						std::string &field = isIn ? cfg->anim_in_sound : cfg->anim_out_sound;
						if (!field.empty()) {
							const QString oldPath =
								dir.filePath(QString::fromStdString(field));
							if (QFile::exists(oldPath))
								QFile::remove(oldPath);
							field.clear();
						}

						const QString ext = QFileInfo(srcPath).suffix().toLower();
						const QString base = isIn ? "soundIn" : "soundOut";
						const QString newName =
							ext.isEmpty() ? QString("%1_%2")
										.arg(QString::fromStdString(cfg->id))
										.arg(base)
								      : QString("%1_%2.%3")
										.arg(QString::fromStdString(cfg->id))
										.arg(base)
										.arg(ext);

						const QString dest = dir.filePath(newName);
						QFile::remove(dest);
						if (QFile::copy(srcPath, dest)) {
							field = newName.toStdString();
						}
					};

					importSound(soundInFilePath, true);
					importSound(soundOutFilePath, false);
				}
			}
		}
	}

	smart_lt::save_state_json();

	loadFromState();

	QMessageBox::information(this, tr("Imported"),
				 tr("Template imported successfully. Click 'Save & Apply' to rebuild files."));
}

} // namespace smart_lt::ui