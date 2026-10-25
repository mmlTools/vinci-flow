// dock.cpp
#define LOG_TAG "[" PLUGIN_NAME "][dock]"
#include "dock.hpp"

#include "core.hpp"
#include "hotkeys.hpp"
#include "settings.hpp"
#include "widget.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <cstring>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QAbstractItemView>
#include <QColorDialog>
#include <QDialog>
#include <QFormLayout>
#include <QListWidget>
#include <QLabel>
#include <QColor>
#include <QSet>
#include <QPushButton>
#include <QScrollArea>
#include <QCheckBox>
#include <QLineEdit>
#include <QFileDialog>
#include <QStyle>
#include <QFrame>
#include <QEvent>
#include <QMouseEvent>
#include <QDir>
#include <QPixmap>
#include <QVariant>
#include <QSizePolicy>
#include <QTimer>
#include <QDateTime>
#include <QComboBox>
#include <QSpinBox>
#include <QMetaObject>
#include <QUrl>
#include <QAbstractButton>
#include <QDesktopServices>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QKeySequenceEdit>
#include <QTabWidget>
#include <QToolButton>
#include <QToolBar>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVersionNumber>

static QWidget *g_dockWidget = nullptr;

struct GroupRuntime {
	bool running = false;
	// Incremented on every start/stop; pending timers from an older run see a
	// different value and do nothing (prevents double-firing after stop/start).
	quint64 generation = 0;
	int index = 0;
	bool phaseShow = true;
	QString currentId;
	qint64 hideAtMs = 0;
	QVector<int> seq;
};

static QHash<QString, GroupRuntime> g_groupRuns;
static QHash<QString, qint64> g_groupHideAtMs;

static constexpr const char *kReleasesApiUrl = "https://api.github.com/repos/mmlTools/vinci-flow/releases/latest";

// Thumbnail size in the list. The row height follows it, so every row is the
// same height whether or not it has a picture, a countdown or the LIVE badge.
static constexpr int kRowThumbSize = 32;
static constexpr int kRowThumbRadius = 6;

static QString rgba(const QColor &c, double alpha)
{
	return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha, 0, 'f', 2);
}

// Flat single-colour icons for actions the OBS theme has no icon for. They are
// drawn in the palette text colour so they match the theme's own SVG icons in
// dark and light themes alike. kind: "groups", "clone" or "help".
static QIcon glyphIcon(const char *kind, const QColor &color)
{
	QIcon icon;
	for (const qreal dpr : {1.0, 2.0}) {
		QPixmap pm(int(16 * dpr), int(16 * dpr));
		pm.setDevicePixelRatio(dpr);
		pm.fill(Qt::transparent);

		QPainter p(&pm);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);

		if (std::strcmp(kind, "groups") == 0) {
			// Playlist: three bars and a play triangle.
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			p.drawRoundedRect(QRectF(1.5, 2.0, 13.0, 2.0), 1.0, 1.0);
			p.drawRoundedRect(QRectF(1.5, 7.0, 7.5, 2.0), 1.0, 1.0);
			p.drawRoundedRect(QRectF(1.5, 12.0, 7.5, 2.0), 1.0, 1.0);
			QPainterPath tri;
			tri.moveTo(10.5, 6.5);
			tri.lineTo(15.0, 10.5);
			tri.lineTo(10.5, 14.5);
			tri.closeSubpath();
			p.drawPath(tri);
		} else if (std::strcmp(kind, "clone") == 0) {
			// Two overlapping sheets; the front one punches out the back one.
			p.drawRoundedRect(QRectF(5.5, 1.5, 9.0, 9.0), 1.5, 1.5);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.setBrush(Qt::transparent);
			p.drawRoundedRect(QRectF(1.5, 5.5, 9.0, 9.0), 1.5, 1.5);
		} else {
			// Question mark in a circle.
			p.drawEllipse(QRectF(1.5, 1.5, 13.0, 13.0));
			QFont f = p.font();
			f.setBold(true);
			f.setPixelSize(10);
			p.setFont(f);
			p.drawText(QRectF(0, 0, 16, 16), Qt::AlignCenter, QStringLiteral("?"));
		}
		p.end();
		icon.addPixmap(pm);
	}
	return icon;
}

// Centre-crops a picture into a rounded square (QSS border-radius does not clip pixmaps).
static QPixmap roundedThumbnail(const QPixmap &src, int size, int radius, qreal dpr)
{
	const int px = int(size * dpr);
	QPixmap out(px, px);
	out.setDevicePixelRatio(dpr);
	out.fill(Qt::transparent);

	QPainter p(&out);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);

	QPainterPath clip;
	clip.addRoundedRect(QRectF(0, 0, size, size), radius, radius);
	p.setClipPath(clip);

	const QPixmap scaled = src.scaled(px, px, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
	const QRectF source((scaled.width() - px) / 2.0, (scaled.height() - px) / 2.0, px, px);
	p.drawPixmap(QRectF(0, 0, size, size), scaled, source);
	return out;
}

// Stand-in for lower thirds without a picture: the first letter on a tinted tile.
static QPixmap initialsThumbnail(const QString &name, int size, int radius, const QColor &bg, const QColor &fg,
				 qreal dpr)
{
	QPixmap out(int(size * dpr), int(size * dpr));
	out.setDevicePixelRatio(dpr);
	out.fill(Qt::transparent);

	QPainter p(&out);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(Qt::NoPen);
	p.setBrush(bg);
	p.drawRoundedRect(QRectF(0, 0, size, size), radius, radius);

	QString initial = QStringLiteral("#");
	for (const QChar ch : name.trimmed()) {
		if (ch.isLetterOrNumber()) {
			initial = QString(ch).toUpper();
			break;
		}
	}

	QFont f = p.font();
	f.setBold(true);
	f.setPixelSize(int(size * 0.45));
	p.setFont(f);
	p.setPen(fg);
	p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, initial);
	return out;
}

static void applyGroupRowStyle(QFrame *rowFrame)
{
	if (!rowFrame)
		return;

	const bool active = rowFrame->property("sltActive").toBool();
	const bool inCar = rowFrame->property("sltInGroup").toBool();
	const QString col = rowFrame->property("sltGroupColor").toString();

	if (!active && inCar && !col.isEmpty()) {
		QColor c(col);
		if (c.isValid()) {
			rowFrame->setStyleSheet(
				QStringLiteral(
					"QFrame#sltRowFrame{ background: rgba(%1,%2,%3,0.14); border: 1px solid rgba(%1,%2,%3,0.55); }")
					.arg(c.red())
					.arg(c.green())
					.arg(c.blue()));
			return;
		}
	}

	rowFrame->setStyleSheet(QString());
}

static QIcon themeIcon(const char *name, QStyle *st, QStyle::StandardPixmap fallback)
{
	QIcon ico = QIcon::fromTheme(QString::fromLatin1(name));
	return ico.isNull() ? st->standardIcon(fallback) : ico;
}

static void stopGroupRun(const QString &groupId)
{
	auto it = g_groupRuns.find(groupId);
	if (it == g_groupRuns.end())
		return;

	if (!it->currentId.isEmpty()) {
		g_groupHideAtMs.remove(it->currentId);
		vflow::set_visible_persist(it->currentId.toStdString(), false);
	}

	it->running = false;
	it->generation++;
	it->index = 0;
	it->phaseShow = true;
	it->currentId.clear();
	it->hideAtMs = 0;
	it->seq.clear();
}

static bool isGroupRunning(const QString &groupId)
{
	auto it = g_groupRuns.find(groupId);
	return it != g_groupRuns.end() && it->running;
}

static void shuffleSeq(QVector<int> &seq)
{
	auto *rng = QRandomGenerator::global();
	for (int i = (int)seq.size() - 1; i > 0; --i) {
		const int j = (int)rng->bounded((quint32)(i + 1));
		std::swap(seq[i], seq[j]);
	}
}

static void scheduleGroupStep(vflow::ui::LowerThirdDock *dock, const QString &groupId, quint64 generation);

static void startGroupRun(vflow::ui::LowerThirdDock *dock, const QString &groupId)
{
	if (!dock)
		return;

	auto *car = vflow::get_group_by_id(groupId.toStdString());
	if (!car || car->members.empty())
		return;

	auto &rt = g_groupRuns[groupId];
	rt.running = true;
	rt.generation++;
	rt.index = 0;
	rt.phaseShow = true;
	rt.currentId.clear();
	rt.hideAtMs = 0;
	rt.seq.clear();

	scheduleGroupStep(dock, groupId, rt.generation);
}

static void scheduleGroupStep(vflow::ui::LowerThirdDock *dock, const QString &groupId, quint64 generation)
{
	if (!dock)
		return;

	auto it = g_groupRuns.find(groupId);
	if (it == g_groupRuns.end() || !it->running || it->generation != generation)
		return;

	auto *car = vflow::get_group_by_id(groupId.toStdString());
	if (!car || car->members.empty()) {
		stopGroupRun(groupId);
		return;
	}

	const int visibleMs = std::max(0, car->visible_ms);
	const int intervalMs = std::max(0, car->interval_ms);
	const int count = (int)car->members.size();

	if (it->seq.size() != count) {
		it->seq.clear();
		it->seq.reserve(count);
		for (int i = 0; i < count; ++i)
			it->seq.push_back(i);
		if (car->order_mode == 1)
			shuffleSeq(it->seq);
		it->index = 0;
	}

	auto next = [dock, groupId, generation](int delayMs) {
		QTimer::singleShot(delayMs, dock,
				   [dock, groupId, generation]() { scheduleGroupStep(dock, groupId, generation); });
	};

	if (it->phaseShow) {
		if (!it->currentId.isEmpty()) {
			g_groupHideAtMs.remove(it->currentId);
			vflow::set_visible_persist(it->currentId.toStdString(), false);
		}

		if (it->index >= count) {
			if (!car->loop) {
				stopGroupRun(groupId);
				return;
			}
			it->index = 0;
			if (car->order_mode == 1)
				shuffleSeq(it->seq);
		}

		const int memberIdx = it->seq.value(it->index, 0);
		const QString nextId = QString::fromStdString(car->members[(size_t)memberIdx]);
		it->currentId = nextId;
		it->hideAtMs = QDateTime::currentMSecsSinceEpoch() + (qint64)visibleMs;
		g_groupHideAtMs[nextId] = it->hideAtMs;

		vflow::set_visible_persist(nextId.toStdString(), true);

		it->phaseShow = false;
		next(visibleMs);
	} else {
		if (!it->currentId.isEmpty()) {
			g_groupHideAtMs.remove(it->currentId);
			vflow::set_visible_persist(it->currentId.toStdString(), false);
		}

		it->currentId.clear();
		it->hideAtMs = 0;
		it->index++;

		if (!car->loop && it->index >= count) {
			stopGroupRun(groupId);
			return;
		}

		it->phaseShow = true;
		next(intervalMs);
	}
}

// Creates a Browser Source sized to the canvas, adds it to the scene the user is
// editing and returns its name. Empty on failure.
static QString createOverlaySource()
{
	const std::string html =
		vflow::has_output_dir()
			? QDir(QString::fromStdString(vflow::output_dir())).filePath("lt.html").toStdString()
			: std::string();

	int width = sltBrowserWidth;
	int height = sltBrowserHeight;
	obs_video_info ovi{};
	if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
		width = (int)ovi.base_width;
		height = (int)ovi.base_height;
	}

	QString name = QString::fromLatin1(sltBrowserSourceName);
	for (int n = 2;; ++n) {
		obs_source_t *existing = obs_get_source_by_name(name.toUtf8().constData());
		if (!existing)
			break;
		obs_source_release(existing);
		name = QStringLiteral("%1 %2").arg(QString::fromLatin1(sltBrowserSourceName)).arg(n);
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", html.c_str());
	obs_data_set_int(settings, "width", width);
	obs_data_set_int(settings, "height", height);
	obs_data_set_bool(settings, "reroute_audio", true);
	obs_data_set_bool(settings, "shutdown", false);
	obs_data_set_bool(settings, "restart_when_active", false);
	obs_source_t *src = obs_source_create(sltBrowserSourceId, name.toUtf8().constData(), settings, nullptr);
	obs_data_release(settings);
	if (!src)
		return {};

	obs_source_t *sceneSrc = obs_frontend_preview_program_mode_active() ? obs_frontend_get_current_preview_scene()
									    : obs_frontend_get_current_scene();
	if (obs_scene_t *scene = obs_scene_from_source(sceneSrc)) {
		if (obs_sceneitem_t *item = obs_scene_add(scene, src))
			obs_sceneitem_set_locked(item, true); // full-canvas overlay: avoid accidental drags
	}
	obs_source_release(sceneSrc);
	obs_source_release(src);

	// Bind first, then size: the size applies to the current target source.
	vflow::set_target_browser_source_name(name.toStdString());
	vflow::set_target_browser_dimensions(width, height);
	return name;
}

namespace vflow::ui {

void LowerThirdDock::coreEventThunk(const vflow::core_event &ev, void *user)
{
	auto *self = static_cast<LowerThirdDock *>(user);
	if (!self)
		return;

	QMetaObject::invokeMethod(self, [self, ev]() { self->onCoreEvent(ev); }, Qt::QueuedConnection);
}

LowerThirdDock::~LowerThirdDock()
{
	stopAll();
}

void LowerThirdDock::stopAll()
{
	for (auto it = g_groupRuns.begin(); it != g_groupRuns.end(); ++it) {
		it->running = false;
		it->generation++;
	}
	if (repeatTimer_)
		repeatTimer_->stop();
	disconnectObsSignals();
	if (coreListenerToken_) {
		vflow::remove_event_listener(coreListenerToken_);
		coreListenerToken_ = 0;
	}
}

void LowerThirdDock::onCoreEvent(const vflow::core_event &ev)
{
	switch (ev.type) {
	case vflow::event_type::VisibilityChanged: {
		const QString qid = QString::fromStdString(ev.id);
		const bool active = ev.visible;

		for (auto &row : rows) {
			if (row.id != qid)
				continue;

			if (row.row) {
				row.row->setProperty("sltActive", QVariant(active));
				applyGroupRowStyle(row.row);
				row.row->style()->unpolish(row.row);
				row.row->style()->polish(row.row);
				row.row->update();
			}
			if (row.liveLbl)
				row.liveLbl->setVisible(active);

			updateRowCountdownFor(row);
			break;
		}
		break;
	}

	case vflow::event_type::ListChanged:
	case vflow::event_type::Reloaded: {
		rebuildList();
		updateRowCountdowns();
		updateSetupState();
		break;
	}

	default:
		break;
	}
}

LowerThirdDock::LowerThirdDock(QWidget *parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("LowerThirdDock"));
	// A plain QWidget ignores stylesheet backgrounds; with this attribute the
	// OBS theme's dock rule (OBSDock > QWidget) paints the same grey body the
	// Sources dock has.
	setAttribute(Qt::WA_StyledBackground, true);
	applyDockStyle();

	// Everything is flush with the dock edges: list, setup panel and footer
	// toolbar stack with no gaps, like the Sources dock.
	auto *rootLayout = new QVBoxLayout(this);
	rootLayout->setContentsMargins(0, 0, 0, 0);
	rootLayout->setSpacing(0);

	auto *body = new QVBoxLayout();
	body->setContentsMargins(0, 0, 0, 0);
	body->setSpacing(0);
	rootLayout->addLayout(body, 1);

	auto *st = style();
	const QColor glyphColor = QApplication::palette().color(QPalette::ButtonText);

	// Update notice (GitHub releases)
	{
		updateFrame_ = new QFrame(this);
		updateFrame_->setObjectName(QStringLiteral("sltUpdateBanner"));
		updateFrame_->setVisible(false);

		auto *row = new QHBoxLayout(updateFrame_);
		row->setContentsMargins(8, 4, 8, 4);
		row->setSpacing(8);

		updateLabel_ = new QLabel(updateFrame_);
		updateLabel_->setObjectName(QStringLiteral("sltBannerLabel"));
		updateLabel_->setWordWrap(true);
		row->addWidget(updateLabel_, 1);

		updateBtn_ = new QPushButton(tr("Download"), updateFrame_);
		updateBtn_->setCursor(Qt::PointingHandCursor);
		row->addWidget(updateBtn_);

		connect(updateBtn_, &QPushButton::clicked, this, [this]() {
			if (!updateUrl_.isEmpty())
				QDesktopServices::openUrl(QUrl(updateUrl_));
		});

		body->addWidget(updateFrame_);
	}

	// Setup guidance: tells the user exactly what is missing, with a one-click fix.
	{
		statusFrame_ = new QFrame(this);
		statusFrame_->setObjectName(QStringLiteral("sltBanner"));
		statusFrame_->setVisible(false);

		auto *v = new QVBoxLayout(statusFrame_);
		v->setContentsMargins(8, 6, 8, 6);
		v->setSpacing(6);

		statusLabel_ = new QLabel(statusFrame_);
		statusLabel_->setObjectName(QStringLiteral("sltBannerLabel"));
		statusLabel_->setWordWrap(true);
		v->addWidget(statusLabel_);

		statusBtn_ = new QPushButton(statusFrame_);
		statusBtn_->setCursor(Qt::PointingHandCursor);
		v->addWidget(statusBtn_, 0, Qt::AlignLeft);

		body->addWidget(statusFrame_);
	}

	// Lower third list, flat on the dock body like the Sources list.
	{
		scrollArea = new QScrollArea(this);
		scrollArea->setObjectName(QStringLiteral("LowerThirdContent"));
		scrollArea->setWidgetResizable(true);
		scrollArea->setFrameShape(QFrame::NoFrame);
		scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

		listContainer = new QWidget(scrollArea);
		listContainer->setObjectName(QStringLiteral("sltListContainer"));
		listLayout = new QVBoxLayout(listContainer);
		listLayout->setContentsMargins(4, 4, 4, 4);
		listLayout->setSpacing(2);
		listLayout->setAlignment(Qt::AlignTop);

		emptyStateLbl_ = new QLabel(tr("No lower thirds yet.\nClick \"+\" below to create your first one, "
					       "then click it here to show or hide it on stream."),
					    listContainer);
		emptyStateLbl_->setObjectName(QStringLiteral("sltEmptyState"));
		emptyStateLbl_->setAlignment(Qt::AlignCenter);
		emptyStateLbl_->setWordWrap(true);
		listLayout->addWidget(emptyStateLbl_);

		scrollArea->setWidget(listContainer);
		body->addWidget(scrollArea, 1);
	}

	// Setup panel (collapsed once everything works)
	{
		auto *panel = new QFrame(this);
		panel->setObjectName(QStringLiteral("sltSetupPanel"));
		setupPanel_ = panel;

		auto *panelLayout = new QVBoxLayout(panel);
		panelLayout->setContentsMargins(8, 6, 8, 8);
		panelLayout->setSpacing(6);

		auto *title = new QLabel(tr("Overlay setup").toUpper(), panel);
		title->setObjectName(QStringLiteral("sltSectionTitle"));
		panelLayout->addWidget(title);

		auto *form = new QGridLayout();
		form->setContentsMargins(0, 0, 0, 0);
		form->setHorizontalSpacing(6);
		form->setVerticalSpacing(4);
		form->setColumnStretch(1, 1);
		panelLayout->addLayout(form);

		int r = 0;

		// Overlay source
		form->addWidget(new QLabel(tr("Overlay source:"), setupPanel_), r, 0);
		{
			auto *line = new QHBoxLayout();
			line->setSpacing(4);

			browserSourceCombo = new QComboBox(setupPanel_);
			browserSourceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			browserSourceCombo->setToolTip(tr("The Browser Source that displays your lower thirds"));

			refreshSourcesBtn = new QPushButton(setupPanel_);
			refreshSourcesBtn->setCursor(Qt::PointingHandCursor);
			refreshSourcesBtn->setToolTip(tr("Refresh list"));
			refreshSourcesBtn->setIcon(themeIcon("view-refresh", st, QStyle::SP_BrowserReload));
			refreshSourcesBtn->setProperty("class", QStringLiteral("icon-refresh"));

			createSourceBtn_ = new QPushButton(setupPanel_);
			createSourceBtn_->setCursor(Qt::PointingHandCursor);
			createSourceBtn_->setToolTip(tr("Create a new overlay Browser Source in the current scene"));
			createSourceBtn_->setIcon(themeIcon("list-add", st, QStyle::SP_FileDialogNewFolder));
			createSourceBtn_->setProperty("class", QStringLiteral("icon-plus"));

			line->addWidget(browserSourceCombo, 1);
			line->addWidget(refreshSourcesBtn);
			line->addWidget(createSourceBtn_);
			form->addLayout(line, r++, 1);
		}

		// Overlay size
		form->addWidget(new QLabel(tr("Overlay size:"), setupPanel_), r, 0);
		{
			auto *line = new QHBoxLayout();
			line->setSpacing(4);

			browserWidthSpin = new QSpinBox(setupPanel_);
			browserWidthSpin->setRange(1, 16384);
			browserWidthSpin->setSuffix(QStringLiteral(" px"));
			browserWidthSpin->setToolTip(tr("Width of the overlay (normally your canvas width)"));

			browserHeightSpin = new QSpinBox(setupPanel_);
			browserHeightSpin->setRange(1, 16384);
			browserHeightSpin->setSuffix(QStringLiteral(" px"));
			browserHeightSpin->setToolTip(tr("Height of the overlay (normally your canvas height)"));

			matchCanvasBtn_ = new QPushButton(tr("Match canvas"), setupPanel_);
			matchCanvasBtn_->setCursor(Qt::PointingHandCursor);
			matchCanvasBtn_->setToolTip(tr("Use the base canvas size from Settings > Video"));

			line->addWidget(browserWidthSpin, 1);
			line->addWidget(new QLabel(QStringLiteral("x"), setupPanel_));
			line->addWidget(browserHeightSpin, 1);
			line->addWidget(matchCanvasBtn_);
			form->addLayout(line, r++, 1);
		}

		// Resources folder
		form->addWidget(new QLabel(tr("Resources folder:"), setupPanel_), r, 0);
		{
			auto *line = new QHBoxLayout();
			line->setSpacing(4);

			outputPathEdit = new QLineEdit(setupPanel_);
			outputPathEdit->setReadOnly(true);
			outputPathEdit->setToolTip(
				tr("Where VinciFlow keeps your lower thirds, images and the generated overlay files"));

			outputBrowseBtn = new QPushButton(setupPanel_);
			outputBrowseBtn->setCursor(Qt::PointingHandCursor);
			outputBrowseBtn->setToolTip(tr("Use a different folder (for example a synced or shared one)"));
			outputBrowseBtn->setIcon(st->standardIcon(QStyle::SP_DirOpenIcon));

			openFolderBtn_ = new QPushButton(setupPanel_);
			openFolderBtn_->setCursor(Qt::PointingHandCursor);
			openFolderBtn_->setToolTip(tr("Open the folder in your file manager"));
			openFolderBtn_->setIcon(themeIcon("folder-open", st, QStyle::SP_DirLinkIcon));

			line->addWidget(outputPathEdit, 1);
			line->addWidget(outputBrowseBtn);
			line->addWidget(openFolderBtn_);
			form->addLayout(line, r++, 1);
		}

		body->addWidget(setupPanel_);

		connect(outputBrowseBtn, &QPushButton::clicked, this, &LowerThirdDock::onBrowseOutputFolder);
		connect(openFolderBtn_, &QPushButton::clicked, this, []() {
			if (vflow::has_output_dir())
				QDesktopServices::openUrl(
					QUrl::fromLocalFile(QString::fromStdString(vflow::output_dir())));
		});
		connect(refreshSourcesBtn, &QPushButton::clicked, this, [this]() { populateBrowserSources(true); });
		connect(createSourceBtn_, &QPushButton::clicked, this, &LowerThirdDock::onCreateOverlaySource);
		connect(browserSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			&LowerThirdDock::onBrowserSourceChanged);
		connect(browserWidthSpin, &QSpinBox::editingFinished, this, &LowerThirdDock::onBrowserSizeChanged);
		connect(browserHeightSpin, &QSpinBox::editingFinished, this, &LowerThirdDock::onBrowserSizeChanged);
		connect(matchCanvasBtn_, &QPushButton::clicked, this, [this]() {
			obs_video_info ovi{};
			if (!obs_get_video_info(&ovi))
				return;
			browserWidthSpin->setValue((int)ovi.base_width);
			browserHeightSpin->setValue((int)ovi.base_height);
			onBrowserSizeChanged();
		});
	}

	// Footer: a real QToolBar, so the OBS theme styles it exactly like the
	// toolbar under the Sources list (border-top, height, button hover).
	// Buttons carry the theme icon classes ("icon-plus", ...) so the theme
	// supplies matching icons; the icons set here are only a fallback.
	{
		auto *toolbar = new QToolBar(this);
		toolbar->setObjectName(QStringLiteral("sltToolbar"));
		toolbar->setIconSize(QSize(16, 16));
		toolbar->setMovable(false);
		toolbar->setFloatable(false);
		toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

		auto makeToolBtn = [toolbar](const QIcon &icon, const char *themeClass, const QString &tip) {
			auto *b = new QToolButton(toolbar);
			b->setCursor(Qt::PointingHandCursor);
			b->setFocusPolicy(Qt::NoFocus);
			b->setIcon(icon);
			b->setIconSize(QSize(16, 16));
			b->setToolTip(tip);
			b->setToolButtonStyle(Qt::ToolButtonIconOnly);
			if (themeClass)
				b->setProperty("class", QString::fromLatin1(themeClass));
			toolbar->addWidget(b);
			return b;
		};

		addBtn = makeToolBtn(themeIcon("list-add", st, QStyle::SP_FileDialogNewFolder), "icon-plus",
				     tr("New lower third"));
		manageGroupsBtn_ = makeToolBtn(glyphIcon("groups", glyphColor), nullptr,
					       tr("Groups: play several lower thirds one after another"));

		auto *spacer = new QWidget(toolbar);
		spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		toolbar->addWidget(spacer);

		setupToggleBtn_ = makeToolBtn(themeIcon("preferences-system", st, QStyle::SP_FileDialogInfoView),
					      "icon-cogs", tr("Setup: overlay source, size and resources folder"));
		setupToggleBtn_->setCheckable(true);
		infoBtn =
			makeToolBtn(glyphIcon("help", glyphColor), nullptr, tr("Help, guides, community and support"));

		rootLayout->addWidget(toolbar);

		connect(addBtn, &QToolButton::clicked, this, &LowerThirdDock::onAddLowerThird);
		connect(manageGroupsBtn_, &QToolButton::clicked, this, &LowerThirdDock::onManageGroups);
		connect(infoBtn, &QToolButton::clicked, this, [this]() { show_troubleshooting_dialog(this); });
		connect(setupToggleBtn_, &QToolButton::toggled, this, [this](bool on) { setSetupExpanded(on); });
	}

	addBtn->setEnabled(vflow::has_output_dir());
}

void LowerThirdDock::applyDockStyle()
{
	// Colours come from the application palette (the OBS theme), with alpha,
	// which QSS palette() roles cannot express. The widget's own palette is not
	// used: applying a stylesheet rewrites it, and reading it back here would
	// produce a different stylesheet on every PaletteChange, without end.
	if (applyingStyle_)
		return;
	const QPalette pal = QApplication::palette();
	paletteKey_ = pal.cacheKey();
	const QColor text = pal.color(QPalette::Text);
	const QColor hl = pal.color(QPalette::Highlight);
	const QColor hlText = pal.color(QPalette::HighlightedText);

	const QString t08 = rgba(text, 0.08), t12 = rgba(text, 0.12), t16 = rgba(text, 0.16), t60 = rgba(text, 0.60);
	const QString h12 = rgba(hl, 0.12), h65 = rgba(hl, 0.65);

	QString css;
	// Same grey body as the Sources dock (palette base is the theme's bg_base).
	css += QStringLiteral("QWidget#LowerThirdDock { background: palette(base); }\n");

	css += QStringLiteral("QFrame#sltBanner, QFrame#sltUpdateBanner { background: %1; border: none; "
			      "border-bottom: 1px solid %2; }\n")
		       .arg(h12, h65);
	css += QStringLiteral("QLabel#sltBannerLabel { font-weight: 600; background: transparent; }\n");

	css += QStringLiteral("QScrollArea#LowerThirdContent, QWidget#sltListContainer { background: transparent; }\n");
	css += QStringLiteral("QLabel#sltEmptyState { color: %1; padding: 16px 24px; background: transparent; }\n")
		       .arg(t60);

	// Rows behave like list items: flat, hover tint, and the one on stream is
	// highlighted like a selected source.
	css += QStringLiteral("QFrame#sltRowFrame { background: transparent; border: 1px solid transparent; "
			      "border-radius: 4px; }\n");
	css += QStringLiteral("QFrame#sltRowFrame:hover { background: %1; }\n").arg(t08);
	css += QStringLiteral("QFrame#sltRowFrame[sltActive=\"true\"] { background: %1; }\n").arg(hl.name());
	css += QStringLiteral("QFrame#sltRowFrame QLabel { background: transparent; }\n");
	css += QStringLiteral("QLabel#sltRowLabel { font-weight: 600; }\n");
	css += QStringLiteral("QLabel#sltRowSubLabel { font-size: 11px; color: %1; }\n").arg(t60);
	css += QStringLiteral("QFrame#sltRowFrame[sltActive=\"true\"] QLabel#sltRowLabel { color: %1; }\n")
		       .arg(hlText.name());
	css += QStringLiteral("QFrame#sltRowFrame[sltActive=\"true\"] QLabel#sltRowSubLabel { color: %1; }\n")
		       .arg(rgba(hlText, 0.80));
	css += QStringLiteral("QLabel#sltLivePill { background: #d93025; color: white; border-radius: 8px; "
			      "padding: 2px 7px; font-size: 10px; font-weight: 700; }\n");
	css += QStringLiteral("QToolButton#sltRowBtn { background: transparent; border: 1px solid transparent; "
			      "border-radius: 4px; padding: 0; }\n");
	css += QStringLiteral("QToolButton#sltRowBtn:hover { background: %1; border-color: %2; }\n").arg(t12, t16);
	css += QStringLiteral("QToolButton#sltRowBtn:pressed { background: %1; }\n").arg(t16);

	// Setup section: a flat strip separated from the list by a line.
	css += QStringLiteral("QFrame#sltSetupPanel { background: transparent; border: none; "
			      "border-top: 1px solid %1; }\n")
		       .arg(t12);
	css += QStringLiteral("QLabel#sltSectionTitle { font-size: 11px; font-weight: 700; color: %1; "
			      "background: transparent; }\n")
		       .arg(t60);

	if (css == styleSheet())
		return;

	applyingStyle_ = true;
	setStyleSheet(css);
	applyingStyle_ = false;
}

void LowerThirdDock::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);

	// OBS theme switch: recolour the stylesheet and the palette-drawn icons and
	// thumbnails. Only the application palette counts; the widget palette also
	// changes whenever a stylesheet is (re)applied.
	const bool paletteEvent = event->type() == QEvent::PaletteChange ||
				  event->type() == QEvent::ApplicationPaletteChange;
	if (!paletteEvent || applyingStyle_ || QApplication::palette().cacheKey() == paletteKey_)
		return;

	applyDockStyle();
	if (coreListenerToken_) {
		rebuildList();
		updateRowCountdowns();
	}
}

bool LowerThirdDock::init()
{
	if (vflow::has_output_dir())
		outputPathEdit->setText(QDir::toNativeSeparators(QString::fromStdString(vflow::output_dir())));
	else
		outputPathEdit->clear();

	const bool hasDir = vflow::has_output_dir();
	addBtn->setEnabled(hasDir);

	if (hasDir)
		vflow::ensure_output_artifacts_exist();

	populateBrowserSources(true);

	if (browserWidthSpin) {
		browserWidthSpin->blockSignals(true);
		browserWidthSpin->setValue(vflow::target_browser_width());
		browserWidthSpin->blockSignals(false);
	}
	if (browserHeightSpin) {
		browserHeightSpin->blockSignals(true);
		browserHeightSpin->setValue(vflow::target_browser_height());
		browserHeightSpin->blockSignals(false);
	}

	rebuildList();

	if (!coreListenerToken_) {
		coreListenerToken_ = vflow::add_event_listener(&LowerThirdDock::coreEventThunk, this);
	}

	ensureRepeatTimerStarted();
	connectObsSignals();
	updateSetupState();
	QTimer::singleShot(5000, this, [this]() { checkForUpdates(); });
	return true;
}

void LowerThirdDock::setSetupExpanded(bool expanded)
{
	if (setupPanel_)
		setupPanel_->setVisible(expanded);
	if (setupToggleBtn_ && setupToggleBtn_->isChecked() != expanded) {
		setupToggleBtn_->blockSignals(true);
		setupToggleBtn_->setChecked(expanded);
		setupToggleBtn_->blockSignals(false);
	}
}

void LowerThirdDock::updateSetupState()
{
	if (!statusFrame_)
		return;

	statusBtn_->disconnect();

	const bool hasDir = vflow::has_output_dir();
	const bool hasSource = vflow::target_browser_source_exists();

	if (!hasDir) {
		statusLabel_->setText(tr("Choose a folder where VinciFlow can keep your lower thirds."));
		statusBtn_->setText(tr("Choose folder..."));
		connect(statusBtn_, &QPushButton::clicked, this, &LowerThirdDock::onBrowseOutputFolder);
		statusFrame_->setVisible(true);
		setSetupExpanded(true);
	} else if (!hasSource) {
		statusLabel_->setText(tr(
			"Almost there! Add the VinciFlow overlay to your scene so your lower thirds appear on stream."));
		statusBtn_->setText(tr("Add overlay to current scene"));
		connect(statusBtn_, &QPushButton::clicked, this, &LowerThirdDock::onCreateOverlaySource);
		statusFrame_->setVisible(true);
	} else {
		statusFrame_->setVisible(false);
	}

	if (emptyStateLbl_)
		emptyStateLbl_->setVisible(rows.isEmpty());

	// Keep the panel tucked away unless it is needed or the user opened it.
	if (hasDir && setupToggleBtn_ && !setupToggleBtn_->isChecked())
		setSetupExpanded(false);
}

void LowerThirdDock::onCreateOverlaySource()
{
	if (!vflow::has_output_dir()) {
		onBrowseOutputFolder();
		if (!vflow::has_output_dir())
			return;
	}

	vflow::ensure_output_artifacts_exist();
	const QString name = createOverlaySource();
	if (name.isEmpty()) {
		QMessageBox::warning(
			this, tr("Could not create overlay"),
			tr("OBS could not create a Browser Source. Make sure the Browser plugin is installed "
			   "(it ships with OBS on Windows and macOS)."));
		return;
	}

	vflow::rebuild_and_swap();

	populateBrowserSources(false);
	if (browserWidthSpin && browserHeightSpin) {
		browserWidthSpin->blockSignals(true);
		browserHeightSpin->blockSignals(true);
		browserWidthSpin->setValue(vflow::target_browser_width());
		browserHeightSpin->setValue(vflow::target_browser_height());
		browserWidthSpin->blockSignals(false);
		browserHeightSpin->blockSignals(false);
	}
	updateSetupState();
	emit requestSave();
}

void LowerThirdDock::checkForUpdates()
{
	if (!net_)
		net_ = new QNetworkAccessManager(this);

	QNetworkRequest req{QUrl(QString::fromLatin1(kReleasesApiUrl))};
	req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VinciFlow/" PLUGIN_VERSION));
	req.setRawHeader("Accept", "application/vnd.github+json");
	req.setTransferTimeout(10000);

	QNetworkReply *reply = net_->get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError)
			return; // offline or rate-limited: stay quiet

		const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
		QString tag = o.value(QStringLiteral("tag_name")).toString().trimmed();
		if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
			tag.remove(0, 1);

		const QVersionNumber remote = QVersionNumber::fromString(tag);
		const QVersionNumber local = QVersionNumber::fromString(QStringLiteral(PLUGIN_VERSION));
		if (!remote.isNull() && QVersionNumber::compare(remote, local) > 0)
			setUpdateAvailable(tag, QStringLiteral(PLUGIN_VERSION),
					   o.value(QStringLiteral("html_url")).toString());
	});
}

void LowerThirdDock::populateBrowserSources(bool keepSelection)
{
	if (!browserSourceCombo)
		return;

	populatingSources_ = true;
	browserSourceCombo->blockSignals(true);

	const QString previous = keepSelection ? browserSourceCombo->currentData().toString() : QString();
	const QString saved = QString::fromStdString(vflow::target_browser_source_name());

	browserSourceCombo->clear();

	browserSourceCombo->addItem(tr("→ Select a Browser Source ←"), QVariant(QString()));

	const auto names = vflow::list_browser_source_names();
	for (const auto &n : names) {
		const QString qn = QString::fromStdString(n);
		browserSourceCombo->addItem(qn, QVariant(qn));
	}

	int idxToSelect = 0;

	auto findByData = [this](const QString &val) -> int {
		if (val.isEmpty())
			return 0;
		for (int i = 0; i < browserSourceCombo->count(); ++i) {
			if (browserSourceCombo->itemData(i).toString() == val)
				return i;
		}
		return 0;
	};

	if (!previous.isEmpty())
		idxToSelect = findByData(previous);

	if (idxToSelect == 0 && !saved.isEmpty())
		idxToSelect = findByData(saved);

	browserSourceCombo->setCurrentIndex(idxToSelect);

	browserSourceCombo->blockSignals(false);
	populatingSources_ = false;
}

void LowerThirdDock::onBrowserSourceChanged(int index)
{
	if (populatingSources_)
		return;

	if (!browserSourceCombo || index < 0)
		return;

	const QString name = browserSourceCombo->itemData(index).toString();

	vflow::set_target_browser_source_name(name.toStdString());

	if (!name.isEmpty() && vflow::has_output_dir()) {
		vflow::rebuild_and_swap();
	}

	updateSetupState();
	emit requestSave();
}

void LowerThirdDock::onBrowserSizeChanged()
{
	if (!browserWidthSpin || !browserHeightSpin)
		return;

	const int w = browserWidthSpin->value();
	const int h = browserHeightSpin->value();

	vflow::set_target_browser_dimensions(w, h);

	emit requestSave();
}

void LowerThirdDock::ensureRepeatTimerStarted()
{
	if (repeatTimer_)
		return;

	repeatTimer_ = new QTimer(this);
	repeatTimer_->setInterval(250);
	connect(repeatTimer_, &QTimer::timeout, this, &LowerThirdDock::repeatTick);
	repeatTimer_->start();

	updateRowCountdowns();
}

void LowerThirdDock::repeatTick()
{
	if (!vflow::has_output_dir())
		return;

	const qint64 now = QDateTime::currentMSecsSinceEpoch();

	const auto &items = vflow::all();
	{
		QSet<QString> alive;
		alive.reserve((int)items.size());
		for (const auto &c : items)
			alive.insert(QString::fromStdString(c.id));

		for (auto it = nextOnMs_.begin(); it != nextOnMs_.end();) {
			if (!alive.contains(it.key()))
				it = nextOnMs_.erase(it);
			else
				++it;
		}
		for (auto it = offAtMs_.begin(); it != offAtMs_.end();) {
			if (!alive.contains(it.key()))
				it = offAtMs_.erase(it);
			else
				++it;
		}
		for (auto it = reShowAtMs_.begin(); it != reShowAtMs_.end();) {
			if (!alive.contains(it.key()))
				it = reShowAtMs_.erase(it);
			else
				++it;
		}
	}

	for (const auto &c : items) {
		const QString qid = QString::fromStdString(c.id);

		{
			const auto owners = vflow::groups_containing(c.id);
			bool inRunningGroup = false;
			for (const auto &gid : owners) {
				const QString qgid = QString::fromStdString(gid);
				auto grt = g_groupRuns.find(qgid);
				if (grt != g_groupRuns.end() && grt->running) {
					inRunningGroup = true;
					break;
				}
			}
			if (inRunningGroup) {
				nextOnMs_.remove(qid);
				offAtMs_.remove(qid);
				reShowAtMs_.remove(qid);
				continue;
			}
		}

		const int every = c.repeat_every_sec;
		int visibleSec = c.repeat_visible_sec;

		if (every <= 0) {
			nextOnMs_.remove(qid);

			if (visibleSec <= 0) {
				offAtMs_.remove(qid);
				continue;
			}

			if (vflow::is_visible(c.id)) {
				if (!offAtMs_.contains(qid))
					offAtMs_[qid] = now + (qint64)visibleSec * 1000;
			} else {
				offAtMs_.remove(qid);
			}

		} else {
			if (visibleSec <= 0)
				visibleSec = 3;

			if (!nextOnMs_.contains(qid))
				nextOnMs_[qid] = now + (qint64)every * 1000;
		}

		if (offAtMs_.contains(qid) && now >= offAtMs_[qid]) {
			offAtMs_.remove(qid);
			if (vflow::is_visible(c.id)) {
				vflow::set_visible_persist(c.id, false);
				emit requestSave();
			}
		}

		if (reShowAtMs_.contains(qid) && now >= reShowAtMs_[qid]) {
			reShowAtMs_.remove(qid);
			if (!vflow::is_visible(c.id)) {
				vflow::set_visible_persist(c.id, true);
				if (visibleSec > 0)
					offAtMs_[qid] = now + (qint64)visibleSec * 1000;
				emit requestSave();
			}
		}

		if (every > 0 && nextOnMs_.contains(qid) && now >= nextOnMs_[qid]) {
			qint64 next = nextOnMs_[qid];
			const qint64 step = (qint64)every * 1000;
			while (next <= now)
				next += step;
			nextOnMs_[qid] = next;

			if (!vflow::is_visible(c.id)) {
				vflow::set_visible_persist(c.id, true);
				if (visibleSec > 0)
					offAtMs_[qid] = now + (qint64)visibleSec * 1000;
				emit requestSave();
			} else {
				if (!reShowAtMs_.contains(qid)) {
					vflow::set_visible_persist(c.id, false);
					offAtMs_.remove(qid);
					reShowAtMs_[qid] = now + 150;
					emit requestSave();
				}
			}
		}
	}

	updateRowCountdowns();
}

void LowerThirdDock::onBrowseOutputFolder()
{
	const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Resources Folder"),
							      QString::fromStdString(vflow::output_dir()));
	if (dir.isEmpty())
		return;

	// Groups refer to lower thirds of the current folder.
	for (const QString &gid : g_groupRuns.keys())
		stopGroupRun(gid);

	if (!vflow::set_output_dir_and_load(dir.toStdString())) {
		QMessageBox::warning(
			this, tr("Cannot use output folder"),
			tr("Could not prepare the graphics in this folder. Check that OBS has read/write access "
			   "to the folder and its files, then try again.\n\n%1")
				.arg(dir));
		if (vflow::output_dir() != dir.toStdString())
			return;
	}

	nextOnMs_.clear();
	offAtMs_.clear();
	reShowAtMs_.clear();

	outputPathEdit->setText(QDir::toNativeSeparators(dir));

	const bool hasDir = vflow::has_output_dir();
	addBtn->setEnabled(hasDir);

	populateBrowserSources(true);

	rebuildList();
	updateRowCountdowns();
	updateRowActiveStyles();
	updateSetupState();
	emit requestSave();
}

void LowerThirdDock::onAddLowerThird()
{
	if (!vflow::has_output_dir())
		return;

	// Created hidden so nothing pops up on stream; open the editor right away.
	const std::string id = vflow::add_default_lower_third(false);
	if (id.empty())
		return;

	updateRowCountdowns();
	emit requestSave();
	handleOpenSettings(QString::fromStdString(id));
}

void LowerThirdDock::onManageGroups()
{
	if (!vflow::has_output_dir()) {
		QMessageBox::information(this, tr("Output folder not set"),
					 tr("Please choose an output folder first."));
		return;
	}

	QDialog dlg(this);
	dlg.setWindowTitle(tr("Lower Third Groups"));
	dlg.setModal(true);
	dlg.resize(860, 520);

	auto *root = new QHBoxLayout(&dlg);

	auto *left = new QVBoxLayout();
	auto *carList = new QListWidget(&dlg);
	carList->setSelectionMode(QAbstractItemView::SingleSelection);
	left->addWidget(carList, 1);

	auto *leftBtns = new QHBoxLayout();
	auto *btnAdd = new QPushButton(tr("New"), &dlg);
	btnAdd->setCursor(Qt::PointingHandCursor);
	btnAdd->setToolTip(tr("Add new group"));
	auto *btnDel = new QPushButton(tr("Delete"), &dlg);
	btnDel->setCursor(Qt::PointingHandCursor);
	btnDel->setToolTip(tr("Delete selected group"));
	auto *btnSort = new QPushButton(tr("Sort"), &dlg);
	btnSort->setCursor(Qt::PointingHandCursor);
	btnSort->setToolTip(tr("Sort Items in dock by their group"));
	auto *btnStart = new QPushButton(tr("Start"), &dlg);
	btnStart->setCursor(Qt::PointingHandCursor);
	btnStart->setToolTip(tr("Start carousel for selected group"));
	auto *btnStop = new QPushButton(tr("Stop"), &dlg);
	btnStop->setCursor(Qt::PointingHandCursor);
	btnStop->setToolTip(tr("Stop carousel for selected group"));
	leftBtns->addWidget(btnAdd);
	leftBtns->addWidget(btnDel);
	leftBtns->addWidget(btnSort);
	leftBtns->addStretch(1);
	leftBtns->addWidget(btnStart);
	leftBtns->addWidget(btnStop);
	left->addLayout(leftBtns);

	root->addLayout(left, 1);

	auto *right = new QVBoxLayout();

	auto *tabs = new QTabWidget(&dlg);
	auto *tabGeneral = new QWidget(&dlg);
	auto *tabFeatures = new QWidget(&dlg);
	tabs->addTab(tabGeneral, tr("General"));
	tabs->addTab(tabFeatures, tr("Group Features"));

	auto *generalForm = new QFormLayout(tabGeneral);
	auto *titleEd = new QLineEdit(tabGeneral);

	auto *toggleHkEdit = new QKeySequenceEdit(tabGeneral);
	toggleHkEdit->setMaximumSequenceLength(1);
	toggleHkEdit->setToolTip(tr("Starts/stops this group. Works while OBS is in the background and can also be "
				    "changed in OBS Settings > Hotkeys."));
	QKeySequence loadedToggleSeq;

	auto *toggleHkClear = new QPushButton(&dlg);
	toggleHkClear->setToolTip(tr("Clear hotkey"));
	toggleHkClear->setCursor(Qt::PointingHandCursor);
	toggleHkClear->setIcon(dlg.style()->standardIcon(QStyle::SP_DialogResetButton));
	toggleHkClear->setFixedWidth(32);
	toggleHkClear->setFocusPolicy(Qt::NoFocus);
	{
		auto *r = new QHBoxLayout();
		r->setContentsMargins(0, 0, 0, 0);
		r->setSpacing(4);
		r->addWidget(toggleHkEdit, 1);
		r->addWidget(toggleHkClear);
		generalForm->addRow(tr("Toggle Hotkey"), r);
	}

	auto *colorEd = new QLineEdit(tabGeneral);
	colorEd->setPlaceholderText(tr("#2EA043"));

	auto *pickColor = new QPushButton(tr("Pick…"), tabGeneral);
	auto *colorRow = new QHBoxLayout();
	colorRow->addWidget(colorEd, 1);
	colorRow->addWidget(pickColor);

	generalForm->addRow(tr("Title"), titleEd);
	generalForm->addRow(tr("Dock highlight color"), colorRow);

	auto *featuresForm = new QFormLayout(tabFeatures);
	auto *orderCmb = new QComboBox(tabFeatures);
	orderCmb->addItem(tr("Linear"), 0);
	orderCmb->addItem(tr("Randomized"), 1);

	auto *visibleSpin = new QSpinBox(tabFeatures);
	visibleSpin->setRange(1, 600);
	visibleSpin->setSuffix(tr(" s"));
	visibleSpin->setToolTip(tr("Duration each item is visible during carousel"));

	auto *intervalSpin = new QSpinBox(tabFeatures);
	intervalSpin->setRange(0, 600);
	intervalSpin->setSuffix(tr(" s"));
	intervalSpin->setToolTip(tr("Time until next item during carousel"));

	auto *loopChk = new QCheckBox(tr("Loop group"), tabFeatures);
	auto *exclusiveChk = new QCheckBox(tr("Exclusive"), tabFeatures);

	featuresForm->addRow(tr("Carousel order"), orderCmb);
	featuresForm->addRow(tr("Visible duration"), visibleSpin);
	featuresForm->addRow(tr("Time between items"), intervalSpin);
	{
		auto *checks = new QHBoxLayout();
		checks->setContentsMargins(0, 0, 0, 0);
		checks->setSpacing(12);
		checks->addWidget(loopChk);
		checks->addWidget(exclusiveChk);
		checks->addStretch(1);
		featuresForm->addRow(QString(), checks);
	}

	right->addWidget(tabs);

	auto *membersLbl = new QLabel(tr("Members (lower thirds)"), &dlg);
	right->addWidget(membersLbl);

	auto *members = new QListWidget(&dlg);
	members->setSelectionMode(QAbstractItemView::NoSelection);
	right->addWidget(members, 1);

	auto *btnApply = new QPushButton(tr("Apply"), &dlg);
	btnApply->setDefault(true);
	right->addWidget(btnApply, 0, Qt::AlignRight);

	root->addLayout(right, 2);

	auto refreshList = [&]() {
		carList->clear();
		for (const auto &c : vflow::groups_const()) {
			auto *it = new QListWidgetItem(QString::fromStdString(c.title.empty() ? c.id : c.title));
			it->setData(Qt::UserRole, QString::fromStdString(c.id));
			carList->addItem(it);
		}
	};

	auto refreshMembers = [&](const std::string &groupId) {
		members->clear();
		QSet<QString> inSet;
		if (auto *car = vflow::get_group_by_id(groupId)) {
			for (const auto &m : car->members)
				inSet.insert(QString::fromStdString(m));
		}

		for (const auto &lt : vflow::all_const()) {
			const auto owners = vflow::groups_containing(lt.id);
			if (!owners.empty() && owners.front() != groupId)
				continue;

			auto *it = new QListWidgetItem(QString::fromStdString(lt.label.empty() ? lt.id : lt.label));
			it->setData(Qt::UserRole, QString::fromStdString(lt.id));
			it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
			it->setCheckState(inSet.contains(QString::fromStdString(lt.id)) ? Qt::Checked : Qt::Unchecked);
			members->addItem(it);
		}
	};

	auto updateStartStopButtons = [&]() {
		auto *it = carList->currentItem();
		const QString qid = it ? it->data(Qt::UserRole).toString() : QString();
		if (qid.isEmpty()) {
			btnStart->setEnabled(false);
			btnStop->setEnabled(false);
			return;
		}

		bool running = false;
		auto rt = g_groupRuns.find(qid);
		if (rt != g_groupRuns.end())
			running = rt->running;

		bool hasMembers = false;
		if (auto *car = vflow::get_group_by_id(qid.toStdString()))
			hasMembers = !car->members.empty();

		btnStart->setEnabled(!running && hasMembers);
		btnStop->setEnabled(running);
	};

	auto loadSelected = [&]() {
		auto *it = carList->currentItem();
		const QString qid = it ? it->data(Qt::UserRole).toString() : QString();
		if (qid.isEmpty()) {
			titleEd->clear();
			orderCmb->setCurrentIndex(0);
			loopChk->setChecked(true);
			exclusiveChk->setChecked(false);
			visibleSpin->setValue(15);
			intervalSpin->setValue(5);
			colorEd->clear();
			toggleHkEdit->setKeySequence(QKeySequence());
			members->clear();
			updateStartStopButtons();
			return;
		}

		if (auto *car = vflow::get_group_by_id(qid.toStdString())) {
			titleEd->setText(QString::fromStdString(car->title));
			orderCmb->setCurrentIndex(car->order_mode == 1 ? 1 : 0);
			loopChk->setChecked(car->loop);
			exclusiveChk->setChecked(car->exclusive);
			{
				const int visSec = std::max(1, (car->visible_ms + 500) / 1000);
				const int intSec = std::max(0, (car->interval_ms + 500) / 1000);
				visibleSpin->setValue(visSec);
				intervalSpin->setValue(intSec);
			}
			colorEd->setText(QString::fromStdString(car->dock_color));
			loadedToggleSeq = vflow::hotkeys::group_sequence(qid);
			toggleHkEdit->setKeySequence(loadedToggleSeq);
			refreshMembers(car->id);
		}
		updateStartStopButtons();
	};

	QObject::connect(toggleHkClear, &QPushButton::clicked, &dlg,
			 [&]() { toggleHkEdit->setKeySequence(QKeySequence()); });

	refreshList();
	if (carList->count() > 0)
		carList->setCurrentRow(0);
	loadSelected();

	QObject::connect(carList, &QListWidget::currentItemChanged, &dlg,
			 [&](QListWidgetItem *, QListWidgetItem *) { loadSelected(); });

	QObject::connect(pickColor, &QPushButton::clicked, &dlg, [&]() {
		const QColor cur(colorEd->text().trimmed());
		const QColor picked =
			QColorDialog::getColor(cur.isValid() ? cur : QColor("#2EA043"), &dlg, tr("Pick dock color"));
		if (picked.isValid())
			colorEd->setText(picked.name(QColor::HexRgb));
	});

	QObject::connect(btnAdd, &QPushButton::clicked, &dlg, [&]() {
		const std::string id = vflow::add_default_group();
		refreshList();
		for (int i = 0; i < carList->count(); ++i) {
			auto *it = carList->item(i);
			if (it && it->data(Qt::UserRole).toString().toStdString() == id) {
				carList->setCurrentRow(i);
				break;
			}
		}
	});

	QObject::connect(btnSort, &QPushButton::clicked, &dlg, [&]() {
		(void)vflow::sort_lower_thirds_by_group();
		emit requestSave();
	});

	QObject::connect(btnDel, &QPushButton::clicked, &dlg, [&]() {
		auto *it = carList->currentItem();
		if (!it)
			return;
		const QString qid = it->data(Qt::UserRole).toString();
		if (qid.isEmpty())
			return;

		const auto res = QMessageBox::question(&dlg, tr("Delete group"),
						       tr("Delete this group? This will not delete any lower thirds."),
						       QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (res != QMessageBox::Yes)
			return;

		stopGroupRun(qid);
		vflow::remove_group(qid.toStdString());
		refreshList();
		if (carList->count() > 0)
			carList->setCurrentRow(0);
		loadSelected();
	});

	QObject::connect(btnStart, &QPushButton::clicked, &dlg, [&]() {
		auto *it = carList->currentItem();
		if (!it)
			return;
		const QString qid = it->data(Qt::UserRole).toString();
		if (qid.isEmpty())
			return;
		startGroupRun(this, qid);
		updateStartStopButtons();
	});

	QObject::connect(btnStop, &QPushButton::clicked, &dlg, [&]() {
		auto *it = carList->currentItem();
		if (!it)
			return;
		const QString qid = it->data(Qt::UserRole).toString();
		if (qid.isEmpty())
			return;
		stopGroupRun(qid);
		updateStartStopButtons();
	});

	QObject::connect(btnApply, &QPushButton::clicked, &dlg, [&]() {
		auto *it = carList->currentItem();
		if (!it)
			return;
		const QString qid = it->data(Qt::UserRole).toString();
		if (qid.isEmpty())
			return;

		auto *car = vflow::get_group_by_id(qid.toStdString());
		if (!car)
			return;

		group_cfg upd = *car;
		upd.title = titleEd->text().trimmed().toStdString();
		upd.order_mode = orderCmb->currentData().toInt();
		upd.loop = loopChk->isChecked();
		upd.exclusive = exclusiveChk->isChecked();
		upd.visible_ms = std::max(1, visibleSpin->value()) * 1000;
		upd.interval_ms = std::max(0, intervalSpin->value()) * 1000;
		upd.dock_color = colorEd->text().trimmed().toStdString();

		// Only touch the OBS bindings when the field was edited, so bindings made in
		// OBS Settings > Hotkeys (e.g. mouse buttons) are not cleared by accident.
		const QKeySequence seq = toggleHkEdit->keySequence();
		if (seq != loadedToggleSeq) {
			if (!vflow::hotkeys::is_supported(seq)) {
				QMessageBox::warning(
					&dlg, tr("Unsupported hotkey"),
					tr("This key cannot be used as an OBS hotkey. Please pick another one, "
					   "or assign it in OBS Settings > Hotkeys."));
				return;
			}
			vflow::hotkeys::set_group_sequence(qid, seq);
			upd.toggle_hotkey = seq.toString(QKeySequence::PortableText).toStdString();
		}

		vflow::update_group(upd);

		std::vector<std::string> mem;
		for (int i = 0; i < members->count(); ++i) {
			auto *mit = members->item(i);
			if (!mit)
				continue;
			if (mit->checkState() == Qt::Checked)
				mem.push_back(mit->data(Qt::UserRole).toString().toStdString());
		}
		vflow::set_group_members(qid.toStdString(), mem);

		refreshMembers(qid.toStdString());
		updateStartStopButtons();

		it->setText(QString::fromStdString(upd.title.empty() ? upd.id : upd.title));
		rebuildList();
		dlg.accept();
	});

	dlg.exec();

	rebuildList();
}

void LowerThirdDock::rebuildList()
{
	for (auto &row : rows) {
		if (row.row)
			row.row->deleteLater();
	}
	rows.clear();

	const auto &items = vflow::all();
	const QString outDir = QString::fromStdString(vflow::output_dir());

	const qreal dpr = devicePixelRatio();
	const QColor text = QApplication::palette().color(QPalette::Text);
	QColor tileBg = QApplication::palette().color(QPalette::Highlight);
	tileBg.setAlphaF(0.35f);

	for (const auto &cfg : items) {
		LowerThirdRowUi ui;
		ui.id = QString::fromStdString(cfg.id);

		const QString displayLabel = QString::fromStdString(cfg.label.empty() ? cfg.title : cfg.label);

		auto *rowFrame = new QFrame(listContainer);
		rowFrame->setObjectName(QStringLiteral("sltRowFrame"));
		rowFrame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
		rowFrame->setProperty("sltActive", QVariant(vflow::is_visible(cfg.id)));

		const auto carIds = vflow::groups_containing(cfg.id);
		if (!carIds.empty()) {
			rowFrame->setProperty("sltInGroup", QVariant(true));

			std::string col = "#2EA043";
			if (auto *car = vflow::get_group_by_id(carIds.front())) {
				if (!car->dock_color.empty())
					col = car->dock_color;
			}
			rowFrame->setProperty("sltGroupColor", QString::fromStdString(col));
		} else {
			rowFrame->setProperty("sltInGroup", QVariant(false));
			rowFrame->setProperty("sltGroupColor", QString());
		}

		rowFrame->setCursor(Qt::PointingHandCursor);
		rowFrame->setToolTip(QStringLiteral("%1\n%2").arg(displayLabel, tr("Click to show or hide on stream")));
		applyGroupRowStyle(rowFrame);

		auto *h = new QHBoxLayout(rowFrame);
		h->setContentsMargins(8, 5, 6, 5);
		h->setSpacing(8);

		// Thumbnail. Always present so rows line up; a letter tile stands in
		// for a missing picture.
		auto *thumb = new QLabel(rowFrame);
		thumb->setObjectName(QStringLiteral("sltRowThumbnail"));
		thumb->setFixedSize(kRowThumbSize, kRowThumbSize);

		QPixmap px;
		if (!cfg.profile_picture.empty() && !outDir.isEmpty())
			px.load(QDir(outDir).filePath(QString::fromStdString(cfg.profile_picture)));
		thumb->setPixmap(
			px.isNull() ? initialsThumbnail(displayLabel, kRowThumbSize, kRowThumbRadius, tileBg, text, dpr)
				    : roundedThumbnail(px, kRowThumbSize, kRowThumbRadius, dpr));
		h->addWidget(thumb, 0, Qt::AlignVCenter);
		ui.thumbnailLbl = thumb;

		// Name and status line. Ignored width policy: long names clip instead
		// of forcing a horizontal scrollbar on the list.
		auto *labelCol = new QWidget(rowFrame);
		auto *labelColLayout = new QVBoxLayout(labelCol);
		labelColLayout->setContentsMargins(0, 0, 0, 0);
		labelColLayout->setSpacing(1);

		auto *label = new QLabel(displayLabel, labelCol);
		label->setObjectName(QStringLiteral("sltRowLabel"));
		label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
		label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

		// Starts as a blank line so the row keeps its height before any
		// countdown or hotkey hint is shown.
		auto *sub = new QLabel(QString(QChar::Nbsp), labelCol);
		sub->setObjectName(QStringLiteral("sltRowSubLabel"));
		sub->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
		sub->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

		labelColLayout->addWidget(label);
		labelColLayout->addWidget(sub);
		h->addWidget(labelCol, 1, Qt::AlignVCenter);

		// LIVE badge. It keeps its slot while hidden so the row never shifts
		// when a lower third goes on or off stream.
		auto *live = new QLabel(tr("LIVE"), rowFrame);
		live->setObjectName(QStringLiteral("sltLivePill"));
		live->setToolTip(tr("Visible on stream"));
		live->setAlignment(Qt::AlignCenter);
		{
			QSizePolicy sp = live->sizePolicy();
			sp.setRetainSizeWhenHidden(true);
			live->setSizePolicy(sp);
		}
		live->setVisible(vflow::is_visible(cfg.id));
		h->addWidget(live, 0, Qt::AlignVCenter);

		ui.labelLbl = label;
		ui.subLbl = sub;
		ui.liveLbl = live;
		{
			const QKeySequence seq = vflow::hotkeys::item_sequence(ui.id);
			if (!seq.isEmpty())
				ui.hotkeyText = tr("Hotkey: %1").arg(seq.toString(QKeySequence::NativeText));
		}

		// Row actions. OBS theme icon classes first, drawn/standard icons as fallback.
		auto *st = rowFrame->style();
		auto makeRowBtn = [rowFrame](const QIcon &icon, const char *themeClass, const QString &tip) {
			auto *b = new QToolButton(rowFrame);
			b->setObjectName(QStringLiteral("sltRowBtn"));
			b->setAutoRaise(true);
			b->setCursor(Qt::PointingHandCursor);
			b->setFocusPolicy(Qt::NoFocus);
			b->setIcon(icon);
			b->setIconSize(QSize(16, 16));
			b->setFixedSize(24, 24);
			b->setToolTip(tip);
			if (themeClass)
				b->setProperty("class", QString::fromLatin1(themeClass));
			return b;
		};

		auto *upBtn = makeRowBtn(themeIcon("go-up", st, QStyle::SP_ArrowUp), "icon-up", tr("Move up"));
		auto *downBtn =
			makeRowBtn(themeIcon("go-down", st, QStyle::SP_ArrowDown), "icon-down", tr("Move down"));
		auto *cloneBtn = makeRowBtn(glyphIcon("clone", text), nullptr, tr("Clone lower third"));
		auto *settingsBtn = makeRowBtn(themeIcon("document-edit", st, QStyle::SP_FileDialogDetailedView),
					       "icon-gear", tr("Edit lower third"));
		auto *removeBtn = makeRowBtn(themeIcon("edit-delete", st, QStyle::SP_DialogCloseButton), "icon-trash",
					     tr("Remove lower third"));

		h->addWidget(upBtn);
		h->addWidget(downBtn);
		h->addWidget(cloneBtn);
		h->addWidget(settingsBtn);
		h->addWidget(removeBtn);

		listLayout->addWidget(rowFrame);

		ui.row = rowFrame;
		ui.upBtn = upBtn;
		ui.downBtn = downBtn;
		ui.cloneBtn = cloneBtn;
		ui.settingsBtn = settingsBtn;
		ui.removeBtn = removeBtn;

		const QString id = ui.id;

		rowFrame->installEventFilter(this);
		label->installEventFilter(this);
		sub->installEventFilter(this);
		thumb->installEventFilter(this);

		connect(cloneBtn, &QToolButton::clicked, this, [this, id]() { handleClone(id); });
		connect(upBtn, &QToolButton::clicked, this, [this, id]() {
			vflow::move_lower_third(id.toStdString(), -1);
			emit requestSave();
		});
		connect(downBtn, &QToolButton::clicked, this, [this, id]() {
			vflow::move_lower_third(id.toStdString(), +1);
			emit requestSave();
		});
		connect(settingsBtn, &QToolButton::clicked, this, [this, id]() { handleOpenSettings(id); });
		connect(removeBtn, &QToolButton::clicked, this, [this, id]() { handleRemove(id); });

		rows.push_back(ui);
	}

	// Rows stack from the top; the empty-state hint is centred in the panel.
	listLayout->setAlignment(rows.isEmpty() ? Qt::AlignVCenter : Qt::AlignTop);

	updateRowActiveStyles();
	updateRowCountdowns();
	if (emptyStateLbl_)
		emptyStateLbl_->setVisible(rows.isEmpty());
}

void LowerThirdDock::updateRowActiveStyles()
{
	for (auto &row : rows) {
		const bool active = vflow::is_visible(row.id.toStdString());

		if (row.row) {
			row.row->setProperty("sltActive", QVariant(active));
			applyGroupRowStyle(row.row);
			row.row->style()->unpolish(row.row);
			row.row->style()->polish(row.row);
			row.row->update();
		}

		if (row.liveLbl)
			row.liveLbl->setVisible(active);
	}
}

bool LowerThirdDock::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::MouseButtonPress) {
		auto *me = static_cast<QMouseEvent *>(event);
		if (me->button() == Qt::LeftButton) {
			for (auto &r : rows) {
				if (watched == r.row || watched == r.labelLbl || watched == r.subLbl ||
				    watched == r.thumbnailLbl) {
					handleToggleVisible(r.id);
					return true;
				}
			}
		}
	}
	return QWidget::eventFilter(watched, event);
}

void LowerThirdDock::toggleLowerThird(const QString &id)
{
	if (vflow::get_by_id(id.toStdString()))
		handleToggleVisible(id);
}

void LowerThirdDock::toggleGroup(const QString &id)
{
	if (isGroupRunning(id))
		stopGroupRun(id);
	else
		startGroupRun(this, id);
	updateRowCountdowns();
	updateRowActiveStyles();
}

void LowerThirdDock::handleToggleVisible(const QString &id)
{
	const std::string sid = id.toStdString();

	for (const auto &g : vflow::groups_const()) {
		bool isMember = false;
		for (const auto &m : g.members) {
			if (m == sid) {
				isMember = true;
				break;
			}
		}
		if (!isMember)
			continue;

		const QString groupId = QString::fromStdString(g.id);
		auto it = g_groupRuns.find(groupId);
		if (it != g_groupRuns.end() && it->running) {
			stopGroupRun(groupId);
		}
	}
	const bool wasVisible = vflow::is_visible(sid);
	const bool ok = vflow::toggle_visible_persist(sid);

	if (!ok)
		return;

	const bool nowVisible = vflow::is_visible(sid);

	if (!wasVisible && nowVisible) {
		if (auto *cfg = vflow::get_by_id(sid)) {
			const int every = cfg->repeat_every_sec;
			int visibleSec = cfg->repeat_visible_sec;

			if (every > 0 || visibleSec > 0) {
				if (every > 0 && visibleSec <= 0)
					visibleSec = 3;

				if (visibleSec > 0) {
					const qint64 now = QDateTime::currentMSecsSinceEpoch();
					offAtMs_[id] = now + (qint64)visibleSec * 1000;
				}

				if (every > 0 && !nextOnMs_.contains(id)) {
					const qint64 now = QDateTime::currentMSecsSinceEpoch();
					nextOnMs_[id] = now + (qint64)every * 1000;
				}
			}
		}
	}

	updateRowCountdowns();
	emit requestSave();
}

void LowerThirdDock::handleClone(const QString &id)
{
	if (!vflow::has_output_dir())
		return;

	vflow::clone_lower_third(id.toStdString(), false);
	emit requestSave();
}

void LowerThirdDock::handleOpenSettings(const QString &id)
{
	auto *dlg = new vflow::ui::LowerThirdSettingsDialog(this);
	dlg->setAttribute(Qt::WA_DeleteOnClose, true);
	dlg->setLowerThirdId(id);
	dlg->setWindowModality(Qt::NonModal);
	dlg->setModal(false);
	dlg->show();
	dlg->raise();
	dlg->activateWindow();
}

void LowerThirdDock::handleRemove(const QString &id)
{
	if (!vflow::has_output_dir())
		return;

	const std::string sid = id.toStdString();
	const auto cars = vflow::groups_containing(sid);

	QString msg = tr("Remove this lower third?\n\nThis action cannot be undone.");
	if (!cars.empty()) {
		msg += tr("\n\nNote: This lower third is part of %1 group(s) and will also be removed from those groups.")
			       .arg((int)cars.size());
	}

	const auto res = QMessageBox::question(this, tr("Confirm Removal"), msg, QMessageBox::Yes | QMessageBox::No,
					       QMessageBox::No);
	if (res != QMessageBox::Yes)
		return;

	vflow::remove_lower_third(id.toStdString());

	nextOnMs_.remove(id);
	offAtMs_.remove(id);
	reShowAtMs_.remove(id);

	emit requestSave();
}

QString LowerThirdDock::formatCountdownMs(qint64 ms)
{
	if (ms < 0)
		ms = 0;
	const qint64 totalSec = ms / 1000;
	const qint64 m = totalSec / 60;
	const qint64 s = totalSec % 60;
	return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}

void LowerThirdDock::updateRowCountdownFor(const LowerThirdRowUi &rowUi)
{
	if (!rowUi.subLbl)
		return;

	const auto *cfg = vflow::get_by_id(rowUi.id.toStdString());
	if (!cfg) {
		rowUi.subLbl->setText(QString(QChar::Nbsp));
		return;
	}

	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const QString qid = rowUi.id;

	// Nothing scheduled: show the hotkey hint (if any). The line stays, blank,
	// so every row keeps the same height.
	auto showIdle = [&rowUi]() {
		rowUi.subLbl->setText(rowUi.hotkeyText.isEmpty() ? QString(QChar::Nbsp) : rowUi.hotkeyText);
	};

	if (g_groupHideAtMs.contains(qid) && vflow::is_visible(cfg->id)) {
		const qint64 leftHide = g_groupHideAtMs.value(qid) - now;
		rowUi.subLbl->setText(tr("Hides in %1").arg(formatCountdownMs(leftHide)));
		return;
	}

	const int every = cfg->repeat_every_sec;
	const int keepVisible = cfg->repeat_visible_sec;

	if (every <= 0) {
		if (keepVisible <= 0) {
			showIdle();
			return;
		}

		const bool isVis = vflow::is_visible(cfg->id);
		if (!isVis) {
			showIdle();
			return;
		}

		if (!offAtMs_.contains(qid))
			offAtMs_[qid] = now + (qint64)keepVisible * 1000;

		const qint64 leftHide = offAtMs_[qid] - now;
		rowUi.subLbl->setText(tr("Hides in %1").arg(formatCountdownMs(leftHide)));
		return;
	}

	if (!nextOnMs_.contains(qid)) {
		nextOnMs_[qid] = now + (qint64)every * 1000;
	}

	const bool isVis = vflow::is_visible(cfg->id);

	QStringList parts;

	if (isVis && offAtMs_.contains(qid)) {
		const qint64 leftHide = offAtMs_[qid] - now;
		parts << (tr("Hides in %1").arg(formatCountdownMs(leftHide)));
	} else if (isVis) {
		parts << tr("Visible");
	}

	if (nextOnMs_.contains(qid)) {
		const qint64 leftNext = nextOnMs_[qid] - now;
		parts << (tr("Next in %1").arg(formatCountdownMs(leftNext)));
	} else {
		parts << tr("Repeating");
	}

	rowUi.subLbl->setText(parts.join(QStringLiteral(" • ")));
}

void LowerThirdDock::updateRowCountdowns()
{
	for (const auto &r : rows)
		updateRowCountdownFor(r);
}

void LowerThirdDock::connectObsSignals()
{
	if (obsSignalsConnected_)
		return;

	obsSignalHandler_ = obs_get_signal_handler();
	if (!obsSignalHandler_)
		return;

	signal_handler_connect(obsSignalHandler_, "source_create", onObsSourceEvent, this);
	signal_handler_connect(obsSignalHandler_, "source_destroy", onObsSourceEvent, this);
	signal_handler_connect(obsSignalHandler_, "source_rename", onObsSourceEvent, this);
	obsSignalsConnected_ = true;
}

void LowerThirdDock::disconnectObsSignals()
{
	if (!obsSignalsConnected_ || !obsSignalHandler_)
		return;

	signal_handler_disconnect(obsSignalHandler_, "source_create", onObsSourceEvent, this);
	signal_handler_disconnect(obsSignalHandler_, "source_destroy", onObsSourceEvent, this);
	signal_handler_disconnect(obsSignalHandler_, "source_rename", onObsSourceEvent, this);
	obsSignalsConnected_ = false;
	obsSignalHandler_ = nullptr;
}

static bool isBrowserSource(obs_source_t *src)
{
	if (!src)
		return false;

	const char *id = obs_source_get_unversioned_id(src);
	if (!id)
		id = obs_source_get_id(src);

	return id && strcmp(id, "browser_source") == 0;
}

void LowerThirdDock::onObsSourceEvent(void *data, calldata_t *cd)
{
	auto *self = static_cast<LowerThirdDock *>(data);
	if (!self || !cd)
		return;

	auto *src = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!isBrowserSource(src))
		return;

	// Follow renames of the overlay source instead of losing the binding.
	const char *prevName = calldata_string(cd, "prev_name");
	const char *newName = calldata_string(cd, "new_name");
	const QString from = prevName ? QString::fromUtf8(prevName) : QString();
	const QString to = newName ? QString::fromUtf8(newName) : QString();

	QMetaObject::invokeMethod(
		self,
		[self, from, to]() {
			if (!from.isEmpty() && !to.isEmpty() &&
			    from == QString::fromStdString(vflow::target_browser_source_name()))
				vflow::set_target_browser_source_name(to.toStdString());
			self->populateBrowserSources(true);
			self->updateSetupState();
		},
		Qt::QueuedConnection);
}

void LowerThirdDock::refreshBrowserSources()
{
	populateBrowserSources(true);
}

void LowerThirdDock::onSceneCollectionChanged()
{
	// OBS reports "finished loading" and "scene collection changed" together at
	// startup; coalesce them so the overlay is rebuilt (and reloaded) only once.
	if (collectionRefreshPending_)
		return;
	collectionRefreshPending_ = true;
	QTimer::singleShot(150, this, [this]() {
		collectionRefreshPending_ = false;
		applySceneCollection();
	});
}

void LowerThirdDock::applySceneCollection()
{
	populateBrowserSources(false);

	const QString selected = QString::fromStdString(vflow::target_browser_source_name());
	if (!selected.isEmpty() && vflow::has_output_dir()) {
		vflow::rebuild_and_swap();
	}
	updateSetupState();
}

void LowerThirdDock::setUpdateAvailable(const QString &remoteVersion, const QString &localVersion, const QString &url)
{
	updateRemote_ = remoteVersion.trimmed();
	updateLocal_ = localVersion.trimmed();
	updateUrl_ = url.trimmed();
	if (updateUrl_.isEmpty())
		updateUrl_ = QStringLiteral("https://github.com/mmlTools/vinci-flow/releases/latest");

	const bool show = !updateRemote_.isEmpty() && updateRemote_ != updateLocal_;
	if (!updateFrame_ || !updateLabel_)
		return;

	updateFrame_->setVisible(show);
	if (show)
		updateLabel_->setText(tr("VinciFlow %1 is available (you have %2).").arg(updateRemote_, updateLocal_));
}

} // namespace vflow::ui

void LowerThird_create_dock()
{
	if (g_dockWidget)
		return;

	auto *panel = new vflow::ui::LowerThirdDock(nullptr);
	panel->init();

	obs_frontend_add_dock_by_id(sltDockId, sltDockTitle, panel);

	g_dockWidget = panel;
	LOGI("Dock created");
}

void LowerThird_destroy_dock()
{
	if (!g_dockWidget)
		return;

	obs_frontend_remove_dock(sltDockId);

	g_dockWidget = nullptr;
	LOGI("Dock destroyed");
}

vflow::ui::LowerThirdDock *LowerThird_get_dock()
{
	return qobject_cast<vflow::ui::LowerThirdDock *>(g_dockWidget);
}
