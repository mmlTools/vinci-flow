// dock.cpp
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
#include <QTimer>
#include <QDateTime>
#include <QComboBox>
#include <QSpinBox>
#include <QMetaObject>
#include <QUrl>
#include <QAbstractButton>
#include <QDesktopServices>
#include <QMessageBox>

static QWidget *g_dockWidget = nullptr;

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
QFrame#sltRowFrame {
  border: 1px solid rgba(255,255,255,40);
  border-radius: 4px;
  padding: 4px 6px;
  background: transparent;
}
QFrame#sltRowFrame:hover { background: rgba(255,255,255,0.04); }
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
	// Top row: Resources (output dir)
	// -------------------------
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

		row->addWidget(lbl);
		row->addWidget(outputPathEdit, 1);
		row->addWidget(outputBrowseBtn);

		rootLayout->addLayout(row);

		connect(outputBrowseBtn, &QPushButton::clicked, this, &LowerThirdDock::onBrowseOutputFolder);
	}

	// -------------------------
	// Browser Source selector row (combo-only workflow)
	// -------------------------
	{
		auto *row = new QHBoxLayout();
		row->setSpacing(6);

		auto *lbl = new QLabel(tr("Browser Source:"), this);

		browserSourceCombo = new QComboBox(this);
		browserSourceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		browserSourceCombo->setToolTip(
			tr("Select an existing OBS Browser Source that will display and control Smart Lower Thirds"));

		refreshSourcesBtn = new QPushButton(this);
		refreshSourcesBtn->setCursor(Qt::PointingHandCursor);
		refreshSourcesBtn->setToolTip(tr("Refresh list"));
		refreshSourcesBtn->setFlat(true);

		QIcon refresh = QIcon::fromTheme(QStringLiteral("view-refresh"));
		if (refresh.isNull())
			refresh = st->standardIcon(QStyle::SP_BrowserReload);
		refreshSourcesBtn->setIcon(refresh);

		row->addWidget(lbl);
		row->addWidget(browserSourceCombo, 1);
		row->addWidget(refreshSourcesBtn);

		rootLayout->addLayout(row);

		connect(refreshSourcesBtn, &QPushButton::clicked, this, [this]() { populateBrowserSources(true); });
		connect(browserSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			&LowerThirdDock::onBrowserSourceChanged);
	}

	// -------------------------
	// Browser Source size row + exclusive mode
	// -------------------------
	{
		auto *row = new QHBoxLayout();
		row->setSpacing(6);

		auto *lbl = new QLabel(tr("Browser Size:"), this);

		browserWidthSpin = new QSpinBox(this);
		browserWidthSpin->setRange(1, 16384);
		browserWidthSpin->setToolTip(tr("Width of the selected Browser Source"));
		browserWidthSpin->setFixedWidth(110);

		browserHeightSpin = new QSpinBox(this);
		browserHeightSpin->setRange(1, 16384);
		browserHeightSpin->setToolTip(tr("Height of the selected Browser Source"));
		browserHeightSpin->setFixedWidth(110);

		exclusiveModeCheck = new QCheckBox(tr("Exclusive"), this);
		exclusiveModeCheck->setToolTip(
			tr("When enabled, activating one lower third automatically hides the others"));

		row->addWidget(lbl);
		row->addWidget(browserWidthSpin);
		row->addWidget(new QLabel(tr("x"), this));
		row->addWidget(browserHeightSpin);
		row->addStretch(1);
		row->addWidget(exclusiveModeCheck);

		rootLayout->addLayout(row);

		connect(browserWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
			&LowerThirdDock::onBrowserSizeChanged);
		connect(browserHeightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
			&LowerThirdDock::onBrowserSizeChanged);
		connect(exclusiveModeCheck, &QCheckBox::toggled, this,
			[this](bool on) { onExclusiveModeChanged(on ? 1 : 0); });
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

		QIcon plus = QIcon::fromTheme(QStringLiteral("list-add"));
		if (plus.isNull())
			plus = st->standardIcon(QStyle::SP_DialogYesButton);
		addBtn->setIcon(plus);

		row->addWidget(infoBtn);
		row->addWidget(addBtn);
		rootLayout->addLayout(row);

		connect(addBtn, &QPushButton::clicked, this, &LowerThirdDock::onAddLowerThird);

		connect(infoBtn, &QPushButton::clicked, this, [this]() {
			QMessageBox box(this);
			box.setIcon(QMessageBox::Information);
			box.setWindowTitle(tr("Smart Lower Thirds • Troubleshooting"));
			box.setText(tr("Resources path must have read and write access from OBS Studio."));
			box.setInformativeText(tr(
				"Tip: It is best to store resources in Documents or another location where your user account has full access.\n\n"
				"Use the buttons below to open the resource page and video guides."));
			QAbstractButton *openPage = box.addButton(tr("Open Resource Page"), QMessageBox::ActionRole);
			QAbstractButton *openV1 = box.addButton(tr("Open Video 1"), QMessageBox::ActionRole);
			QAbstractButton *openV2 = box.addButton(tr("Open Video 2"), QMessageBox::ActionRole);
			box.addButton(QMessageBox::Ok);

			box.exec();

			QAbstractButton *clicked = box.clickedButton();
			if (clicked == openPage) {
				QDesktopServices::openUrl(
					QUrl(QStringLiteral("https://obscountdown.com/r/smart-lower-thirds")));
			} else if (clicked == openV1) {
				QDesktopServices::openUrl(
					QUrl(QStringLiteral("https://studio.youtube.com/video/AunKJCyrSmM/edit")));
			} else if (clicked == openV2) {
				QDesktopServices::openUrl(
					QUrl(QStringLiteral("https://studio.youtube.com/video/79Qh2hg9Z_o/edit")));
			}
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
	if (exclusiveModeCheck) {
		exclusiveModeCheck->blockSignals(true);
		exclusiveModeCheck->setChecked(smart_lt::dock_exclusive_mode());
		exclusiveModeCheck->blockSignals(false);
	}

	rebuildList();

	// Subscribe to core events so dock stays in sync with WS + external edits
	if (!coreListenerToken_) {
		coreListenerToken_ = smart_lt::add_event_listener(&LowerThirdDock::coreEventThunk, this);
	}

	ensureRepeatTimerStarted();
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

void LowerThirdDock::onExclusiveModeChanged(int)
{
	if (!exclusiveModeCheck)
		return;

	smart_lt::set_dock_exclusive_mode(exclusiveModeCheck->isChecked());
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
		rowFrame->setCursor(Qt::PointingHandCursor);

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
}

void LowerThirdDock::handleToggleVisible(const QString &id)
{
	const std::string sid = id.toStdString();
	const bool wasVisible = smart_lt::is_visible(sid);
	const bool ok = smart_lt::toggle_visible_persist(sid);

	if (!ok)
		return;

	const bool nowVisible = smart_lt::is_visible(sid);

	const bool exclusive = smart_lt::dock_exclusive_mode();
	if (exclusive && !wasVisible && nowVisible) {
		const auto visible = smart_lt::visible_ids();
		for (const auto &vid : visible) {
			if (vid == sid)
				continue;
			smart_lt::set_visible_persist(vid, false);
		}
	}

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
	smart_lt::ui::LowerThirdSettingsDialog dlg(this);
	dlg.setLowerThirdId(id);
	dlg.exec();
	rebuildList();
	updateRowCountdowns();
	emit requestSave();
}

void LowerThirdDock::handleRemove(const QString &id)
{
	if (!smart_lt::has_output_dir())
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

		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		const QString qid = rowUi.id;

		if (!offAtMs_.contains(qid))
			offAtMs_[qid] = now + (qint64)keepVisible * 1000;

		const qint64 leftHide = offAtMs_[qid] - now;
		rowUi.subLbl->setText(QStringLiteral("Hides in ") + formatCountdownMs(leftHide));
		return;
	}

	rowUi.subLbl->setVisible(true);

	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const QString qid = rowUi.id;

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

void LowerThirdDock::refreshBrowserSources()
{
	populateBrowserSources(true);
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
