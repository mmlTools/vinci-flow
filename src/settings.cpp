// settings.cpp
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
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStyle>
#include <QTabWidget>
#include <QScrollArea>
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
	addNavItem(tr("Templates"));

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
	QVBoxLayout *templatesPageLayout = nullptr;

	auto *contentPageSa = makeScrollPage(contentPageLayout);
	auto *stylePageSa = makeScrollPage(stylePageLayout);
	auto *templatesPageSa = makeScrollPage(templatesPageLayout);

	stack->addWidget(contentPageSa);
	stack->addWidget(stylePageSa);
	stack->addWidget(templatesPageSa);

	body->addWidget(nav);
	body->addWidget(stack, 1);

	root->addLayout(body, /*stretch*/ 1);

	nav->setCurrentRow(0);
	stack->setCurrentIndex(0);

	connect(nav, &QListWidget::currentRowChanged, this, [stack](int row) {
		if (row >= 0 && row < stack->count())
			stack->setCurrentIndex(row);
	});

	// ------------------------------------------------------------
	// Content & Media page
	// ------------------------------------------------------------
	{
		auto *contentBox = new QGroupBox(tr("Content && Media"), this);
		auto *g = new QGridLayout(contentBox);
		g->setSpacing(8);

		int row = 0;

		// Row 0: Label
		g->addWidget(new QLabel(tr("Dock Label:"), this), row, 0);
		labelEdit = new QLineEdit(this);
		labelEdit->setToolTip(tr("Display-only label used in the dock list"));
		g->addWidget(labelEdit, row, 1, 1, 3);

		row++;
		// Row 1: Title
		g->addWidget(new QLabel(tr("Title:"), this), row, 0);
		titleEdit = new QLineEdit(this);
		g->addWidget(titleEdit, row, 1, 1, 3);

		row++;
		// Row 2: Subtitle
		g->addWidget(new QLabel(tr("Subtitle:"), this), row, 0);
		subtitleEdit = new QLineEdit(this);
		g->addWidget(subtitleEdit, row, 1, 1, 3);

		row++;
		// Row 3: Profile Picture
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

		contentPageLayout->addWidget(contentBox);

		connect(clearHotkeyBtn, &QPushButton::clicked, this,
			[this]() { hotkeyEdit->setKeySequence(QKeySequence()); });

		connect(browseProfilePictureBtn, &QPushButton::clicked, this,
			&LowerThirdSettingsDialog::onBrowseProfilePicture);
		connect(deleteProfilePictureBtn, &QPushButton::clicked, this,
			&LowerThirdSettingsDialog::onDeleteProfilePicture);

		auto *marketBox = new QGroupBox(tr("Lower Thirds Library"), this);
		auto *mv = new QVBoxLayout(marketBox);
		mv->setSpacing(8);

		auto *hero = new QFrame(this);
		hero->setObjectName(QStringLiteral("oc_marketHero"));
		hero->setFrameShape(QFrame::NoFrame);
		hero->setStyleSheet(QStringLiteral(
			"#oc_marketHero {"
			"  border: 1px solid rgba(255,255,255,0.10);"
			"  border-radius: 10px;"
			"  background: rgba(255,255,255,0.04);"
			"  padding: 10px;"
			"}"
		));

		auto *heroRow = new QHBoxLayout(hero);
		heroRow->setContentsMargins(8, 8, 8, 8);
		heroRow->setSpacing(10);

		auto *heroIcon = new QLabel(this);
		heroIcon->setFixedSize(40, 40);
		heroIcon->setPixmap(style()->standardIcon(QStyle::SP_DirOpenIcon).pixmap(36, 36));
		heroIcon->setAlignment(Qt::AlignCenter);
		heroRow->addWidget(heroIcon);

		auto *heroTextCol = new QVBoxLayout();
		heroTextCol->setContentsMargins(0, 0, 0, 0);
		heroTextCol->setSpacing(2);

		auto *heroTitle = new QLabel(tr("Get new Lower Thirds instantly"), this);
		heroTitle->setTextFormat(Qt::PlainText);
		heroTitle->setStyleSheet(QStringLiteral("font-weight: 700;"));
		heroTextCol->addWidget(heroTitle);

		marketStatus = new QLabel(tr("Loading recommendations…"), this);
		marketStatus->setWordWrap(true);
		marketStatus->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.85);"));
		heroTextCol->addWidget(marketStatus);
		heroRow->addLayout(heroTextCol, 1);

		seeAllLowerThirdsBtn = new QPushButton(tr("See all lower thirds"), this);
		seeAllLowerThirdsBtn->setCursor(Qt::PointingHandCursor);
		seeAllLowerThirdsBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
		seeAllLowerThirdsBtn->setToolTip(tr("Open the Lower Thirds marketplace in your browser"));
		heroRow->addWidget(seeAllLowerThirdsBtn);

		mv->addWidget(hero);

		marketList = new QListWidget(this);
		marketList->setSelectionMode(QAbstractItemView::SingleSelection);
		marketList->setUniformItemSizes(false);
		marketList->setMinimumHeight(280);
		marketList->setSpacing(6);
		marketList->setStyleSheet(QStringLiteral(
			"QListWidget {"
			"  border: 1px solid rgba(255,255,255,0.10);"
			"  border-radius: 10px;"
			"  background: rgba(255,255,255,0.02);"
			"  padding: 6px;"
			"}"
			"QListWidget::item { padding: 0px; margin: 0px; }"
			"QListWidget::item:selected { background: transparent; }"
		));
		mv->addWidget(marketList, 1);

		contentPageLayout->addWidget(marketBox);

		connect(marketList, &QListWidget::itemActivated, this, [](QListWidgetItem *it) {
			if (!it)
				return;
			const QString url = it->data(Qt::UserRole).toString();
			if (!url.isEmpty())
				QDesktopServices::openUrl(QUrl(url));
		});

		connect(seeAllLowerThirdsBtn, &QPushButton::clicked, this, []() {
			QDesktopServices::openUrl(QUrl(QStringLiteral("https://obscountdown.com/?type=lower-thirds-templates")));
		});

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

		contentPageLayout->addStretch(1);
	}

	// ------------------------------------------------------------
	// Style page
	// ------------------------------------------------------------
	{
		auto *styleBox = new QGroupBox(tr("Style"), this);
		auto *g = new QGridLayout(styleBox);
		g->setSpacing(8);

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
		g->addWidget(new QLabel(tr("Background:"), this), row, 0);
		bgColorBtn = new QPushButton(tr("Pick"), this);
		g->addWidget(bgColorBtn, row, 1);

		row++;
		g->addWidget(new QLabel(tr("Text color:"), this), row, 0);
		textColorBtn = new QPushButton(tr("Pick"), this);
		g->addWidget(textColorBtn, row, 1);

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

		stylePageLayout->addWidget(styleBox);

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

		stylePageLayout->addStretch(1);
	}

	// ------------------------------------------------------------
	// Templates page
	// ------------------------------------------------------------
	{
		auto *tplCard = new QGroupBox(tr("Templates"), this);
		auto *tplLayout = new QVBoxLayout(tplCard);
		tplLayout->setSpacing(8);
		tplLayout->setContentsMargins(8, 8, 8, 8);

		tplTabs = new QTabWidget(tplCard);
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

		tplLayout->addWidget(tplTabs, /*stretch*/ 1);

		// ------------------------------------------------------------
		// Placeholders panel BELOW the editors (copy/paste friendly)
		// ------------------------------------------------------------
		auto *phBox = new QGroupBox(tr("Template Placeholders"), tplCard);
		auto *phLayout = new QVBoxLayout(phBox);
		phLayout->setContentsMargins(8, 8, 8, 8);
		phLayout->setSpacing(6);

		auto *phTop = new QHBoxLayout();
		phTop->setContentsMargins(0, 0, 0, 0);
		phTop->addStretch(1);

		phLayout->addLayout(phTop);

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
				     "Styling\n"
				     "  {{BG_COLOR}}         Background color\n"
				     "  {{TEXT_COLOR}}       Text color\n"
				     "  {{OPACITY}}          Opacity (0-100 or normalized based on your templater)\n"
				     "  {{RADIUS}}           Radius (0-100)\n"
				     "\n"
				     "Behavior\n"
				     "  {{ANIM_IN}}          Animate.css in class\n"
				     "  {{ANIM_OUT}}         Animate.css out class\n"
				     "  {{POSITION}}         Screen position key\n");

		phLayout->addWidget(phText);

		tplLayout->addWidget(phBox);

		templatesPageLayout->addWidget(tplCard);
		templatesPageLayout->addStretch(1);
	}

	// ------------------------------------------------------------
	// Footer: Import/Export (left) + Cancel / Save&Apply (right)
	// ------------------------------------------------------------
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

		connect(buttonBox, &QDialogButtonBox::rejected, this, &LowerThirdSettingsDialog::reject, Qt::UniqueConnection);
		connect(applyBtn, &QPushButton::clicked, this, &LowerThirdSettingsDialog::onSaveAndApply, Qt::UniqueConnection);
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

	smart_lt::save_state_json();
}

void LowerThirdSettingsDialog::onSaveAndApply()
{
	saveToState();

	smart_lt::rebuild_and_swap();

	QMessageBox::information(this, tr("Saved"), tr("Lower third settings were saved and applied."));
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

	const QMessageBox::StandardButton btn = QMessageBox::question(
		this, tr("Remove profile picture"), tr("Delete the current profile picture?"),
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
			marketStatus->setText(err.isEmpty() ? tr("No recommendations yet.")
					      : tr("No recommendations yet. %1").arg(err));
		}
	}

	const int iconPx = 44;
	auto trunc = [](const QString &s, int maxChars) -> QString {
		QString t = s.trimmed();
		if (maxChars <= 0)
			return QString();
		if (t.size() <= maxChars)
			return t;
		return t.left(maxChars - 1).trimmed() + QStringLiteral("…");
	};

	for (const auto &r : items) {
		const QString title = r.title.trimmed().isEmpty() ? r.slug : r.title.trimmed();
		const QString desc  = r.shortDescription.trimmed();
		const QString url   = r.url.trimmed();
		const QString ico   = r.iconPublicUrl.trimmed();
		const QString dl    = r.downloadUrl.trimmed();

		auto *rowItem = new QListWidgetItem(marketList);
		rowItem->setData(Qt::UserRole, url);
		rowItem->setFlags(rowItem->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled);

		auto *card = new QFrame(this);
		card->setObjectName(QStringLiteral("oc_marketCard"));
		card->setFrameShape(QFrame::NoFrame);
		card->setStyleSheet(QStringLiteral(
			"#oc_marketCard {"
			"  border: 1px solid rgba(255,255,255,0.10);"
			"  border-radius: 10px;"
			"  background: rgba(255,255,255,0.03);"
			"  padding: 8px;"
			"}"
			"#oc_marketCard QLabel { background: transparent; }"
		));

		auto *h = new QHBoxLayout(card);
		h->setContentsMargins(8, 8, 8, 8);
		h->setSpacing(10);

		auto *icon = new QLabel(this);
		icon->setFixedSize(iconPx, iconPx);
		icon->setAlignment(Qt::AlignCenter);
		icon->setStyleSheet(QStringLiteral(
			"border: 1px solid rgba(255,255,255,0.10);"
			"border-radius: 10px;"
			"background: rgba(0,0,0,0.10);"
		));
		icon->setPixmap(style()->standardIcon(QStyle::SP_FileIcon).pixmap(iconPx - 8, iconPx - 8));
		h->addWidget(icon);

		if (!ico.isEmpty()) {
			marketIconByUrl.insert(ico, icon);
			api.requestImage(ico, iconPx);
		}

		auto *textCol = new QVBoxLayout();
		textCol->setContentsMargins(0, 0, 0, 0);
		textCol->setSpacing(2);

		auto *t = new QLabel(trunc(title, 62), this);
		t->setTextFormat(Qt::PlainText);
		t->setStyleSheet(QStringLiteral("font-weight: 700;"));
		t->setWordWrap(false);
		t->setToolTip(title);
		textCol->addWidget(t);

		if (!desc.isEmpty()) {
			auto *d = new QLabel(trunc(desc, 110), this);
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

		auto *previewBtn = new QPushButton(tr("Preview"), this);
		previewBtn->setCursor(Qt::PointingHandCursor);
		previewBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
		previewBtn->setToolTip(tr("Open the template page"));
		ctaCol->addWidget(previewBtn);

		QPushButton *downloadBtn = nullptr;
		if (!dl.isEmpty()) {
			downloadBtn = new QPushButton(tr("Download"), this);
			downloadBtn->setCursor(Qt::PointingHandCursor);
			downloadBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
			downloadBtn->setToolTip(tr("Open download link"));
			ctaCol->addWidget(downloadBtn);
		}

		ctaCol->addStretch(1);
		h->addLayout(ctaCol);

		connect(previewBtn, &QPushButton::clicked, this, [url]() {
			if (!url.isEmpty())
				QDesktopServices::openUrl(QUrl(url));
		});
		if (downloadBtn) {
			connect(downloadBtn, &QPushButton::clicked, this, [dl]() {
				if (!dl.isEmpty())
					QDesktopServices::openUrl(QUrl(dl));
			});
		}

		QString tip = title;
		if (!desc.isEmpty())
			tip += QStringLiteral("\n\n") + desc;
		if (!dl.isEmpty())
			tip += QStringLiteral("\n\n") + tr("Download: %1").arg(dl);
		rowItem->setToolTip(tip);

		rowItem->setSizeHint(QSize(0, 96));
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

	const QByteArray html = htmlEdit->toPlainText().toUtf8();
	const QByteArray css = cssEdit->toPlainText().toUtf8();
	const QByteArray js = jsEdit->toPlainText().toUtf8();

	// template.json
	QJsonObject o;
	o["title"] = QString::fromStdString(cfg->title);
	o["subtitle"] = QString::fromStdString(cfg->subtitle);
	o["title_size"] = cfg->title_size;
	o["subtitle_size"] = cfg->subtitle_size;
	o["avatar_width"] = cfg->avatar_width;
	o["avatar_height"] = cfg->avatar_height;
	o["anim_in"] = QString::fromStdString(cfg->anim_in);
	o["anim_out"] = QString::fromStdString(cfg->anim_out);
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

void LowerThirdSettingsDialog::onInfoClicked()
{
	QString text;
	text += tr("Placeholders you can use inside your HTML/CSS/JS templates:") + "\n\n";

	text += "  {{ID}}\n    " + tr("The unique <li> element id for this lower third (used for scoping).") + "\n\n";
	text += "  {{TITLE}}\n    " + tr("Replaced with the Title field value.") + "\n\n";
	text += "  {{SUBTITLE}}\n    " + tr("Replaced with the Subtitle field value.") + "\n\n";
	text += "  {{PROFILE_PICTURE_URL}}\n    " +
		tr("Resolved file URL for the selected profile picture (empty when none).") + "\n\n";

	text += "  {{BG_COLOR}}\n    " + tr("Background color (hex).") + "\n\n";
	text += "  {{TEXT_COLOR}}\n    " + tr("Primary text color (hex).") + "\n\n";
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

	QMessageBox::information(this, tr("Template Placeholders"), text);
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

	loadFromState();

	QMessageBox::information(this, tr("Imported"),
				 tr("Template imported successfully. Click 'Save & Apply' to rebuild files."));
}

} // namespace smart_lt::ui
