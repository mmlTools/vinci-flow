#include "widget.hpp"

#include "config.hpp"

#include <QApplication>
#include <QColor>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString rgba(const QColor &c, double alpha)
{
	return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha, 0, 'f', 2);
}

// Branded call-to-action card (Discord, Ko-fi). White text on the brand
// gradient, so it looks the same in every OBS theme.
QFrame *make_brand_card(QWidget *parent, const QString &objectName, const QString &emoji, const QString &title,
			const QString &body, const QString &buttonText, const QUrl &url, const QString &gradient,
			const QString &border, const QString &buttonBg, const QString &buttonHover,
			const QString &buttonPressed)
{
	auto *card = new QFrame(parent);
	card->setObjectName(objectName);
	card->setFrameShape(QFrame::NoFrame);
	card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	auto *lay = new QHBoxLayout(card);
	lay->setContentsMargins(14, 12, 14, 12);
	lay->setSpacing(12);

	auto *icon = new QLabel(emoji, card);
	icon->setStyleSheet(QStringLiteral("font-size: 28px; background: transparent;"));

	auto *text = new QLabel(card);
	text->setTextFormat(Qt::RichText);
	text->setWordWrap(true);
	text->setText(QStringLiteral("<b>%1</b><br>%2").arg(title, body));

	auto *btn = new QPushButton(buttonText, card);
	btn->setCursor(Qt::PointingHandCursor);
	btn->setMinimumHeight(30);
	btn->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: #FFFFFF; border: none; "
					  "border-radius: 6px; padding: 6px 14px; font-weight: 600; }"
					  "QPushButton:hover { background: %2; }"
					  "QPushButton:pressed { background: %3; }")
				   .arg(buttonBg, buttonHover, buttonPressed));
	QObject::connect(btn, &QPushButton::clicked, card, [url]() { QDesktopServices::openUrl(url); });

	lay->addWidget(icon, 0, Qt::AlignVCenter);
	lay->addWidget(text, 1);
	lay->addWidget(btn, 0, Qt::AlignVCenter);

	card->setStyleSheet(QStringLiteral("#%1 { background: %2; border: 1px solid %3; border-radius: 10px; }"
					   "#%1 QLabel { color: #FFFFFF; background: transparent; }")
				    .arg(objectName, gradient, border));
	return card;
}

// Link card: emoji, title, one-line description and a button that opens a URL.
QFrame *make_link_card(QWidget *parent, const QString &emoji, const QString &title, const QString &subtitle,
		       const QString &buttonText, const QUrl &url)
{
	auto *card = new QFrame(parent);
	card->setObjectName(QStringLiteral("ocHelpCard"));
	card->setFrameShape(QFrame::NoFrame);

	auto *lay = new QHBoxLayout(card);
	lay->setContentsMargins(12, 10, 12, 10);
	lay->setSpacing(10);

	auto *ico = new QLabel(emoji, card);
	ico->setObjectName(QStringLiteral("ocHelpEmoji"));
	ico->setMinimumWidth(34);
	ico->setAlignment(Qt::AlignCenter);

	auto *textCol = new QVBoxLayout();
	textCol->setContentsMargins(0, 0, 0, 0);
	textCol->setSpacing(2);

	auto *t = new QLabel(QStringLiteral("<b>%1</b>").arg(title), card);
	t->setTextFormat(Qt::RichText);

	auto *sub = new QLabel(subtitle, card);
	sub->setObjectName(QStringLiteral("ocHelpMuted"));
	sub->setWordWrap(true);

	textCol->addWidget(t);
	textCol->addWidget(sub);

	auto *btn = new QPushButton(buttonText, card);
	btn->setObjectName(QStringLiteral("ocHelpCta"));
	btn->setCursor(Qt::PointingHandCursor);
	btn->setMinimumHeight(30);
	QObject::connect(btn, &QPushButton::clicked, card, [url]() { QDesktopServices::openUrl(url); });

	lay->addWidget(ico, 0, Qt::AlignVCenter);
	lay->addLayout(textCol, 1);
	lay->addWidget(btn, 0, Qt::AlignVCenter);

	return card;
}

// Compact tile for the social links grid: emoji + title on top, description,
// full-width button below.
QFrame *make_social_tile(QWidget *parent, const QString &emoji, const QString &title, const QString &subtitle,
			 const QString &buttonText, const QUrl &url)
{
	auto *card = new QFrame(parent);
	card->setObjectName(QStringLiteral("ocHelpCard"));
	card->setFrameShape(QFrame::NoFrame);

	auto *lay = new QVBoxLayout(card);
	lay->setContentsMargins(12, 10, 12, 10);
	lay->setSpacing(6);

	auto *head = new QHBoxLayout();
	head->setContentsMargins(0, 0, 0, 0);
	head->setSpacing(8);

	auto *ico = new QLabel(emoji, card);
	ico->setObjectName(QStringLiteral("ocHelpEmoji"));

	auto *t = new QLabel(QStringLiteral("<b>%1</b>").arg(title), card);
	t->setTextFormat(Qt::RichText);

	head->addWidget(ico);
	head->addWidget(t, 1);

	auto *sub = new QLabel(subtitle, card);
	sub->setObjectName(QStringLiteral("ocHelpMuted"));
	sub->setWordWrap(true);

	auto *btn = new QPushButton(buttonText, card);
	btn->setObjectName(QStringLiteral("ocHelpCta"));
	btn->setCursor(Qt::PointingHandCursor);
	btn->setMinimumHeight(28);
	QObject::connect(btn, &QPushButton::clicked, card, [url]() { QDesktopServices::openUrl(url); });

	lay->addLayout(head);
	lay->addWidget(sub, 1);
	lay->addWidget(btn);

	return card;
}

QLabel *make_section_title(QWidget *parent, const QString &text)
{
	auto *l = new QLabel(text.toUpper(), parent);
	l->setObjectName(QStringLiteral("ocHelpSection"));
	return l;
}

// A scrollable tab page. The returned widget goes into the tab widget; the
// layout receives the page content.
QWidget *make_page(QWidget *parent, QVBoxLayout *&layout)
{
	auto *scroll = new QScrollArea(parent);
	scroll->setObjectName(QStringLiteral("ocHelpScroll"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	auto *content = new QWidget(scroll);
	content->setObjectName(QStringLiteral("ocHelpPage"));
	layout = new QVBoxLayout(content);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(8);

	scroll->setWidget(content);
	return scroll;
}

} // namespace

QWidget *widget_create_kofi_card(QWidget *parent)
{
	return make_brand_card(parent, QStringLiteral("kofiCard"), QString::fromUtf8("☕"),
			       QObject::tr("Enjoying VinciFlow?"),
			       QObject::tr("Support development and keep the updates coming."),
			       QObject::tr("Buy me a Ko-fi"), QUrl(QStringLiteral("https://ko-fi.com/mmltech")),
			       QStringLiteral("qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #1E2124, "
					      "stop:0.45 #176A8C, stop:1 #29ABE0)"),
			       QStringLiteral("rgba(41,171,224,0.85)"), QStringLiteral("#29ABE0"),
			       QStringLiteral("#1E97C6"), QStringLiteral("#1984AC"));
}

QWidget *widget_create_discord_card(QWidget *parent)
{
	return make_brand_card(parent, QStringLiteral("discordCard"), QString::fromUtf8("💬"),
			       QObject::tr("Join the Discord server"),
			       QObject::tr("Get help, share ideas and chat with other users."),
			       QObject::tr("Join Discord"), QUrl(QStringLiteral("https://discord.gg/2yD6B2PTuQ")),
			       QStringLiteral("qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #23272A, "
					      "stop:0.40 #3035A5, stop:1 #5865F2)"),
			       QStringLiteral("rgba(88,101,242,0.85)"), QStringLiteral("#5865F2"),
			       QStringLiteral("#4752C4"), QStringLiteral("#3C45A5"));
}

void show_troubleshooting_dialog(QWidget *parent)
{
	auto *dlg = new QDialog(parent);
	dlg->setAttribute(Qt::WA_DeleteOnClose, true);
	dlg->setWindowTitle(QObject::tr("VinciFlow Help"));
	dlg->setModal(true);
	dlg->resize(660, 620);

	auto *root = new QVBoxLayout(dlg);
	root->setContentsMargins(14, 14, 14, 12);
	root->setSpacing(10);

	// Header: name, version, tagline and the docs shortcut.
	{
		auto *hdr = new QFrame(dlg);
		hdr->setObjectName(QStringLiteral("ocHelpHeader"));
		hdr->setFrameShape(QFrame::NoFrame);

		auto *hl = new QHBoxLayout(hdr);
		hl->setContentsMargins(14, 12, 14, 12);
		hl->setSpacing(12);

		auto *logo = new QLabel(QStringLiteral("VF"), hdr);
		logo->setObjectName(QStringLiteral("ocHelpLogo"));
		logo->setFixedSize(44, 44);
		logo->setAlignment(Qt::AlignCenter);

		auto *textCol = new QVBoxLayout();
		textCol->setContentsMargins(0, 0, 0, 0);
		textCol->setSpacing(2);

		auto *titleRow = new QHBoxLayout();
		titleRow->setContentsMargins(0, 0, 0, 0);
		titleRow->setSpacing(8);

		auto *title = new QLabel(QStringLiteral("VinciFlow"), hdr);
		title->setObjectName(QStringLiteral("ocHelpTitle"));

		auto *version = new QLabel(QStringLiteral("v" PLUGIN_VERSION), hdr);
		version->setObjectName(QStringLiteral("ocHelpVersion"));

		titleRow->addWidget(title);
		titleRow->addWidget(version);
		titleRow->addStretch(1);

		auto *subtitle = new QLabel(QObject::tr("Lower thirds controller for OBS Studio"), hdr);
		subtitle->setObjectName(QStringLiteral("ocHelpMuted"));

		textCol->addLayout(titleRow);
		textCol->addWidget(subtitle);

		auto *docsBtn = new QPushButton(QObject::tr("Open docs"), hdr);
		docsBtn->setObjectName(QStringLiteral("ocHelpCta"));
		docsBtn->setCursor(Qt::PointingHandCursor);
		docsBtn->setMinimumHeight(30);
		QObject::connect(docsBtn, &QPushButton::clicked, dlg, []() {
			QDesktopServices::openUrl(QUrl(QStringLiteral("https://vinciflow.streamrsc.com/")));
		});

		hl->addWidget(logo, 0, Qt::AlignVCenter);
		hl->addLayout(textCol, 1);
		hl->addWidget(docsBtn, 0, Qt::AlignVCenter);
		root->addWidget(hdr);
	}

	auto *tabs = new QTabWidget(dlg);
	tabs->setObjectName(QStringLiteral("ocHelpTabs"));

	// Quick start
	{
		QVBoxLayout *cl = nullptr;
		auto *page = make_page(tabs, cl);

		auto *stepsCard = new QFrame(page);
		stepsCard->setObjectName(QStringLiteral("ocHelpCard"));
		auto *sl = new QVBoxLayout(stepsCard);
		sl->setContentsMargins(14, 10, 14, 10);
		sl->setSpacing(4);

		auto *steps = new QLabel(
			QObject::tr("<ol style='margin-left:-20px'>"
				    "<li><b>Add the overlay</b>: click <i>Add overlay to current scene</i> in the dock "
				    "(or Setup &gt; +). To use it in other scenes, add it there with "
				    "<i>Add Existing Source</i>.</li>"
				    "<li><b>Create a lower third</b>: click <i>New</i>, type your text, pick a "
				    "<i>Style Preset</i> and click <i>Apply</i>.</li>"
				    "<li><b>Go live</b>: click a lower third in the dock to show or hide it. A red "
				    "<b>LIVE</b> badge marks what is on stream.</li>"
				    "<li><b>Hotkeys</b>: set one in the editor or in OBS Settings &gt; Hotkeys. They "
				    "work even while you are in a game.</li>"
				    "</ol>"),
			stepsCard);
		steps->setTextFormat(Qt::RichText);
		steps->setWordWrap(true);
		sl->addWidget(steps);
		cl->addWidget(stepsCard);

		auto *troubleCard = new QFrame(page);
		troubleCard->setObjectName(QStringLiteral("ocHelpCard"));
		auto *tl = new QVBoxLayout(troubleCard);
		tl->setContentsMargins(14, 10, 14, 10);
		tl->setSpacing(4);

		auto *troubleTitle =
			new QLabel(QStringLiteral("<b>%1</b>").arg(QObject::tr("Nothing shows up?")), troubleCard);
		troubleTitle->setTextFormat(Qt::RichText);

		auto *trouble = new QLabel(
			QObject::tr("Open Setup in the dock and check that the overlay source exists and is visible "
				    "in your current scene, and that its size matches your canvas (Match canvas)."),
			troubleCard);
		trouble->setObjectName(QStringLiteral("ocHelpMuted"));
		trouble->setWordWrap(true);

		tl->addWidget(troubleTitle);
		tl->addWidget(trouble);
		cl->addWidget(troubleCard);

		cl->addWidget(make_section_title(page, QObject::tr("Video guides")));
		cl->addWidget(make_link_card(page, QString::fromUtf8("▶"), QObject::tr("Getting started"),
					     QObject::tr("Setup, browser source, and basic workflow."),
					     QObject::tr("Watch on YouTube"),
					     QUrl(QStringLiteral("https://www.youtube.com/watch?v=AunKJCyrSmM"))));
		cl->addWidget(make_link_card(page, QString::fromUtf8("▶"), QObject::tr("Going further"),
					     QObject::tr("Advanced configuration, templates, and automation."),
					     QObject::tr("Watch on YouTube"),
					     QUrl(QStringLiteral("https://www.youtube.com/watch?v=79Qh2hg9Z_o"))));
		cl->addStretch(1);

		tabs->addTab(page, QObject::tr("Quick start"));
	}

	// Community & support (the cards that used to rotate in the dock live here now)
	{
		QVBoxLayout *cl = nullptr;
		auto *page = make_page(tabs, cl);

		cl->addWidget(widget_create_discord_card(page));
		cl->addWidget(widget_create_kofi_card(page));

		cl->addWidget(make_section_title(page, QObject::tr("Follow along")));

		auto *grid = new QGridLayout();
		grid->setContentsMargins(0, 0, 0, 0);
		grid->setHorizontalSpacing(8);
		grid->setVerticalSpacing(8);
		grid->setColumnStretch(0, 1);
		grid->setColumnStretch(1, 1);

		grid->addWidget(make_social_tile(page, QString::fromUtf8("𝕏"), QObject::tr("X (Twitter)"),
						 QObject::tr("Release notes, feature previews, and tips."),
						 QObject::tr("Open X"),
						 QUrl(QStringLiteral("https://x.com/streamcd_net"))),
				0, 0);
		grid->addWidget(make_social_tile(
					page, QString::fromUtf8("👥"), QObject::tr("Facebook Group"),
					QObject::tr("Promotions, community posts, and livestreaming discussions."),
					QObject::tr("Open Facebook"),
					QUrl(QStringLiteral("https://www.facebook.com/groups/freestreamerspromotion"))),
				0, 1);
		grid->addWidget(make_social_tile(page, QString::fromUtf8("🐘"), QObject::tr("Mastodon"),
						 QObject::tr("Federated updates and community posts."),
						 QObject::tr("Open Mastodon"),
						 QUrl(QStringLiteral("https://mastodon.social/@streamrsc"))),
				1, 0);
		grid->addWidget(make_social_tile(page, QString::fromUtf8("📸"), QObject::tr("Instagram"),
						 QObject::tr("Design previews, templates, and behind-the-scenes."),
						 QObject::tr("Open Instagram"),
						 QUrl(QStringLiteral("https://www.instagram.com/streamrsc/"))),
				1, 1);
		cl->addLayout(grid);
		cl->addStretch(1);

		tabs->addTab(page, QObject::tr("Community"));
	}

	// Resources
	{
		QVBoxLayout *cl = nullptr;
		auto *page = make_page(tabs, cl);

		cl->addWidget(make_link_card(page, QString::fromUtf8("📘"), QObject::tr("Documentation"),
					     QObject::tr("Setup guide, template placeholders and the websocket API."),
					     QObject::tr("Open docs"),
					     QUrl(QStringLiteral("https://vinciflow.streamrsc.com/"))));
		cl->addWidget(
			make_link_card(page, QString::fromUtf8("🌐"), QObject::tr("Website"),
				       QObject::tr("Downloads, updates, documentation, and templates."),
				       QObject::tr("Open streamrsc.com"),
				       QUrl(QStringLiteral("https://streamrsc.com/streaming-resource/vinci-flow"))));
		cl->addWidget(make_link_card(page, QString::fromUtf8("🐞"), QObject::tr("Report a problem"),
					     QObject::tr("Found a bug or need a feature? Open an issue on GitHub."),
					     QObject::tr("Open GitHub"),
					     QUrl(QStringLiteral("https://github.com/mmlTools/vinci-flow/issues"))));
		cl->addStretch(1);

		tabs->addTab(page, QObject::tr("Resources"));
	}

	root->addWidget(tabs, 1);

	// Footer
	{
		auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
		QObject::connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::close);
		root->addWidget(bb);
	}

	// Styling derived from the OBS theme palette, so the dialog follows dark
	// and light themes instead of forcing its own colours.
	{
		const QPalette pal = QApplication::palette();
		const QColor text = pal.color(QPalette::Text);
		const QColor hl = pal.color(QPalette::Highlight);

		const QString t05 = rgba(text, 0.05), t10 = rgba(text, 0.10), t12 = rgba(text, 0.12),
			      t16 = rgba(text, 0.16), t20 = rgba(text, 0.20), t22 = rgba(text, 0.22),
			      t60 = rgba(text, 0.60), t70 = rgba(text, 0.70);

		QString css;
		css += QStringLiteral("#ocHelpHeader { background: %1; border: 1px solid %2; border-radius: 10px; }\n")
			       .arg(t05, t10);
		css += QStringLiteral("#ocHelpLogo { background: %1; color: #FFFFFF; border-radius: 10px; "
				      "font-size: 15px; font-weight: 800; }\n")
			       .arg(hl.name());
		css += QStringLiteral("#ocHelpTitle { font-size: 18px; font-weight: 700; background: transparent; }\n");
		css += QStringLiteral("#ocHelpVersion { background: %1; border-radius: 8px; padding: 1px 7px; "
				      "font-size: 11px; font-weight: 600; }\n")
			       .arg(t12);
		css += QStringLiteral("#ocHelpMuted { color: %1; background: transparent; }\n").arg(t70);
		css += QStringLiteral("#ocHelpSection { font-size: 11px; font-weight: 700; color: %1; "
				      "margin-top: 6px; background: transparent; }\n")
			       .arg(t60);
		css += QStringLiteral("#ocHelpEmoji { font-size: 20px; background: transparent; }\n");
		css += QStringLiteral("#ocHelpScroll, #ocHelpPage { background: transparent; }\n");
		css += QStringLiteral("#ocHelpCard { background: %1; border: 1px solid %2; border-radius: 10px; }\n")
			       .arg(t05, t10);
		css += QStringLiteral("#ocHelpCard:hover { border-color: %1; }\n").arg(t20);
		css += QStringLiteral("#ocHelpCard QLabel { background: transparent; }\n");
		css += QStringLiteral("#ocHelpCta { background: %1; border: 1px solid %2; border-radius: 8px; "
				      "padding: 6px 12px; font-weight: 700; }\n")
			       .arg(t10, t12);
		css += QStringLiteral("#ocHelpCta:hover { background: %1; }\n").arg(t16);
		css += QStringLiteral("#ocHelpCta:pressed { background: %1; }\n").arg(t22);
		dlg->setStyleSheet(css);
	}

	dlg->show();
}
