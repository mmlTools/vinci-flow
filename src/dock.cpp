#define LOG_TAG "[" PLUGIN_NAME "][dock]"
#include "dock.hpp"

#include "core.hpp"
#include "settings.hpp"
#include "widget.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QCheckBox>
#include <QLineEdit>
#include <QFileDialog>
#include <QStyle>
#include <QFrame>
#include <QEvent>
#include <QShortcut>
#include <QMouseEvent>
#include <QDir>
#include <QPixmap>
#include <QVariant>
#include <QSizePolicy>
#include <QToolButton>

static QWidget *g_dockWidget = nullptr;

// ------------------------------------------------------------

LowerThirdDock::LowerThirdDock(QWidget *parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("LowerThirdDock"));
	setAttribute(Qt::WA_StyledBackground, true);

	setStyleSheet(R"(
	#LowerThirdDock {
	    background: rgba(39, 42, 51, 1.0);
	}
	QFrame#sltRowFrame {
	    border: 1px solid rgba(255,255,255,40);
	    border-radius: 4px;
	    padding: 4px 6px;
	    background: transparent;
	}
	QFrame#sltRowFrame:hover {
	    background: rgba(255,255,255,0.04);
	}
	QFrame#sltRowFrame[sltActive="true"] {
	    background: rgba(88,166,255,0.16);
	    border: 1px solid rgba(88,166,255,0.9);
	}
	QLabel#sltRowLabel {
	    color: #f0f6fc;
	    font-weight: 500;
	}
	QLabel#sltRowThumbnail {
	    border-radius: 16px;
	    background: rgba(0,0,0,0.35);
	}
	QScrollArea#LowerThirdContent QPushButton {
	    border: none;
	    background: transparent;
	    padding: 2px;
	}
	QScrollArea#LowerThirdContent QPushButton:hover {
	    background: rgba(255,255,255,0.06);
	    border-radius: 3px;
	}

	/* Carousel */
	QToolButton[sltCarousel="true"] {
	  background: rgba(255,255,255,0.06);
	  border: 1px solid rgba(255,255,255,0.10);
	  border-radius: 10px;
	  padding: 6px 10px;
	  color: #f0f6fc;
	}
	QToolButton[sltCarousel="true"]:hover {
	  background: rgba(255,255,255,0.10);
	}
	QToolButton[sltCarousel="true"][sltActive="true"] {
	  background: rgba(88,166,255,0.18);
	  border: 1px solid rgba(88,166,255,0.95);
	}
	)");

	auto *rootLayout = new QVBoxLayout(this);
	rootLayout->setContentsMargins(8, 8, 8, 8);
	rootLayout->setSpacing(6);

	auto *st = style();

	// ─────────────────────────────────────────────────────────────
	// Row 1: Browse output folder + Add Browser Source (globe icon)
	// ─────────────────────────────────────────────────────────────
	{
		auto *row = new QHBoxLayout();
		row->setSpacing(6);

		auto *lbl = new QLabel(tr("Resources:"), this);

		outputPathEdit = new QLineEdit(this);
		outputPathEdit->setReadOnly(true);

		outputBrowseBtn = new QPushButton(this);
		outputBrowseBtn->setCursor(Qt::PointingHandCursor);
		outputBrowseBtn->setToolTip(tr("Select output folder"));
		outputBrowseBtn->setFlat(true);
		outputBrowseBtn->setIcon(st->standardIcon(QStyle::SP_DirOpenIcon));

		ensureSourceBtn = new QPushButton(this);
		ensureSourceBtn->setCursor(Qt::PointingHandCursor);
		ensureSourceBtn->setToolTip(tr("Add Browser Source to current scene"));
		ensureSourceBtn->setFlat(true);

		// Web globe icon (theme) with fallback
		QIcon globe = QIcon::fromTheme(QStringLiteral("internet-web-browser"));
		if (globe.isNull())
			globe = QIcon::fromTheme(QStringLiteral("applications-internet"));
		if (globe.isNull())
			globe = QIcon::fromTheme(QStringLiteral("network-workgroup"));
		if (globe.isNull())
			globe = st->standardIcon(QStyle::SP_BrowserReload);
		ensureSourceBtn->setIcon(globe);

		row->addWidget(lbl);
		row->addWidget(outputPathEdit, 1);
		row->addWidget(outputBrowseBtn);
		row->addWidget(ensureSourceBtn);

		rootLayout->addLayout(row);

		connect(outputBrowseBtn, &QPushButton::clicked, this, &LowerThirdDock::onBrowseOutputFolder);
		connect(ensureSourceBtn, &QPushButton::clicked, this, &LowerThirdDock::onEnsureBrowserSourceClicked);
	}

	// ─────────────────────────────────────────────────────────────
	// List (scroll area)
	// ─────────────────────────────────────────────────────────────
	scrollArea = new QScrollArea(this);
	scrollArea->setObjectName(QStringLiteral("LowerThirdContent"));
	scrollArea->setStyleSheet(QStringLiteral("#LowerThirdContent {"
						 "  background-color: rgba(0, 0, 0, 0.25);"
						 "  border: 1px solid rgba(7, 7, 7, 0.1);"
						 "  border-radius: 4px;"
						 "  margin-top: 10px;"
						 "  padding-left: 10px;"
						 "}"));
	scrollArea->setWidgetResizable(true);

	listContainer = new QWidget(scrollArea);
	listLayout = new QVBoxLayout(listContainer);
	listLayout->setContentsMargins(8, 8, 8, 8);
	listLayout->setSpacing(6);
	listLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

	scrollArea->setWidget(listContainer);
	rootLayout->addWidget(scrollArea);

	// ─────────────────────────────────────────────────────────────
	// Bottom-right "+" Add button (below scroll area)
	// ─────────────────────────────────────────────────────────────
	{
		auto *row = new QHBoxLayout();
		row->setSpacing(6);

		row->addStretch(1);

		addBtn = new QPushButton(this);
		addBtn->setCursor(Qt::PointingHandCursor);
		addBtn->setToolTip(tr("Add new lower third"));
		addBtn->setFlat(true);

		// "+" icon (theme) with fallbacks
		QIcon plus = QIcon::fromTheme(QStringLiteral("list-add"));
		if (plus.isNull())
			plus = QIcon::fromTheme(QStringLiteral("add"));
		if (plus.isNull())
			plus = st->standardIcon(QStyle::SP_DialogYesButton);
		addBtn->setIcon(plus);

		row->addWidget(addBtn);
		rootLayout->addLayout(row);

		connect(addBtn, &QPushButton::clicked, this, &LowerThirdDock::onAddLowerThird);
	}

	// Your existing widget carousel (Ko-fi/etc)
	rootLayout->addWidget(create_widget_carousel(this));

	// init button state + current path display
	if (smart_lt::has_output_dir())
		outputPathEdit->setText(QString::fromStdString(smart_lt::output_dir()));
	else
		outputPathEdit->clear();

	const bool hasDir = smart_lt::has_output_dir();
	addBtn->setEnabled(hasDir);
	ensureSourceBtn->setEnabled(hasDir);
}

bool LowerThirdDock::init()
{
	if (smart_lt::all().empty())
		smart_lt::add_default_lower_third();

	rebuildList();

	// Ensure baseline artifacts exist
	if (smart_lt::has_output_dir())
		smart_lt::ensure_output_artifacts_exist();

	return true;
}

void LowerThirdDock::updateFromState()
{
	updateRowActiveStyles();
	updateCarouselActiveStyles();
}

// ------------------------------------------------------------
// event filter: click row toggles checkbox (except buttons)
// ------------------------------------------------------------

bool LowerThirdDock::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::MouseButtonPress) {
		auto *me = static_cast<QMouseEvent *>(event);
		if (me->button() == Qt::LeftButton) {
			for (auto &r : rows) {
				if (watched == r.row) {
					if (QWidget *rowWidget = r.row) {
						QWidget *child = rowWidget->childAt(me->pos());
						if (child && qobject_cast<QPushButton *>(child))
							return QWidget::eventFilter(watched, event);
					}
				}

				if (watched == r.row || watched == r.labelLbl || watched == r.thumbnailLbl) {
					if (r.visibleCheck) {
						r.visibleCheck->setChecked(!r.visibleCheck->isChecked());
						return true;
					}
				}
			}
		}
	}

	return QWidget::eventFilter(watched, event);
}

// ------------------------------------------------------------
// shortcuts
// ------------------------------------------------------------

void LowerThirdDock::clearShortcuts()
{
	for (auto *sc : shortcuts_) {
		if (sc)
			sc->deleteLater();
	}
	shortcuts_.clear();
}

void LowerThirdDock::rebuildShortcuts()
{
	clearShortcuts();

	auto &items = smart_lt::all();
	for (const auto &cfg : items) {
		if (cfg.hotkey.empty())
			continue;

		const QString seqStr = QString::fromStdString(cfg.hotkey).trimmed();
		if (seqStr.isEmpty())
			continue;

		QKeySequence seq(seqStr);
		if (seq.isEmpty())
			continue;

		auto *sc = new QShortcut(seq, this);
		sc->setContext(Qt::ApplicationShortcut);
		shortcuts_.push_back(sc);

		const QString id = QString::fromStdString(cfg.id);

		connect(sc, &QShortcut::activated, this, [this, id]() {
			// Hotkeys toggle the item (and hide others for a broadcast-safe behavior)
			handleToggleVisible(id, /*hideOthers*/ true);
		});
	}
}

// ------------------------------------------------------------
// Carousel (optional; safe no-op if you keep it disabled)
// ------------------------------------------------------------

void LowerThirdDock::rebuildCarousel()
{
	if (!carouselContainer || !carouselLayout)
		return;

	for (auto *b : carouselButtons) {
		if (b)
			b->deleteLater();
	}
	carouselButtons.clear();

	auto &items = smart_lt::all();
	const QString outDir = QString::fromStdString(smart_lt::output_dir());

	for (const auto &cfg : items) {
		const QString id = QString::fromStdString(cfg.id);

		auto *btn = new QToolButton(carouselContainer);
		btn->setProperty("sltCarousel", true);
		btn->setProperty("sltActive", cfg.visible);
		btn->setProperty("sltId", id);
		btn->setCursor(Qt::PointingHandCursor);
		btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		btn->setText(QString::fromStdString(cfg.title));
		btn->setIconSize(QSize(28, 28));

		QPixmap px;
		if (!cfg.profile_picture.empty() && !outDir.isEmpty()) {
			const QString imgPath = QDir(outDir).filePath(QString::fromStdString(cfg.profile_picture));
			px.load(imgPath);
		}

		if (!px.isNull())
			btn->setIcon(QIcon(px));
		else
			btn->setIcon(style()->standardIcon(QStyle::SP_FileIcon));

		connect(btn, &QToolButton::clicked, this, [this, id]() { handleToggleVisible(id, true); });

		carouselLayout->addWidget(btn);
		carouselButtons.push_back(btn);
	}

	carouselLayout->addStretch(1);
	updateCarouselActiveStyles();
}

void LowerThirdDock::updateCarouselActiveStyles()
{
	if (carouselButtons.isEmpty())
		return;

	auto &items = smart_lt::all();

	auto isVisibleById = [&](const QString &id) -> bool {
		for (const auto &cfg : items) {
			if (id == QString::fromStdString(cfg.id))
				return cfg.visible;
		}
		return false;
	};

	for (auto *btn : carouselButtons) {
		if (!btn)
			continue;

		const QString id = btn->property("sltId").toString();
		const bool active = (!id.isEmpty()) ? isVisibleById(id) : false;

		btn->setProperty("sltActive", active);
		btn->style()->unpolish(btn);
		btn->style()->polish(btn);
		btn->update();
	}
}

// ------------------------------------------------------------
// handlers
// ------------------------------------------------------------

void LowerThirdDock::onBrowseOutputFolder()
{
	const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Folder"));
	if (dir.isEmpty())
		return;

	smart_lt::set_output_dir_and_load(dir.toStdString());

	outputPathEdit->setText(dir);

	const bool hasDir = smart_lt::has_output_dir();
	addBtn->setEnabled(hasDir);
	ensureSourceBtn->setEnabled(hasDir);

	rebuildList();
	emit requestSave();
}

void LowerThirdDock::onAddLowerThird()
{
	const QString newId = QString::fromStdString(smart_lt::add_default_lower_third());
	LOGI("Added lower third '%s'", newId.toUtf8().constData());

	rebuildList();
	emit requestSave();
}

void LowerThirdDock::onEnsureBrowserSourceClicked()
{
	if (!smart_lt::has_output_dir()) {
		LOGW("Cannot add browser source: output dir not set");
		return;
	}

	// Ensure artifacts exist (HTML may be missing)
	smart_lt::ensure_output_artifacts_exist();

	const std::string &htmlPath = smart_lt::index_html_path();
	if (htmlPath.empty()) {
		LOGW("index_html_path empty");
		return;
	}

	obs_source_t *curSceneSrc = obs_frontend_get_current_scene();
	if (!curSceneSrc) {
		LOGW("No current scene");
		return;
	}

	obs_scene_t *scene = obs_scene_from_source(curSceneSrc);
	if (!scene) {
		LOGW("Current scene source is not a scene");
		obs_source_release(curSceneSrc);
		return;
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", htmlPath.c_str());
	obs_data_set_bool(settings, "smart_lt_managed", true);

	obs_video_info vi;
	if (obs_get_video_info(&vi) == 0) {
		obs_data_set_int(settings, "width", vi.base_width);
		obs_data_set_int(settings, "height", vi.base_height);
	} else {
		obs_data_set_int(settings, "width", sltBrowserWidth);
		obs_data_set_int(settings, "height", sltBrowserHeight);
	}

	obs_data_set_bool(settings, "shutdown", false);

	obs_source_t *browser = obs_source_create(sltBrowserSourceId, sltBrowserSourceName, settings, nullptr);
	obs_data_release(settings);

	if (!browser) {
		LOGW("Failed to create browser source");
		obs_source_release(curSceneSrc);
		return;
	}

	obs_scene_add(scene, browser);

	LOGI("Added browser source '%s' with local_file '%s'", sltBrowserSourceName, htmlPath.c_str());

	obs_source_release(browser);
	obs_source_release(curSceneSrc);
}

// ------------------------------------------------------------
// list rendering
// ------------------------------------------------------------

void LowerThirdDock::rebuildList()
{
	for (auto &row : rows) {
		if (row.row)
			row.row->deleteLater();
	}
	rows.clear();

	auto &items = smart_lt::all();
	const QString outDir = QString::fromStdString(smart_lt::output_dir());

	for (const auto &cfg : items) {
		LowerThirdRowUi rowUi;
		rowUi.id = QString::fromStdString(cfg.id);

		auto *rowFrame = new QFrame(listContainer);
		rowFrame->setObjectName(QStringLiteral("sltRowFrame"));
		rowFrame->setProperty("sltActive", QVariant(cfg.visible));
		rowFrame->setCursor(Qt::PointingHandCursor);

		auto *h = new QHBoxLayout(rowFrame);
		h->setContentsMargins(8, 4, 8, 4);
		h->setSpacing(6);

		auto *visible = new QCheckBox(rowFrame);
		visible->setChecked(cfg.visible);
		visible->setFocusPolicy(Qt::NoFocus);
		visible->setAttribute(Qt::WA_TransparentForMouseEvents, true);
		visible->setStyleSheet("QCheckBox::indicator { width: 0px; height: 0px; margin: 0; padding: 0; }");
		h->addWidget(visible);
		rowUi.visibleCheck = visible;

		auto *thumb = new QLabel(rowFrame);
		thumb->setObjectName(QStringLiteral("sltRowThumbnail"));
		thumb->setFixedSize(32, 32);
		thumb->setScaledContents(true);

		bool hasThumb = false;
		if (!cfg.profile_picture.empty() && !outDir.isEmpty()) {
			const QString imgPath = QDir(outDir).filePath(QString::fromStdString(cfg.profile_picture));
			QPixmap px(imgPath);
			if (!px.isNull()) {
				thumb->setPixmap(
					px.scaled(32, 32, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
				hasThumb = true;
			}
		}
		thumb->setVisible(hasThumb);
		h->addWidget(thumb);
		rowUi.thumbnailLbl = thumb;

		auto *label = new QLabel(QString::fromStdString(cfg.title), rowFrame);
		label->setObjectName(QStringLiteral("sltRowLabel"));
		label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
		label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		h->addWidget(label, 1);
		rowUi.labelLbl = label;

		auto *cloneBtn = new QPushButton(rowFrame);
		auto *settingsBtn = new QPushButton(rowFrame);
		auto *removeBtn = new QPushButton(rowFrame);

		auto *st = rowFrame->style();

		cloneBtn->setIcon(
			QIcon::fromTheme(QStringLiteral("edit-copy"), st->standardIcon(QStyle::SP_DialogOpenButton)));
		cloneBtn->setToolTip(tr("Clone lower third"));
		cloneBtn->setText(QString());
		cloneBtn->setFlat(true);

		settingsBtn->setIcon(QIcon::fromTheme(QStringLiteral("settings-configure"),
						      st->standardIcon(QStyle::SP_FileDialogInfoView)));
		settingsBtn->setToolTip(tr("Open settings"));
		settingsBtn->setText(QString());
		settingsBtn->setFlat(true);

		removeBtn->setIcon(st->standardIcon(QStyle::SP_DialogCloseButton));
		removeBtn->setToolTip(tr("Remove lower third"));
		removeBtn->setText(QString());
		removeBtn->setFlat(true);

		cloneBtn->setIconSize(QSize(16, 16));
		settingsBtn->setIconSize(QSize(16, 16));
		removeBtn->setIconSize(QSize(16, 16));

		h->addWidget(cloneBtn);
		h->addWidget(settingsBtn);
		h->addWidget(removeBtn);

		listLayout->addWidget(rowFrame);

		rowUi.row = rowFrame;
		rowUi.cloneBtn = cloneBtn;
		rowUi.settingsBtn = settingsBtn;
		rowUi.removeBtn = removeBtn;

		const QString id = rowUi.id;

		connect(visible, &QCheckBox::toggled, this, [this, id](bool) { handleToggleVisible(id, false); });

		rowFrame->installEventFilter(this);
		label->installEventFilter(this);
		thumb->installEventFilter(this);

		connect(cloneBtn, &QPushButton::clicked, this, [this, id]() { handleClone(id); });
		connect(settingsBtn, &QPushButton::clicked, this, [this, id]() { handleOpenSettings(id); });
		connect(removeBtn, &QPushButton::clicked, this, [this, id]() { handleRemove(id); });

		rows.push_back(rowUi);
	}

	rebuildShortcuts();
	updateRowActiveStyles();
	rebuildCarousel();
}

void LowerThirdDock::updateRowActiveStyles()
{
	auto &items = smart_lt::all();

	for (auto &row : rows) {
		bool isVisible = false;

		for (const auto &cfg : items) {
			if (row.id == QString::fromStdString(cfg.id)) {
				isVisible = cfg.visible;
				break;
			}
		}

		if (row.row) {
			row.row->setProperty("sltActive", QVariant(isVisible));
			row.row->style()->unpolish(row.row);
			row.row->style()->polish(row.row);
			row.row->update();
		}

		if (row.visibleCheck) {
			row.visibleCheck->blockSignals(true);
			row.visibleCheck->setChecked(isVisible);
			row.visibleCheck->blockSignals(false);
		}
	}
}

// ------------------------------------------------------------
// row actions
// ------------------------------------------------------------

void LowerThirdDock::handleToggleVisible(const QString &id, bool hideOthers)
{
	smart_lt::toggle_active(id.toStdString(), hideOthers);

	emit requestSave();
	updateRowActiveStyles();
	updateCarouselActiveStyles();
}

void LowerThirdDock::handleClone(const QString &id)
{
	const QString newId = QString::fromStdString(smart_lt::clone_lower_third(id.toStdString()));
	if (newId.isEmpty())
		return;

	rebuildList();
	emit requestSave();
}

void LowerThirdDock::handleOpenSettings(const QString &id)
{
	LowerThirdSettingsDialog dlg(this);
	dlg.setLowerThirdId(id);
	dlg.exec();

	rebuildList();
	emit requestSave();
}

void LowerThirdDock::handleRemove(const QString &id)
{
	smart_lt::remove_lower_third(id.toStdString());

	rebuildList();
	emit requestSave();
}

// ------------------------------------------------------------
// Dock lifecycle wrappers
// ------------------------------------------------------------

void LowerThird_create_dock()
{
	if (g_dockWidget)
		return;

	auto *panel = new LowerThirdDock(nullptr);
	panel->init();

#if defined(HAVE_OBS_DOCK_BY_ID)
	obs_frontend_add_dock_by_id(sltDockId, sltDockTitle, panel);
#else
	obs_frontend_add_dock(panel);
#endif

	g_dockWidget = panel;
	LOGI("Dock created");
}

void LowerThird_destroy_dock()
{
	if (!g_dockWidget)
		return;

#if defined(HAVE_OBS_DOCK_BY_ID)
	obs_frontend_remove_dock(sltDockId);
#else
	// NOTE: obs_frontend_remove_dock(QWidget*) is deprecated in some OBS versions.
	// Prefer _by_id when available.
	obs_frontend_remove_dock(g_dockWidget);
#endif

	g_dockWidget = nullptr;
	LOGI("Dock destroyed");
}

LowerThirdDock *LowerThird_get_dock()
{
	return qobject_cast<LowerThirdDock *>(g_dockWidget);
}
