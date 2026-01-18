// dock.cpp
#define LOG_TAG "[" PLUGIN_NAME "][dock]"
#include "dock.hpp"

#include "core.hpp"
#include "settings.hpp"
#include "widget.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <cstring>

#include <QVBoxLayout>
#include <QHBoxLayout>
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
#include <QShortcut>
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

static QWidget *g_dockWidget = nullptr;



struct GroupRuntime {
	bool running = false;
	int index = 0; // position in seq
	bool phaseShow = true;
	QString currentId;
	qint64 hideAtMs = 0;
	QVector<int> seq;
};

static QHash<QString, GroupRuntime> g_groupRuns;
static QHash<QString, qint64> g_groupHideAtMs; // ltId -> hideAtMs (only while a group-run is showing it)

static void applyGroupRowStyle(QFrame *rowFrame)
{
	if (!rowFrame)
		return;

	const bool active = rowFrame->property("sltActive").toBool();
	const bool inCar = rowFrame->property("sltInGroup").toBool();
	const QString col = rowFrame->property("sltGroupColor").toString();

	// Only apply custom background when inactive; active styling is handled by QSS.
	// We still keep the dynamic properties so the global stylesheet can provide a default green tint.
	if (!active && inCar && !col.isEmpty()) {
		QColor c(col);
		if (c.isValid()) {
			rowFrame->setStyleSheet(QStringLiteral(
				"QFrame#sltRowFrame{ background: rgba(%1,%2,%3,0.14); border: 1px solid rgba(%1,%2,%3,0.55); }")
							.arg(c.red())
							.arg(c.green())
							.arg(c.blue()));
			return;
		}
	}

	// Clear any per-row override.
	rowFrame->setStyleSheet(QString());
}

static void stopGroupRun(const QString &groupId)
{
	auto it = g_groupRuns.find(groupId);
	if (it == g_groupRuns.end())
		return;

	// Best-effort hide currently shown item
	if (!it->currentId.isEmpty()) {
		g_groupHideAtMs.remove(it->currentId);
		smart_lt::set_visible_persist(it->currentId.toStdString(), false);
	}

	it->running = false;
	it->index = 0;
	it->phaseShow = true;
	it->currentId.clear();
	it->hideAtMs = 0;
	it->seq.clear();
}

static void scheduleGroupStep(smart_lt::ui::LowerThirdDock *dock, const QString &groupId);

static void startGroupRun(smart_lt::ui::LowerThirdDock *dock, const QString &groupId)
{
	if (!dock)
		return;

	auto *car = smart_lt::get_group_by_id(groupId.toStdString());
	if (!car || car->members.empty())
		return;

	auto &rt = g_groupRuns[groupId];
	rt.running = true;
	rt.index = 0;
	rt.phaseShow = true;
	rt.currentId.clear();
	rt.hideAtMs = 0;
	rt.seq.clear();

	scheduleGroupStep(dock, groupId);
}

static void scheduleGroupStep(smart_lt::ui::LowerThirdDock *dock, const QString &groupId)
{
	if (!dock)
		return;

	auto it = g_groupRuns.find(groupId);
	if (it == g_groupRuns.end() || !it->running)
		return;

	auto *car = smart_lt::get_group_by_id(groupId.toStdString());
	if (!car || car->members.empty()) {
		stopGroupRun(groupId);
		return;
	}

	const int visibleMs = std::max(0, car->visible_ms);
	const int intervalMs = std::max(0, car->interval_ms);
	const int count = (int)car->members.size();

	// Build / refresh sequence
	if (it->seq.size() != count) {
		it->seq.clear();
		it->seq.reserve(count);
		for (int i = 0; i < count; ++i)
			it->seq.push_back(i);
		if (car->order_mode == 1) {
			// Shuffle
			auto *rng = QRandomGenerator::global();
			for (int i = count - 1; i > 0; --i) {
				const int j = (int)rng->bounded((quint32)(i + 1));
				std::swap(it->seq[i], it->seq[j]);
			}
		}
		it->index = 0;
	}

	if (it->phaseShow) {
		// hide previous
		if (!it->currentId.isEmpty()) {
			g_groupHideAtMs.remove(it->currentId);
			smart_lt::set_visible_persist(it->currentId.toStdString(), false);
		}

		// End condition (non-loop): stop before showing beyond last
		if (!car->loop && it->index >= count) {
			stopGroupRun(groupId);
			return;
		}

		// Loop condition: wrap and reshuffle per cycle if randomized
		if (it->index >= count) {
			it->index = 0;
			if (car->order_mode == 1) {
				auto *rng = QRandomGenerator::global();
				for (int i = count - 1; i > 0; --i) {
					const int j = (int)rng->bounded((quint32)(i + 1));
					std::swap(it->seq[i], it->seq[j]);
				}
			}
		}

		const int memberIdx = it->seq.value(it->index, 0);
		const QString nextId = QString::fromStdString(car->members[(size_t)memberIdx]);
		it->currentId = nextId;
		it->hideAtMs = QDateTime::currentMSecsSinceEpoch() + (qint64)visibleMs;
		g_groupHideAtMs[nextId] = it->hideAtMs;

		smart_lt::set_visible_persist(nextId.toStdString(), true);

		it->phaseShow = false;
		QTimer::singleShot(visibleMs, dock, [dock, groupId]() { scheduleGroupStep(dock, groupId); });
	} else {
		// hide current and advance index
		if (!it->currentId.isEmpty()) {
			g_groupHideAtMs.remove(it->currentId);
			smart_lt::set_visible_persist(it->currentId.toStdString(), false);
		}

		it->currentId.clear();
		it->hideAtMs = 0;
		it->index++;

		// If we just finished the last item and loop is disabled, stop now.
		if (!car->loop && it->index >= count) {
			stopGroupRun(groupId);
			return;
		}

		it->phaseShow = true;
		QTimer::singleShot(intervalMs, dock, [dock, groupId]() { scheduleGroupStep(dock, groupId); });
	}
}



namespace smart_lt::ui {

// -------------------------
// NEW: Core event bus hookup
// -------------------------
void LowerThirdDock::coreEventThunk(const smart_lt::core_event &ev, void *user)
{
	auto *self = static_cast<LowerThirdDock *>(user);
	if (!self)
		return;

	// Always marshal to Qt thread to keep UI safe
	QMetaObject::invokeMethod(self, [self, ev]() { self->onCoreEvent(ev); }, Qt::QueuedConnection);
}

LowerThirdDock::~LowerThirdDock()
{
	disconnectObsSignals();
	if (coreListenerToken_) {
		smart_lt::remove_event_listener(coreListenerToken_);
		coreListenerToken_ = 0;
	}
}

void LowerThirdDock::onCoreEvent(const smart_lt::core_event &ev)
{
	switch (ev.type) {
	case smart_lt::event_type::VisibilityChanged: {
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

			if (row.visibleCheck) {
				row.visibleCheck->blockSignals(true);
				row.visibleCheck->setChecked(active);
				row.visibleCheck->blockSignals(false);
			}

			updateRowCountdownFor(row);
			break;
		}
		break;
	}

	case smart_lt::event_type::ListChanged:
	case smart_lt::event_type::Reloaded: {
		rebuildList();
		updateRowCountdowns();
		break;
	}

	default:
		break;
	}
}

LowerThirdDock::LowerThirdDock(QWidget *parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("LowerThirdDock"));
	setAttribute(Qt::WA_StyledBackground, true);

	setStyleSheet(R"(
#LowerThirdDock { background: rgba(39, 42, 51, 1.0); }
QFrame#sltUpdateBanner {
  border: 1px solid rgba(90,140,255,0.45);
  border-radius: 10px;
  background: rgba(90,140,255,0.14);
  padding: 8px;
}
QLabel#sltUpdateBannerLabel { color: rgba(240,246,252,0.95); font-weight: 600; }
QPushButton#sltUpdateBannerBtn {
  border: 1px solid rgba(255,255,255,0.18);
  border-radius: 8px;
  padding: 6px 10px;
  background: rgba(255,255,255,0.06);
}
QPushButton#sltUpdateBannerBtn:hover { background: rgba(255,255,255,0.10); }
QFrame#sltRowFrame {
  border: 1px solid rgba(255,255,255,40);
  border-radius: 4px;
  padding: 4px 6px;
  background: transparent;
}
QFrame#sltRowFrame:hover { background: rgba(255,255,255,0.04); }
QFrame#sltRowFrame[sltInGroup="true"] {
  background: rgba(46,160,67,0.14);
  border: 1px solid rgba(46,160,67,0.55);
}
QFrame#sltRowFrame[sltActive="true"] {
  background: rgba(88,166,255,0.16);
  border: 1px solid rgba(88,166,255,0.9);
}
QLabel#sltRowLabel { color: #f0f6fc; font-weight: 500; }
QLabel#sltRowSubLabel { color: rgba(240,246,252,0.72); font-size: 11px; }
QLabel#sltRowThumbnail { border-radius: 16px; background: rgba(0,0,0,0.35); }
QScrollArea#LowerThirdContent QPushButton { border: none; background: transparent; padding: 2px; }
QScrollArea#LowerThirdContent QPushButton:hover { background: rgba(255,255,255,0.06); border-radius: 3px; }
)");

	auto *rootLayout = new QVBoxLayout(this);
	rootLayout->setContentsMargins(8, 8, 8, 8);
	rootLayout->setSpacing(6);

	auto *st = style();

	// -------------------------
	// Update banner (hidden unless API reports a newer plugin version)
	// Placed above the Resources selector row.
	// -------------------------
	{
		updateFrame_ = new QFrame(this);
		updateFrame_->setObjectName(QStringLiteral("sltUpdateBanner"));
		updateFrame_->setFrameShape(QFrame::NoFrame);
		updateFrame_->setVisible(false);

		auto *row = new QHBoxLayout(updateFrame_);
		row->setContentsMargins(8, 6, 8, 6);
		row->setSpacing(10);

		auto *ico = new QLabel(updateFrame_);
		ico->setFixedSize(18, 18);
		ico->setAlignment(Qt::AlignCenter);
		ico->setPixmap(st->standardIcon(QStyle::SP_ArrowUp).pixmap(16, 16));
		row->addWidget(ico);

		updateLabel_ = new QLabel(tr("New Smart Lower Thirds update available."), updateFrame_);
		updateLabel_->setObjectName(QStringLiteral("sltUpdateBannerLabel"));
		updateLabel_->setWordWrap(true);
		row->addWidget(updateLabel_, 1);

		updateBtn_ = new QPushButton(tr("Download update"), updateFrame_);
		updateBtn_->setObjectName(QStringLiteral("sltUpdateBannerBtn"));
		updateBtn_->setCursor(Qt::PointingHandCursor);
		updateBtn_->setIcon(st->standardIcon(QStyle::SP_DialogSaveButton));
		updateBtn_->setToolTip(tr("Open the Smart Lower Thirds download page"));
		row->addWidget(updateBtn_);

		connect(updateBtn_, &QPushButton::clicked, this, [this]() {
			QDesktopServices::openUrl(
				QUrl(QStringLiteral("https://obscountdown.com/r/smart-lower-thirds")));
		});

		rootLayout->addWidget(updateFrame_);
	}

	// -------------------------
	// Top row: Resources (output dir)
	// -------------------------
	{
		auto *grid = new QGridLayout();
		grid->setContentsMargins(0, 0, 0, 0);
		grid->setHorizontalSpacing(6);
		grid->setVerticalSpacing(0);
		// Keep the label column tight; let the controls column consume the remaining width.
		grid->setColumnStretch(0, 0);
		grid->setColumnStretch(1, 1);

		auto *lbl = new QLabel(tr("Resources:"), this);
		lbl->setObjectName(QStringLiteral("sltRowLabel"));
		lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		lbl->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

		auto *right = new QWidget(this);
		auto *rightRow = new QHBoxLayout(right);
		rightRow->setContentsMargins(0, 0, 0, 0);
		rightRow->setSpacing(6);

		outputPathEdit = new QLineEdit(right);
		outputPathEdit->setReadOnly(true);
		outputPathEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

		outputBrowseBtn = new QPushButton(right);
		outputBrowseBtn->setCursor(Qt::PointingHandCursor);
		outputBrowseBtn->setToolTip(tr("Select output folder"));
		outputBrowseBtn->setFlat(true);
		outputBrowseBtn->setIcon(st->standardIcon(QStyle::SP_DirOpenIcon));

		rightRow->addWidget(outputPathEdit, 1);
		rightRow->addWidget(outputBrowseBtn);

		grid->addWidget(lbl, 0, 0);
		grid->addWidget(right, 0, 1);

		rootLayout->addLayout(grid);

		connect(outputBrowseBtn, &QPushButton::clicked, this, &LowerThirdDock::onBrowseOutputFolder);
	}

	// -------------------------
	// Browser Source selector row (combo-only workflow)
	// -------------------------
	{
		auto *grid = new QGridLayout();
		grid->setContentsMargins(0, 0, 0, 0);
		grid->setHorizontalSpacing(6);
		grid->setVerticalSpacing(0);
		// Keep the label column tight; let the controls column consume the remaining width.
		grid->setColumnStretch(0, 0);
		grid->setColumnStretch(1, 1);

		auto *lbl = new QLabel(tr("Browser Source:"), this);
		lbl->setObjectName(QStringLiteral("sltRowLabel"));
		lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		lbl->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

		auto *right = new QWidget(this);
		auto *rightRow = new QHBoxLayout(right);
		rightRow->setContentsMargins(0, 0, 0, 0);
		rightRow->setSpacing(6);

		browserSourceCombo = new QComboBox(right);
		browserSourceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		browserSourceCombo->setToolTip(
			tr("Select an existing OBS Browser Source that will display and control Smart Lower Thirds"));

		refreshSourcesBtn = new QPushButton(right);
		refreshSourcesBtn->setCursor(Qt::PointingHandCursor);
		refreshSourcesBtn->setToolTip(tr("Refresh list"));
		refreshSourcesBtn->setFlat(true);

		QIcon refresh = QIcon::fromTheme(QStringLiteral("view-refresh"));
		if (refresh.isNull())
			refresh = st->standardIcon(QStyle::SP_BrowserReload);
		refreshSourcesBtn->setIcon(refresh);

		rightRow->addWidget(browserSourceCombo, 1);
		rightRow->addWidget(refreshSourcesBtn);

		grid->addWidget(lbl, 0, 0);
		grid->addWidget(right, 0, 1);

		rootLayout->addLayout(grid);

		connect(refreshSourcesBtn, &QPushButton::clicked, this, [this]() { populateBrowserSources(true); });
		connect(browserSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			&LowerThirdDock::onBrowserSourceChanged);
	}

	// -------------------------
	// Browser Source size row + exclusive mode
	// -------------------------
	{
		auto *grid = new QGridLayout();
		grid->setContentsMargins(0, 0, 0, 0);
		grid->setHorizontalSpacing(6);
		grid->setVerticalSpacing(0);
		// Keep the label column tight; let the controls column consume the remaining width.
		grid->setColumnStretch(0, 0);
		grid->setColumnStretch(1, 1);

		auto *lbl = new QLabel(tr("Browser Size:"), this);
		lbl->setObjectName(QStringLiteral("sltRowLabel"));
		lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		lbl->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

		auto *right = new QWidget(this);
		auto *rightRow = new QHBoxLayout(right);
		rightRow->setContentsMargins(0, 0, 0, 0);
		rightRow->setSpacing(6);

		browserWidthSpin = new QSpinBox(right);
		browserWidthSpin->setRange(1, 16384);
		browserWidthSpin->setToolTip(tr("Width of the selected Browser Source"));
		browserWidthSpin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		browserWidthSpin->setMinimumWidth(90);

		browserHeightSpin = new QSpinBox(right);
		browserHeightSpin->setRange(1, 16384);
		browserHeightSpin->setToolTip(tr("Height of the selected Browser Source"));
		browserHeightSpin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		browserHeightSpin->setMinimumWidth(90);

		auto *xLbl = new QLabel(tr("x"), right);
		xLbl->setAlignment(Qt::AlignCenter);

		rightRow->addWidget(browserWidthSpin, 1);
		rightRow->addWidget(xLbl);
		rightRow->addWidget(browserHeightSpin, 1);

		grid->addWidget(lbl, 0, 0);
		grid->addWidget(right, 0, 1);

		rootLayout->addLayout(grid);

		connect(browserWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
			&LowerThirdDock::onBrowserSizeChanged);
		connect(browserHeightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
			&LowerThirdDock::onBrowserSizeChanged);
	}

	// -------------------------
	// List
	// -------------------------
	scrollArea = new QScrollArea(this);
	scrollArea->setObjectName(QStringLiteral("LowerThirdContent"));
	scrollArea->setWidgetResizable(true);

	listContainer = new QWidget(scrollArea);
	listLayout = new QVBoxLayout(listContainer);
	listLayout->setContentsMargins(8, 8, 8, 8);
	listLayout->setSpacing(6);
	listLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

	scrollArea->setWidget(listContainer);
	rootLayout->addWidget(scrollArea, 1);

	// -------------------------
	// Add button
	// -------------------------
	{
		auto *row = new QHBoxLayout();
		row->setSpacing(6);
		row->addStretch(1);

		infoBtn = new QPushButton(this);
		infoBtn->setCursor(Qt::PointingHandCursor);
		infoBtn->setToolTip(tr("Troubleshooting / Help"));
		infoBtn->setFlat(true);

		QIcon infoIco = QIcon::fromTheme(QStringLiteral("help-about"));
		if (infoIco.isNull())
			infoIco = st->standardIcon(QStyle::SP_MessageBoxInformation);
		infoBtn->setIcon(infoIco);

		addBtn = new QPushButton(this);
		addBtn->setCursor(Qt::PointingHandCursor);
		addBtn->setToolTip(tr("Add new lower third"));
		addBtn->setFlat(true);

manageGroupsBtn_ = new QPushButton(this);
manageGroupsBtn_->setCursor(Qt::PointingHandCursor);
manageGroupsBtn_->setToolTip(tr("Manage groups"));
manageGroupsBtn_->setFlat(true);

QIcon carIco = QIcon::fromTheme(QStringLiteral("view-media-playlist"));
if (carIco.isNull())
	carIco = QIcon::fromTheme(QStringLiteral("media-playlist-shuffle"));
if (carIco.isNull())
	carIco = st->standardIcon(QStyle::SP_FileDialogDetailedView);
manageGroupsBtn_->setIcon(carIco);


		QIcon plus = QIcon::fromTheme(QStringLiteral("list-add"));
		if (plus.isNull())
			plus = st->standardIcon(QStyle::SP_DialogYesButton);
		addBtn->setIcon(plus);

		row->addWidget(infoBtn);
		row->addWidget(addBtn);
		row->addWidget(manageGroupsBtn_);
		rootLayout->addLayout(row);

		connect(addBtn, &QPushButton::clicked, this, &LowerThirdDock::onAddLowerThird);
		connect(manageGroupsBtn_, &QPushButton::clicked, this, &LowerThirdDock::onManageGroups);

		connect(infoBtn, &QPushButton::clicked, this, [this]() {
			show_troubleshooting_dialog(this);
		});
	}

	// Footer
	rootLayout->addWidget(create_widget_carousel(this));

	const bool hasDir = smart_lt::has_output_dir();
	addBtn->setEnabled(hasDir);
	if (browserSourceCombo)
		browserSourceCombo->setEnabled(true);
	if (refreshSourcesBtn)
		refreshSourcesBtn->setEnabled(true);
}

bool LowerThirdDock::init()
{
	if (smart_lt::has_output_dir())
		outputPathEdit->setText(QString::fromStdString(smart_lt::output_dir()));
	else
		outputPathEdit->clear();

	const bool hasDir = smart_lt::has_output_dir();
	addBtn->setEnabled(hasDir);

	if (hasDir)
		smart_lt::ensure_output_artifacts_exist();

	// Populate browser sources from OBS + restore selection from core config
	populateBrowserSources(true);

	// Restore browser size + exclusive mode from persisted core config
	if (browserWidthSpin) {
		browserWidthSpin->blockSignals(true);
		browserWidthSpin->setValue(smart_lt::target_browser_width());
		browserWidthSpin->blockSignals(false);
	}
	if (browserHeightSpin) {
		browserHeightSpin->blockSignals(true);
		browserHeightSpin->setValue(smart_lt::target_browser_height());
		browserHeightSpin->blockSignals(false);
	}

	rebuildList();

	// Subscribe to core events so dock stays in sync with WS + external edits
	if (!coreListenerToken_) {
		coreListenerToken_ = smart_lt::add_event_listener(&LowerThirdDock::coreEventThunk, this);
	}

	ensureRepeatTimerStarted();
	connectObsSignals();
	return true;
}

// -------------------------
// Browser Source selector helpers
// -------------------------
void LowerThirdDock::populateBrowserSources(bool keepSelection)
{
	if (!browserSourceCombo)
		return;

	populatingSources_ = true;
	browserSourceCombo->blockSignals(true);

	const QString previous = keepSelection ? browserSourceCombo->currentData().toString() : QString();
	const QString saved = QString::fromStdString(smart_lt::target_browser_source_name());

	browserSourceCombo->clear();

	// Placeholder / None
	browserSourceCombo->addItem(tr("— Select a Browser Source —"), QVariant(QString()));

	const auto names = smart_lt::list_browser_source_names();
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

	// Persist selection (empty = none)
	smart_lt::set_target_browser_source_name(name.toStdString());

	// If user selected something real and we have an output dir,
	// rebuild/swap once so it immediately points to the latest generated HTML.
	if (!name.isEmpty() && smart_lt::has_output_dir()) {
		smart_lt::rebuild_and_swap();
	}

	emit requestSave();
}

void LowerThirdDock::onBrowserSizeChanged()
{
	if (!browserWidthSpin || !browserHeightSpin)
		return;

	const int w = browserWidthSpin->value();
	const int h = browserHeightSpin->value();

	// Persist + apply to selected source if exists
	smart_lt::set_target_browser_dimensions(w, h);

	emit requestSave();
}


// -------------------------
// Repeat timer
// -------------------------
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
	if (!smart_lt::has_output_dir())
		return;

	const qint64 now = QDateTime::currentMSecsSinceEpoch();

	const auto &items = smart_lt::all();
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
	}

	for (const auto &c : items) {
		const QString qid = QString::fromStdString(c.id);

		// If this lower third belongs to any currently-running group, do not let the per-item
		// repeat/auto-hide scheduler interfere. Group run timing (visible_ms / interval_ms)
		// must be authoritative, otherwise items may be hidden early (e.g. 3s default).
		{
			const auto owners = smart_lt::groups_containing(c.id);
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
				continue;
			}
		}

		const int every = c.repeat_every_sec;
		int visibleSec = c.repeat_visible_sec;

		// Mode matrix:
		//  - every==0 && visible==0   => full manual (no scheduling)
		//  - every==0 && visible>0    => manual show + auto-hide after visibleSec
		//  - every>0                 => full automated (auto-show + auto-hide). If visible==0, use default.
		if (every <= 0) {
			nextOnMs_.remove(qid);

			if (visibleSec <= 0) {
				offAtMs_.remove(qid);
				continue;
			}

			if (smart_lt::is_visible(c.id)) {
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
			if (smart_lt::is_visible(c.id)) {
				smart_lt::set_visible_persist(c.id, false);
				emit requestSave();
			}
		}

		// Auto-show
		if (every > 0 && nextOnMs_.contains(qid) && now >= nextOnMs_[qid]) {
			qint64 next = nextOnMs_[qid];
			const qint64 step = (qint64)every * 1000;
			while (next <= now)
				next += step;
			nextOnMs_[qid] = next;

			if (!smart_lt::is_visible(c.id)) {
				smart_lt::set_visible_persist(c.id, true);
				offAtMs_[qid] = now + (qint64)visibleSec * 1000;
				emit requestSave();
			}
		}
	}

	updateRowCountdowns();
}

// -------------------------
// Actions
// -------------------------
void LowerThirdDock::onBrowseOutputFolder()
{
	const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Folder"));
	if (dir.isEmpty())
		return;

	smart_lt::set_output_dir_and_load(dir.toStdString());

	nextOnMs_.clear();
	offAtMs_.clear();

	outputPathEdit->setText(dir);

	const bool hasDir = smart_lt::has_output_dir();
	addBtn->setEnabled(hasDir);

	populateBrowserSources(true);

	rebuildList();
	updateRowCountdowns();
	updateRowActiveStyles();
	emit requestSave();
}

void LowerThirdDock::onAddLowerThird()
{
	if (!smart_lt::has_output_dir())
		return;

	smart_lt::add_default_lower_third();

	updateRowCountdowns();
	emit requestSave();
}

void LowerThirdDock::onManageGroups()
{
	if (!smart_lt::has_output_dir()) {
		QMessageBox::information(this, tr("Output folder not set"),
					 tr("Please choose an output folder first."));
		return;
	}

	QDialog dlg(this);
	dlg.setWindowTitle(tr("Lower Third Groups"));
	dlg.setModal(true);
	dlg.resize(860, 520);

	auto *root = new QHBoxLayout(&dlg);

	// Left: group list
	auto *left = new QVBoxLayout();
	auto *carList = new QListWidget(&dlg);
	carList->setSelectionMode(QAbstractItemView::SingleSelection);
	left->addWidget(carList, 1);

	auto *leftBtns = new QHBoxLayout();
	auto *btnAdd = new QPushButton(tr("Add"), &dlg);
	auto *btnDel = new QPushButton(tr("Delete"), &dlg);
	auto *btnStart = new QPushButton(tr("Start"), &dlg);
	auto *btnStop = new QPushButton(tr("Stop"), &dlg);
	leftBtns->addWidget(btnAdd);
	leftBtns->addWidget(btnDel);
	leftBtns->addStretch(1);
	leftBtns->addWidget(btnStart);
	leftBtns->addWidget(btnStop);
	left->addLayout(leftBtns);

	root->addLayout(left, 1);

	// Right: editor
	auto *right = new QVBoxLayout();
	auto *form = new QFormLayout();

	auto *titleEd = new QLineEdit(&dlg);
	auto *visibleSpin = new QSpinBox(&dlg);
		visibleSpin->setRange(1, 600);
		visibleSpin->setSuffix(tr(" s"));

	auto *intervalSpin = new QSpinBox(&dlg);
		intervalSpin->setRange(0, 600);
		intervalSpin->setSuffix(tr(" s"));

	auto *orderCmb = new QComboBox(&dlg);
	orderCmb->addItem(tr("Linear"), 0);
	orderCmb->addItem(tr("Randomized"), 1);

		auto *loopChk = new QCheckBox(tr("Loop group"), &dlg);
		auto *exclusiveChk = new QCheckBox(tr("Exclusive"), &dlg);

	auto *toggleHkEdit = new QKeySequenceEdit(&dlg);
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
		form->addRow(tr("Toggle Hotkey"), r);
	}

	auto *colorEd = new QLineEdit(&dlg);
	colorEd->setPlaceholderText(tr("#2EA043"));

	auto *pickColor = new QPushButton(tr("Pick…"), &dlg);
	auto *colorRow = new QHBoxLayout();
	colorRow->addWidget(colorEd, 1);
	colorRow->addWidget(pickColor);

		form->addRow(tr("Title"), titleEd);
		form->addRow(tr("Order"), orderCmb);
		form->addRow(tr("Visible duration"), visibleSpin);
		form->addRow(tr("Time between items"), intervalSpin);
		form->addRow(tr("Dock highlight color"), colorRow);
		{
			auto *checks = new QHBoxLayout();
			checks->setContentsMargins(0, 0, 0, 0);
			checks->setSpacing(12);
			checks->addWidget(loopChk);
			checks->addWidget(exclusiveChk);
			checks->addStretch(1);
			form->addRow(QString(), checks);
		}

	right->addLayout(form);

	auto *membersLbl = new QLabel(tr("Members (lower thirds)"), &dlg);
	right->addWidget(membersLbl);

	auto *members = new QListWidget(&dlg);
	members->setSelectionMode(QAbstractItemView::NoSelection);
	right->addWidget(members, 1);

	auto *btnApply = new QPushButton(tr("Apply"), &dlg);
	btnApply->setDefault(true);
	right->addWidget(btnApply, 0, Qt::AlignRight);

	root->addLayout(right, 2);

	// Helpers
	auto refreshList = [&]() {
		carList->clear();
		for (const auto &c : smart_lt::groups_const()) {
			auto *it = new QListWidgetItem(QString::fromStdString(c.title.empty() ? c.id : c.title));
			it->setData(Qt::UserRole, QString::fromStdString(c.id));
			carList->addItem(it);
		}
	};

	auto refreshMembers = [&](const std::string &groupId) {
		members->clear();
		QSet<QString> inSet;
		if (auto *car = smart_lt::get_group_by_id(groupId)) {
			for (const auto &m : car->members)
				inSet.insert(QString::fromStdString(m));
		}

		// Enforce UX rule: a lower third can belong to only one group.
		// Do not show lower thirds that are already assigned to a different group.
		const QString qCurCarId = QString::fromStdString(groupId);

		for (const auto &lt : smart_lt::all_const()) {
			const auto owners = smart_lt::groups_containing(lt.id);
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
		if (auto *car = smart_lt::get_group_by_id(qid.toStdString()))
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

		if (auto *car = smart_lt::get_group_by_id(qid.toStdString())) {
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
			toggleHkEdit->setKeySequence(QKeySequence(QString::fromStdString(car->toggle_hotkey).trimmed()));
			refreshMembers(car->id);
		}
		updateStartStopButtons();
	};

	QObject::connect(toggleHkClear, &QPushButton::clicked, &dlg, [&]() { toggleHkEdit->setKeySequence(QKeySequence()); });

	refreshList();
	if (carList->count() > 0)
		carList->setCurrentRow(0);
	loadSelected();

	QObject::connect(carList, &QListWidget::currentItemChanged, &dlg, [&](QListWidgetItem *, QListWidgetItem *) {
		loadSelected();
	});

	QObject::connect(pickColor, &QPushButton::clicked, &dlg, [&]() {
		const QColor cur(colorEd->text().trimmed());
		const QColor picked = QColorDialog::getColor(cur.isValid() ? cur : QColor("#2EA043"), &dlg, tr("Pick dock color"));
		if (picked.isValid())
			colorEd->setText(picked.name(QColor::HexRgb));
	});

	QObject::connect(btnAdd, &QPushButton::clicked, &dlg, [&]() {
		const std::string id = smart_lt::add_default_group();
		refreshList();
		for (int i = 0; i < carList->count(); ++i) {
			auto *it = carList->item(i);
			if (it && it->data(Qt::UserRole).toString().toStdString() == id) {
				carList->setCurrentRow(i);
				break;
			}
		}
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
		smart_lt::remove_group(qid.toStdString());
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

		auto *car = smart_lt::get_group_by_id(qid.toStdString());
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

		auto normalize = [](const QString &s) -> QString {
			return QKeySequence(s).toString(QKeySequence::PortableText).trimmed();
		};

		// Toggle hotkey (PortableText) + uniqueness enforcement
		const QString seq = normalize(toggleHkEdit->keySequence().toString(QKeySequence::PortableText));
		if (!seq.isEmpty()) {
			// If this hotkey is already used by another lower third or group, clear the previous usage.
			for (auto &it : smart_lt::all()) {
				if (it.id == upd.id)
					continue;
				const QString other = normalize(QString::fromStdString(it.hotkey));
				if (!other.isEmpty() && other == seq) {
					it.hotkey.clear();
					smart_lt::notify_list_updated(it.id);
				}
			}
			bool clearedAnyGroup = false;
			for (auto &g : smart_lt::groups()) {
				if (g.id == upd.id)
					continue;
				const QString other = normalize(QString::fromStdString(g.toggle_hotkey));
				if (!other.isEmpty() && other == seq) {
					g.toggle_hotkey.clear();
					clearedAnyGroup = true;
				}
			}
			if (clearedAnyGroup)
				smart_lt::notify_list_updated();
		}
		upd.toggle_hotkey = seq.toStdString();

		smart_lt::update_group(upd);

		std::vector<std::string> mem;
		for (int i = 0; i < members->count(); ++i) {
			auto *mit = members->item(i);
			if (!mit)
				continue;
			if (mit->checkState() == Qt::Checked)
				mem.push_back(mit->data(Qt::UserRole).toString().toStdString());
		}
		smart_lt::set_group_members(qid.toStdString(), mem);

		// Refresh member list: items may become unavailable/available for other groups after this change.
		refreshMembers(qid.toStdString());
		updateStartStopButtons();

		// Update list label and refresh dock styles
		it->setText(QString::fromStdString(upd.title.empty() ? upd.id : upd.title));
		rebuildList();
		rebuildShortcuts();
	});

	dlg.exec();

	// Ensure dock reflects latest persisted group state
	rebuildList();
}


// -------------------------
// List rendering
// -------------------------
void LowerThirdDock::rebuildList()
{
	for (auto &row : rows) {
		if (row.row)
			row.row->deleteLater();
	}
	rows.clear();

	const auto &items = smart_lt::all();
	const QString outDir = QString::fromStdString(smart_lt::output_dir());

	for (const auto &cfg : items) {
		LowerThirdRowUi ui;
		ui.id = QString::fromStdString(cfg.id);

		auto *rowFrame = new QFrame(listContainer);
		rowFrame->setObjectName(QStringLiteral("sltRowFrame"));
		rowFrame->setProperty("sltActive", QVariant(smart_lt::is_visible(cfg.id)));

// Mark group membership (dock-only; does not affect overlay output)
const auto carIds = smart_lt::groups_containing(cfg.id);
if (!carIds.empty()) {
	rowFrame->setProperty("sltInGroup", QVariant(true));

	std::string col = "#2EA043";
	if (auto *car = smart_lt::get_group_by_id(carIds.front())) {
		if (!car->dock_color.empty())
			col = car->dock_color;
	}
	rowFrame->setProperty("sltGroupColor", QString::fromStdString(col));
} else {
	rowFrame->setProperty("sltInGroup", QVariant(false));
	rowFrame->setProperty("sltGroupColor", QString());
}

		rowFrame->setCursor(Qt::PointingHandCursor);
		applyGroupRowStyle(rowFrame);

		auto *h = new QHBoxLayout(rowFrame);
		h->setContentsMargins(8, 4, 8, 4);
		h->setSpacing(6);

		auto *visible = new QCheckBox(rowFrame);
		visible->setChecked(smart_lt::is_visible(cfg.id));
		visible->setFocusPolicy(Qt::NoFocus);
		visible->setAttribute(Qt::WA_TransparentForMouseEvents, true);
		visible->setStyleSheet("QCheckBox::indicator { width: 0px; height: 0px; margin: 0; padding: 0; }");
		h->addWidget(visible);
		ui.visibleCheck = visible;

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
		ui.thumbnailLbl = thumb;

		auto *labelCol = new QWidget(rowFrame);
		auto *labelColLayout = new QVBoxLayout(labelCol);
		labelColLayout->setContentsMargins(0, 0, 0, 0);
		labelColLayout->setSpacing(0);

		const QString displayLabel = QString::fromStdString(cfg.label.empty() ? cfg.title : cfg.label);
		auto *label = new QLabel(displayLabel, labelCol);
		label->setObjectName(QStringLiteral("sltRowLabel"));
		label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
		label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

		auto *sub = new QLabel(labelCol);
		sub->setObjectName(QStringLiteral("sltRowSubLabel"));
		sub->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
		sub->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		sub->setText(QString());

		labelColLayout->addWidget(label);
		labelColLayout->addWidget(sub);

		h->addWidget(labelCol, 1);

		ui.labelLbl = label;
		ui.subLbl = sub;

		auto *upBtn = new QPushButton(rowFrame);
		auto *downBtn = new QPushButton(rowFrame);
		auto *cloneBtn = new QPushButton(rowFrame);
		auto *settingsBtn = new QPushButton(rowFrame);
		auto *removeBtn = new QPushButton(rowFrame);

		auto *st = rowFrame->style();

		upBtn->setIcon(QIcon::fromTheme(QStringLiteral("go-up"), st->standardIcon(QStyle::SP_ArrowUp)));
		upBtn->setToolTip(tr("Move up"));
		upBtn->setFlat(true);

		downBtn->setIcon(QIcon::fromTheme(QStringLiteral("go-down"), st->standardIcon(QStyle::SP_ArrowDown)));
		downBtn->setToolTip(tr("Move down"));
		downBtn->setFlat(true);

		cloneBtn->setIcon(
			QIcon::fromTheme(QStringLiteral("edit-copy"), st->standardIcon(QStyle::SP_DialogOpenButton)));
		cloneBtn->setToolTip(tr("Clone lower third"));
		cloneBtn->setFlat(true);

		settingsBtn->setCursor(Qt::PointingHandCursor);

		QIcon edit = QIcon::fromTheme(QStringLiteral("document-edit"));
		if (edit.isNull())
			edit = QIcon::fromTheme(QStringLiteral("edit-rename"));
		if (edit.isNull())
			edit = style()->standardIcon(QStyle::SP_FileDialogDetailedView);

		settingsBtn->setIcon(edit);
		settingsBtn->setToolTip(tr("Press here to edit the lower third"));
		settingsBtn->setFlat(true);

		removeBtn->setIcon(st->standardIcon(QStyle::SP_DialogCloseButton));
		removeBtn->setCursor(Qt::PointingHandCursor);
		{
			QIcon del = QIcon::fromTheme(QStringLiteral("edit-delete"));
			if (del.isNull())
				del = style()->standardIcon(QStyle::SP_DialogCloseButton);
			removeBtn->setIcon(del);
		}
		removeBtn->setToolTip(tr("Remove lower third"));
		removeBtn->setFlat(true);

		cloneBtn->setIconSize(QSize(16, 16));
		settingsBtn->setIconSize(QSize(16, 16));
		removeBtn->setIconSize(QSize(16, 16));

		upBtn->setIconSize(QSize(16, 16));
		downBtn->setIconSize(QSize(16, 16));

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

		connect(cloneBtn, &QPushButton::clicked, this, [this, id]() { handleClone(id); });
		connect(upBtn, &QPushButton::clicked, this, [this, id]() {
			smart_lt::move_lower_third(id.toStdString(), -1);
			emit requestSave();
		});
		connect(downBtn, &QPushButton::clicked, this, [this, id]() {
			smart_lt::move_lower_third(id.toStdString(), +1);
			emit requestSave();
		});
		connect(settingsBtn, &QPushButton::clicked, this, [this, id]() { handleOpenSettings(id); });
		connect(removeBtn, &QPushButton::clicked, this, [this, id]() { handleRemove(id); });

		rows.push_back(ui);
	}

	rebuildShortcuts();
	updateRowActiveStyles();
	updateRowCountdowns();
}

void LowerThirdDock::updateRowActiveStyles()
{
	for (auto &row : rows) {
		const bool active = smart_lt::is_visible(row.id.toStdString());

		if (row.row) {
			row.row->setProperty("sltActive", QVariant(active));
				applyGroupRowStyle(row.row);
			row.row->style()->unpolish(row.row);
			row.row->style()->polish(row.row);
			row.row->update();
		}

		if (row.visibleCheck) {
			row.visibleCheck->blockSignals(true);
			row.visibleCheck->setChecked(active);
			row.visibleCheck->blockSignals(false);
		}
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

	const auto &items = smart_lt::all();
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
		connect(sc, &QShortcut::activated, this, [this, id]() { handleToggleVisible(id); });
	}

	// Group toggle hotkeys
	for (const auto &g : smart_lt::groups_const()) {
		const QString groupId = QString::fromStdString(g.id);
		const QString seqStr = QString::fromStdString(g.toggle_hotkey).trimmed();
		if (seqStr.isEmpty())
			continue;

		QKeySequence seq(seqStr);
		if (seq.isEmpty())
			continue;

		auto *sc = new QShortcut(seq, this);
		sc->setContext(Qt::ApplicationShortcut);
		shortcuts_.push_back(sc);
		connect(sc, &QShortcut::activated, this, [this, groupId]() {
			auto it = g_groupRuns.find(groupId);
			if (it != g_groupRuns.end() && it->running)
				stopGroupRun(groupId);
			else
				startGroupRun(this, groupId);
			updateRowCountdowns();
			updateRowActiveStyles();
		});
	}
}

void LowerThirdDock::handleToggleVisible(const QString &id)
{
	const std::string sid = id.toStdString();

	// Manual override: if this lower third is part of a running group, stop the group-run.
	// This prevents the scheduler from fighting the user's manual toggle.
	for (const auto &g : smart_lt::groups_const()) {
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
	const bool wasVisible = smart_lt::is_visible(sid);
	const bool ok = smart_lt::toggle_visible_persist(sid);

	if (!ok)
		return;

	const bool nowVisible = smart_lt::is_visible(sid);


	if (!wasVisible && nowVisible) {
		if (auto *cfg = smart_lt::get_by_id(sid)) {
			const int every = cfg->repeat_every_sec;
			int visibleSec = cfg->repeat_visible_sec;

			// Same mode matrix as repeatTick()
			//  - every==0 && visible==0   => full manual (no scheduling)
			//  - every==0 && visible>0    => manual show + auto-hide after visibleSec
			//  - every>0                 => full automated. If visible==0, use default.
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
	if (!smart_lt::has_output_dir())
		return;

	smart_lt::clone_lower_third(id.toStdString());
	emit requestSave();
}

void LowerThirdDock::handleOpenSettings(const QString &id)
{
	auto *dlg = new smart_lt::ui::LowerThirdSettingsDialog(this);
	dlg->setAttribute(Qt::WA_DeleteOnClose, true);
	dlg->setLowerThirdId(id);
	dlg->setWindowModality(Qt::NonModal);
	dlg->setModal(false);
	dlg->show();
	dlg->raise();
	dlg->activateWindow();
	rebuildList();
}

void LowerThirdDock::handleRemove(const QString &id)
{
	if (!smart_lt::has_output_dir())
		return;

const std::string sid = id.toStdString();
const auto cars = smart_lt::groups_containing(sid);

QString msg = tr("Remove this lower third?\n\nThis action cannot be undone.");
if (!cars.empty()) {
	msg += tr("\n\nNote: This lower third is part of %1 group(s) and will also be removed from those groups.")
		       .arg((int)cars.size());
}

const auto res = QMessageBox::question(this, tr("Confirm Removal"), msg,
				      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
if (res != QMessageBox::Yes)
	return;

	smart_lt::remove_lower_third(id.toStdString());

	nextOnMs_.remove(id);
	offAtMs_.remove(id);

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

	const auto *cfg = smart_lt::get_by_id(rowUi.id.toStdString());
	if (!cfg) {
		rowUi.subLbl->clear();
		rowUi.subLbl->setVisible(false);
		return;
	}

	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const QString qid = rowUi.id;

	// Group-run countdown (takes precedence): if this lower third is currently being shown as part of
	// an active group run, show time remaining until the group hides it.
	if (g_groupHideAtMs.contains(qid) && smart_lt::is_visible(cfg->id)) {
		rowUi.subLbl->setVisible(true);
		const qint64 leftHide = g_groupHideAtMs.value(qid) - now;
		rowUi.subLbl->setText(QStringLiteral("Hides in ") + formatCountdownMs(leftHide));
		return;
	}

	const int every = cfg->repeat_every_sec;
	const int keepVisible = cfg->repeat_visible_sec;

	// Mode matrix:
	//  - every==0 && keepVisible==0 => full manual (no countdowns)
	//  - every==0 && keepVisible>0  => manual show + auto-hide countdown
	//  - every>0                   => full automated (next + hide countdowns)
	if (every <= 0) {
		if (keepVisible <= 0) {
			rowUi.subLbl->clear();
			rowUi.subLbl->setVisible(false);
			return;
		}

		const bool isVis = smart_lt::is_visible(cfg->id);
		if (!isVis) {
			rowUi.subLbl->clear();
			rowUi.subLbl->setVisible(false);
			return;
		}

		rowUi.subLbl->setVisible(true);


		if (!offAtMs_.contains(qid))
			offAtMs_[qid] = now + (qint64)keepVisible * 1000;

		const qint64 leftHide = offAtMs_[qid] - now;
		rowUi.subLbl->setText(QStringLiteral("Hides in ") + formatCountdownMs(leftHide));
		return;
	}

	rowUi.subLbl->setVisible(true);

	if (!nextOnMs_.contains(qid)) {
		nextOnMs_[qid] = now + (qint64)every * 1000;
	}

	const bool isVis = smart_lt::is_visible(cfg->id);

	QStringList parts;

	if (isVis && offAtMs_.contains(qid)) {
		const qint64 leftHide = offAtMs_[qid] - now;
		parts << (QStringLiteral("Hides in ") + formatCountdownMs(leftHide));
	} else if (isVis) {
		parts << QStringLiteral("Visible");
	}

	if (nextOnMs_.contains(qid)) {
		const qint64 leftNext = nextOnMs_[qid] - now;
		parts << (QStringLiteral("Next in ") + formatCountdownMs(leftNext));
	} else {
		parts << QStringLiteral("Repeating");
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

	QMetaObject::invokeMethod(self, [self]() { self->populateBrowserSources(true); }, Qt::QueuedConnection);
}


void LowerThirdDock::refreshBrowserSources()
{
	populateBrowserSources(true);
}

void LowerThirdDock::setUpdateAvailable(const QString &remoteVersion, const QString &localVersion)
{
	updateRemote_ = remoteVersion.trimmed();
	updateLocal_ = localVersion.trimmed();

	const bool show = (!updateRemote_.isEmpty() && !updateLocal_.isEmpty() && updateRemote_ != updateLocal_);
	if (!updateFrame_ || !updateLabel_)
		return;

	updateFrame_->setVisible(show);
	if (show) {
		updateLabel_->setText(tr("New version available: %1 (you have %2)").arg(updateRemote_, updateLocal_));
	}
}

} // namespace smart_lt::ui

void LowerThird_create_dock()
{
	if (g_dockWidget)
		return;

	auto *panel = new smart_lt::ui::LowerThirdDock(nullptr);
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
	obs_frontend_remove_dock(g_dockWidget);
#endif

	g_dockWidget = nullptr;
	LOGI("Dock destroyed");
}

smart_lt::ui::LowerThirdDock *LowerThird_get_dock()
{
	return qobject_cast<smart_lt::ui::LowerThirdDock *>(g_dockWidget);
}