#include "dock.hpp"
#include "config.hpp"
#include "const.hpp"
#include "log.hpp"
#include "state.hpp"
#include "settings.hpp"
#include "server.hpp"
#include "widget.hpp"
#include "slt_helpers.hpp"

#include <obs-frontend-api.h>

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
#include <QSpinBox>
#include <QEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QShortcut>
#include <QMouseEvent>

#include "httplib.h"

using namespace smart_lt;

static QWidget *g_dockWidget = nullptr;

static void styleDot(QFrame *dot, bool ok)
{
	if (!dot)
		return;
	dot->setFixedSize(12, 12);
	dot->setStyleSheet(QString("border-radius:6px;"
				   "background-color:%1;"
				   "border:1px solid rgba(0,0,0,0.4);")
				   .arg(ok ? "#44ff66" : "#ff5555"));
}

static bool slt_health_ok_http(int port, int timeout_ms = 150)
{
	if (port <= 0)
		return false;

	httplib::Client cli("127.0.0.1", port);
	cli.set_keep_alive(false);
	cli.set_connection_timeout(0, timeout_ms * 1000);
	cli.set_read_timeout(0, timeout_ms * 1000);
	cli.set_write_timeout(0, timeout_ms * 1000);

	if (auto res = cli.Get("/__slt/health"))
		return res->status == 200;

	return false;
}

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
	)");

	auto *rootLayout = new QVBoxLayout(this);
	rootLayout->setContentsMargins(8, 8, 8, 8);
	rootLayout->setSpacing(6);

	{
		auto *row = new QHBoxLayout();
		auto *lbl = new QLabel(tr("Resources:"), this);

		outputPathEdit = new QLineEdit(this);
		outputPathEdit->setReadOnly(true);

		outputBrowseBtn = new QPushButton(this);
		outputBrowseBtn->setCursor(Qt::PointingHandCursor);
		outputBrowseBtn->setToolTip(tr("Select output folder"));
		outputBrowseBtn->setFlat(true);

		addBtn = new QPushButton(this);

		auto *st = style();
		outputBrowseBtn->setIcon(st->standardIcon(QStyle::SP_DirOpenIcon));
		outputBrowseBtn->setText(QString());

		addBtn->setIcon(st->standardIcon(QStyle::SP_FileDialogNewFolder));
		addBtn->setCursor(Qt::PointingHandCursor);
		addBtn->setToolTip(tr("Add new lower third"));
		addBtn->setText(QString());

		row->addWidget(lbl);
		row->addWidget(outputPathEdit, 1);
		row->addWidget(outputBrowseBtn);
		row->addStretch();
		row->addWidget(addBtn);

		rootLayout->addLayout(row);

		connect(addBtn, &QPushButton::clicked, this, &LowerThirdDock::onAddLowerThird);
		connect(outputBrowseBtn, &QPushButton::clicked,
		        this, &LowerThirdDock::onBrowseOutputFolder);
	}

	{
		auto *row = new QHBoxLayout();
		auto *lbl = new QLabel(tr("Webserver:"), this);

		serverStatusDot = new QFrame(this);
		styleDot(serverStatusDot, false);

		serverPortSpin = new QSpinBox(this);
		serverPortSpin->setRange(1024, 65535);
		serverPortSpin->setValue(smart_lt::get_preferred_port());

		serverToggleBtn = new QPushButton(this);
		serverToggleBtn->setCursor(Qt::PointingHandCursor);
		serverToggleBtn->setFlat(false);
		serverToggleBtn->setToolTip(tr("Start webserver"));
		serverToggleBtn->setText(QString());

		row->addWidget(lbl);
		row->addWidget(serverStatusDot);
		row->addSpacing(6);
		row->addWidget(new QLabel(tr("Port:"), this));
		row->addWidget(serverPortSpin);
		row->addStretch();
		row->addWidget(serverToggleBtn);

		rootLayout->addLayout(row);

		connect(serverToggleBtn, &QPushButton::clicked, this, &LowerThirdDock::onToggleServer);
	}

	const QString sareaStyle = QStringLiteral("#LowerThirdContent {"
						  "  background-color: rgba(0, 0, 0, 0.25);"
						  "  border: 1px solid rgba(7, 7, 7, 0.1);"
						  "  border-radius: 4px;"
						  "  margin-top: 10px;"
						  "  padding-left: 10px;"
						  "}");

	scrollArea = new QScrollArea(this);
	scrollArea->setObjectName(QStringLiteral("LowerThirdContent"));
	scrollArea->setStyleSheet(sareaStyle);
	scrollArea->setWidgetResizable(true);

	listContainer = new QWidget(scrollArea);
	listLayout = new QVBoxLayout(listContainer);
	listLayout->setContentsMargins(8, 8, 8, 8);
	listLayout->setSpacing(6);
	listLayout->setAlignment(Qt::AlignTop);
	scrollArea->setAlignment(Qt::AlignTop | Qt::AlignLeft);

	scrollArea->setWidget(listContainer);
	rootLayout->addWidget(scrollArea);

	auto *carousel = create_widget_carousel(this);
	rootLayout->addWidget(carousel);

	if (smart_lt::has_output_dir()) {
		outputPathEdit->setText(QString::fromStdString(smart_lt::output_dir()));
	}

	bool hasDir = smart_lt::has_output_dir();
	addBtn->setEnabled(hasDir);
	serverToggleBtn->setEnabled(hasDir);

	updateServerUi();
}

bool LowerThirdDock::init()
{
	if (smart_lt::all().empty()) {
		smart_lt::add_default_lower_third();
	}

	rebuildList();

	updateServerUi();

	if (server_is_running()) {
		const int p = server_port();
		QTimer::singleShot(200, this, [this, p]() {
			if (!this->isVisible())
				return;

			const bool httpOk = slt_health_ok_http(p, 150);
			const bool runningNow = server_is_running();

			if (runningNow && httpOk) {
				updateServerUi();
			} else {
				styleDot(serverStatusDot, runningNow && httpOk);
			}
		});
	}

	return true;
}

void LowerThirdDock::updateFromState()
{
	updateRowActiveStyles();
}

bool LowerThirdDock::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::MouseButtonPress) {
		auto *me = static_cast<QMouseEvent *>(event);
		if (me->button() == Qt::LeftButton) {
			for (auto &r : rows) {
				if (watched == r.row) {
					if (QWidget *rowWidget = r.row) {
						QWidget *child = rowWidget->childAt(me->pos());
						if (child && qobject_cast<QPushButton *>(child)) {
							return QWidget::eventFilter(watched, event);
						}
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

void LowerThirdDock::onBrowseOutputFolder()
{
	QString dir = QFileDialog::getExistingDirectory(this, tr("Select output folder"));
	if (dir.isEmpty())
		return;

	outputPathEdit->setText(dir);
	smart_lt::set_output_dir(dir.toUtf8().constData());

	smart_lt::write_index_html();
	smart_lt::save_state_json();

	addBtn->setEnabled(true);
	serverToggleBtn->setEnabled(true);

	updateServerUi();
}

void LowerThirdDock::onToggleServer()
{
	if (!smart_lt::has_output_dir())
		return;

	if (server_is_running()) {
		server_stop();
		updateServerUi();
		return;
	}

	int preferred = serverPortSpin->value();
	smart_lt::set_preferred_port(preferred);

	int actual = server_start(outputPathEdit->text(), preferred);
	if (!actual) {
		updateServerUi();
		return;
	}

	serverPortSpin->setValue(actual);

	updateServerUi();

	smart_lt::write_index_html();
	smart_lt::save_state_json();
	smart_lt::ensure_browser_source();                     		
	smart_lt::refresh_browser_source();    

	QTimer::singleShot(200, this, [this, actual]() {
		if (!this->isVisible())
			return;

		const bool httpOk = slt_health_ok_http(actual, 150);
		const bool runningNow = server_is_running();

		if (runningNow && httpOk) {
			updateServerUi();
		} else {
			styleDot(serverStatusDot, runningNow && httpOk);
		}
	});
}

void LowerThirdDock::updateServerUi()
{
	const bool running = server_is_running();

	styleDot(serverStatusDot, running);

	if (serverToggleBtn) {
		auto *st = style();
		if (running) {
			serverToggleBtn->setIcon(st->standardIcon(QStyle::SP_MediaStop));
			serverToggleBtn->setToolTip(tr("Stop webserver"));
		} else {
			serverToggleBtn->setIcon(st->standardIcon(QStyle::SP_MediaPlay));
			serverToggleBtn->setToolTip(tr("Start webserver"));
		}
		serverToggleBtn->setText(QString());
	}

	if (serverPortSpin) {
		serverPortSpin->setEnabled(!running);
	}
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

	auto &items = smart_lt::all();
	for (const auto &cfg : items) {
		if (cfg.hotkey.empty())
			continue;

		const QString seqStr = QString::fromStdString(cfg.hotkey);
		if (seqStr.trimmed().isEmpty())
			continue;

		QKeySequence seq(seqStr);
		if (seq.isEmpty())
			continue;

		auto *sc = new QShortcut(seq, this);
		sc->setContext(Qt::ApplicationShortcut);
		shortcuts_.push_back(sc);

		const QString id = QString::fromStdString(cfg.id);

		connect(sc, &QShortcut::activated, this, [this, id]() {
			LOGI("Hotkey triggered for lower third '%s'", id.toUtf8().constData());
			handleToggleVisible(id);
		});
	}
}

void LowerThirdDock::onAddLowerThird()
{
	QString newId = QString::fromStdString(smart_lt::add_default_lower_third());
	LOGI("Added new lower third '%s'", newId.toUtf8().constData());

	rebuildList();
	smart_lt::write_index_html();
	smart_lt::save_state_json();
	emit requestSave();
}

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
		rowFrame->setProperty("sltActive", cfg.visible);
		rowFrame->setCursor(Qt::PointingHandCursor);

		auto *h = new QHBoxLayout(rowFrame);
		h->setContentsMargins(8, 4, 8, 4);
		h->setSpacing(6);

		auto *visible = new QCheckBox(rowFrame);
		visible->setChecked(cfg.visible);
		visible->setFocusPolicy(Qt::NoFocus);
		visible->setAttribute(Qt::WA_TransparentForMouseEvents, true);
		visible->setStyleSheet(
		    "QCheckBox::indicator { width: 0px; height: 0px; margin: 0; padding: 0; }");
		h->addWidget(visible);
		rowUi.visibleCheck = visible;

		QLabel *thumb = new QLabel(rowFrame);
		thumb->setObjectName(QStringLiteral("sltRowThumbnail"));
		thumb->setFixedSize(32, 32);
		thumb->setScaledContents(true);

		bool hasThumb = false;
		if (!cfg.profile_picture.empty() && !outDir.isEmpty()) {
			QString imgPath = QDir(outDir).filePath(
			    QString::fromStdString(cfg.profile_picture));
			QPixmap px(imgPath);
			if (!px.isNull()) {
				thumb->setPixmap(px.scaled(32, 32,
				                           Qt::KeepAspectRatioByExpanding,
				                           Qt::SmoothTransformation));
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

		{
			QIcon cloneIcon = QIcon::fromTheme(
			    QStringLiteral("edit-copy"),
			    st->standardIcon(QStyle::SP_DialogOpenButton));
			cloneBtn->setIcon(cloneIcon);
			cloneBtn->setToolTip(tr("Clone lower third"));
			cloneBtn->setText(QString());
		}

		{
			QIcon settingsIcon = QIcon::fromTheme(
			    QStringLiteral("settings-configure"),
			    QIcon::fromTheme(QStringLiteral("preferences-system"),
			                     st->standardIcon(QStyle::SP_FileDialogInfoView)));
			settingsBtn->setIcon(settingsIcon);
			settingsBtn->setToolTip(tr("Open settings"));
			settingsBtn->setText(QString());
		}

		{
			removeBtn->setIcon(st->standardIcon(QStyle::SP_DialogCloseButton));
			removeBtn->setToolTip(tr("Remove lower third"));
			removeBtn->setText(QString());
		}

		cloneBtn->setIconSize(QSize(16, 16));
		settingsBtn->setIconSize(QSize(16, 16));
		removeBtn->setIconSize(QSize(16, 16));

		cloneBtn->setFlat(true);
		settingsBtn->setFlat(true);
		removeBtn->setFlat(true);

		h->addWidget(cloneBtn);
		h->addWidget(settingsBtn);
		h->addWidget(removeBtn);

		listLayout->addWidget(rowFrame);

		rowUi.row = rowFrame;
		rowUi.cloneBtn = cloneBtn;
		rowUi.settingsBtn = settingsBtn;
		rowUi.removeBtn = removeBtn;

		const QString id = rowUi.id;

		connect(visible, &QCheckBox::toggled, this,
		        [this, id](bool) { handleToggleVisible(id); });

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
			row.row->setProperty("sltActive", isVisible);
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

void LowerThirdDock::handleToggleVisible(const QString &id)
{
	smart_lt::toggle_active(id.toStdString());
	smart_lt::write_index_html();
	smart_lt::save_state_json();
	emit requestSave();

	updateRowActiveStyles();
}

void LowerThirdDock::handleClone(const QString &id)
{
	QString newId = QString::fromStdString(smart_lt::clone_lower_third(id.toStdString()));
	if (newId.isEmpty())
		return;

	rebuildList();
	smart_lt::write_index_html();
	smart_lt::save_state_json();
	emit requestSave();
}

void LowerThirdDock::handleOpenSettings(const QString &id)
{
	LowerThirdSettingsDialog dlg(this);
	dlg.setLowerThirdId(id);
	dlg.exec();

	rebuildList();
	smart_lt::write_index_html();
	smart_lt::save_state_json();
	emit requestSave();
}

void LowerThirdDock::handleRemove(const QString &id)
{
	smart_lt::remove_lower_third(id.toStdString());
	rebuildList();
	smart_lt::write_index_html();
	smart_lt::save_state_json();
	emit requestSave();
}

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
	LOGI("Smart Lower Thirds dock created");
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

	LOGI("Smart Lower Thirds dock destroyed");
}

LowerThirdDock *LowerThird_get_dock()
{
	return qobject_cast<LowerThirdDock *>(g_dockWidget);
}
