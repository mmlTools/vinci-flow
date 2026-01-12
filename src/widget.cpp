#include "widget.hpp"

#include <QAbstractAnimation>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QSequentialAnimationGroup>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

QWidget *widget_create_kofi_card(QWidget *parent)
{
	auto *kofiCard = new QFrame(parent);
	kofiCard->setObjectName(QStringLiteral("kofiCard"));
	kofiCard->setFrameShape(QFrame::NoFrame);
	kofiCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	auto *kofiLay = new QHBoxLayout(kofiCard);
	kofiLay->setContentsMargins(12, 10, 12, 10);
	kofiLay->setSpacing(10);

	auto *kofiIcon = new QLabel(kofiCard);
	kofiIcon->setText(QString::fromUtf8("☕"));
	kofiIcon->setStyleSheet(QStringLiteral("font-size:30px;"));

	auto *kofiText = new QLabel(kofiCard);
	kofiText->setTextFormat(Qt::RichText);
	kofiText->setOpenExternalLinks(true);
	kofiText->setTextInteractionFlags(Qt::TextBrowserInteraction);
	kofiText->setWordWrap(true);
	kofiText->setText("<b>Enjoying this plugin?</b><br>"
			  "You can support<br>development on Ko-fi.");

	auto *kofiBtn = new QPushButton(QStringLiteral("☕ Buy me a Ko-fi"), kofiCard);
	kofiBtn->setCursor(Qt::PointingHandCursor);
	kofiBtn->setMinimumHeight(28);
	kofiBtn->setStyleSheet("QPushButton { background: #29abe0; color:white; border:none; "
			       "border-radius:6px; padding:6px 10px; font-weight:600; }"
			       "QPushButton:hover { background: #1e97c6; }"
			       "QPushButton:pressed { background: #1984ac; }");

	QObject::connect(kofiBtn, &QPushButton::clicked, kofiCard,
			 [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://ko-fi.com/mmltech"))); });

	kofiLay->addWidget(kofiIcon, 0, Qt::AlignVCenter);
	kofiLay->addWidget(kofiText, 1);
	kofiLay->addWidget(kofiBtn, 0, Qt::AlignVCenter);

	kofiCard->setStyleSheet("#kofiCard {"
				"  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
				"    stop:0 #2a2d30, stop:1 #1e2124);"
				"  border:1px solid #3a3d40; border-radius:10px; padding:6px; }"
				"#kofiCard QLabel { color:#ffffff; }");

	return kofiCard;
}

QWidget *widget_create_discord_card(QWidget *parent)
{
	auto *discordCard = new QFrame(parent);
	discordCard->setObjectName(QStringLiteral("discordCard"));
	discordCard->setFrameShape(QFrame::NoFrame);
	discordCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	auto *lay = new QHBoxLayout(discordCard);
	lay->setContentsMargins(12, 10, 12, 10);
	lay->setSpacing(10);

	auto *icon = new QLabel(discordCard);
	icon->setText(QString::fromUtf8("💬"));
	icon->setStyleSheet(QStringLiteral("font-size:30px;"));

	auto *text = new QLabel(discordCard);
	text->setTextFormat(Qt::RichText);
	text->setWordWrap(true);
	text->setText("<b>Join the Discord server</b><br>"
		      "Get help, share ideas,<br>"
		      "and chat with other users.");

	auto *btn = new QPushButton(QStringLiteral("💬 Join Discord"), discordCard);
	btn->setCursor(Qt::PointingHandCursor);
	btn->setMinimumHeight(28);
	btn->setStyleSheet("QPushButton { "
			   "  background: #5865F2; "
			   "  color: #FFFFFF; "
			   "  border: none; "
			   "  border-radius: 6px; "
			   "  padding: 6px 12px; "
			   "  font-weight: 600; "
			   "}"
			   "QPushButton:hover { "
			   "  background: #4752C4; "
			   "}"
			   "QPushButton:pressed { "
			   "  background: #3C45A5; "
			   "}");

	QObject::connect(btn, &QPushButton::clicked, discordCard,
			 [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://discord.gg/2yD6B2PTuQ"))); });

	lay->addWidget(icon, 0, Qt::AlignVCenter);
	lay->addWidget(text, 1);
	lay->addWidget(btn, 0, Qt::AlignVCenter);

	discordCard->setStyleSheet("#discordCard {"
				   "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
				   "    stop:0 #23272A, "
				   "    stop:0.40 #3035A5, "
				   "    stop:1 #5865F2);"
				   "  border: 1px solid rgba(88, 101, 242, 0.85);"
				   "  border-radius: 10px; "
				   "  padding: 6px; "
				   "}"
				   "#discordCard QLabel { "
				   "  color: #E3E6FF; "
				   "}");

	return discordCard;
}

QWidget *create_widget_carousel(QWidget *parent)
{
	auto *wrapper = new QWidget(parent);
	wrapper->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	auto *root = new QVBoxLayout(wrapper);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(4);

	auto *stack = new QStackedWidget(wrapper);
	stack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	stack->addWidget(widget_create_discord_card(stack));
	stack->addWidget(widget_create_kofi_card(stack));

	root->addWidget(stack);

	QVector<QToolButton *> dots;
	auto *dotsRow = new QHBoxLayout();
	dotsRow->setContentsMargins(0, 0, 0, 0);
	dotsRow->setSpacing(6);
	dotsRow->addStretch(1);

	const int count = stack->count();
	for (int i = 0; i < count; ++i) {
		auto *dot = new QToolButton(wrapper);
		dot->setText(QStringLiteral("●"));
		dot->setCheckable(true);
		dot->setAutoExclusive(true);
		dot->setCursor(Qt::PointingHandCursor);
		dot->setToolTip(QStringLiteral("Show card %1").arg(i + 1));
		dot->setStyleSheet("QToolButton {"
				   "  border: none;"
				   "  background: transparent;"
				   "  color: #666666;"
				   "  font-size: 13px;"
				   "  padding: 0;"
				   "}"
				   "QToolButton:checked {"
				   "  color: #ffffff;"
				   "}");
		dots << dot;
		dotsRow->addWidget(dot);
	}
	dotsRow->addStretch(1);
	root->addLayout(dotsRow);

	auto *effect = new QGraphicsOpacityEffect(stack);
	effect->setOpacity(1.0);
	stack->setGraphicsEffect(effect);

	auto updateDots = [stack, dots]() {
		const int idx = stack->currentIndex();
		for (int i = 0; i < dots.size(); ++i) {
			if (dots[i])
				dots[i]->setChecked(i == idx);
		}
	};

	updateDots();

	auto *timer = new QTimer(wrapper);
	timer->setInterval(20000);

	auto switchToIndex = [stack, effect, updateDots, wrapper](int targetIndex) {
		if (targetIndex < 0 || targetIndex >= stack->count())
			return;
		if (targetIndex == stack->currentIndex())
			return;

		auto *fadeOut = new QPropertyAnimation(effect, "opacity", wrapper);
		fadeOut->setDuration(180);
		fadeOut->setStartValue(1.0);
		fadeOut->setEndValue(0.0);

		auto *fadeIn = new QPropertyAnimation(effect, "opacity", wrapper);
		fadeIn->setDuration(180);
		fadeIn->setStartValue(0.0);
		fadeIn->setEndValue(1.0);

		auto *group = new QSequentialAnimationGroup(wrapper);
		group->addAnimation(fadeOut);
		group->addAnimation(fadeIn);

		QObject::connect(fadeOut, &QPropertyAnimation::finished, stack, [stack, targetIndex, updateDots]() {
			stack->setCurrentIndex(targetIndex);
			updateDots();
		});

		group->start(QAbstractAnimation::DeleteWhenStopped);
	};

	for (int i = 0; i < dots.size(); ++i) {
		if (!dots[i])
			continue;

		QObject::connect(dots[i], &QToolButton::clicked, wrapper, [i, timer, switchToIndex]() {
			if (timer)
				timer->start();
			switchToIndex(i);
		});
	}

	QObject::connect(timer, &QTimer::timeout, wrapper, [stack, switchToIndex]() {
		if (stack->count() == 0)
			return;
		int next = stack->currentIndex() + 1;
		if (next >= stack->count())
			next = 0;
		switchToIndex(next);
	});

	timer->start();
	return wrapper;
}

static QFrame *make_link_card(QWidget *parent, const QString &emoji, const QString &title,
                              const QString &subtitle, const QString &buttonText, const QUrl &url)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("ocHelpCard"));
    card->setFrameShape(QFrame::NoFrame);

    auto *lay = new QHBoxLayout(card);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(10);

    auto *ico = new QLabel(card);
    ico->setText(emoji);
    ico->setMinimumWidth(34);
    ico->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    ico->setStyleSheet(QStringLiteral("font-size:22px;"));

    auto *textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(2);

    auto *t = new QLabel(card);
    t->setText(QStringLiteral("<b>%1</b>").arg(title));
    t->setTextFormat(Qt::RichText);

    auto *sub = new QLabel(card);
    sub->setText(subtitle);
    sub->setWordWrap(true);
    sub->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.75);"));

    textCol->addWidget(t);
    textCol->addWidget(sub);

    auto *btn = new QPushButton(buttonText, card);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setMinimumHeight(30);
    btn->setObjectName(QStringLiteral("ocHelpCta"));
    QObject::connect(btn, &QPushButton::clicked, card, [url]() { QDesktopServices::openUrl(url); });

    lay->addWidget(ico, 0, Qt::AlignTop);
    lay->addLayout(textCol, 1);
    lay->addWidget(btn, 0, Qt::AlignVCenter);

    return card;
}

void show_troubleshooting_dialog(QWidget *parent)
{
    auto *dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    dlg->setWindowTitle(QObject::tr("Smart Lower Thirds • Help & Links"));
    dlg->setModal(true);
    dlg->resize(720, 560);

    // Root
    auto *root = new QVBoxLayout(dlg);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    // Header
    {
        auto *hdr = new QFrame(dlg);
        hdr->setObjectName(QStringLiteral("ocHelpHeader"));
        hdr->setFrameShape(QFrame::NoFrame);
        auto *hl = new QVBoxLayout(hdr);
        hl->setContentsMargins(14, 12, 14, 12);
        hl->setSpacing(6);

        auto *title = new QLabel(QObject::tr("Troubleshooting & Official Links"), hdr);
        title->setStyleSheet(QStringLiteral("font-size:16px; font-weight:700;"));

        auto *desc = new QLabel(
            QObject::tr("If changes do not apply, confirm your Resources path has read/write access (Documents is recommended). "
                        "Use the links below for guides, downloads, and community support."),
            hdr);
        desc->setWordWrap(true);
        desc->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.78);"));

        hl->addWidget(title);
        hl->addWidget(desc);
        root->addWidget(hdr);
    }

    // Scroll content (so the dialog stays usable on smaller screens)
    auto *scroll = new QScrollArea(dlg);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scroll);
    auto *cl = new QVBoxLayout(content);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(10);

    // Quick links
    {
        auto *secTitle = new QLabel(QObject::tr("Quick Links"), content);
        secTitle->setStyleSheet(QStringLiteral("font-weight:700;"));
        cl->addWidget(secTitle);

        cl->addWidget(make_link_card(
            content, QString::fromUtf8("🌐"), QObject::tr("Visit the Website"),
            QObject::tr("Downloads, updates, documentation, and templates."),
            QObject::tr("Open obscountdown.com"), QUrl(QStringLiteral("https://obscountdown.com/r/smart-lower-thirds"))));

        cl->addWidget(make_link_card(
            content, QString::fromUtf8("💬"), QObject::tr("Join Discord"),
            QObject::tr("Community support, feedback, and announcements."),
            QObject::tr("Join Discord"), QUrl(QStringLiteral("https://discord.gg/2yD6B2PTuQ"))));

        cl->addWidget(make_link_card(
            content, QString::fromUtf8("𝕏"), QObject::tr("Follow on X (Twitter)"),
            QObject::tr("Release notes, feature previews, and tips."),
            QObject::tr("Open X"), QUrl(QStringLiteral("https://x.com/streamcd_net"))));

        cl->addWidget(make_link_card(
            content, QString::fromUtf8("👥"), QObject::tr("Facebook Group"),
            QObject::tr("Promotions, community posts, and livestreaming discussions."),
            QObject::tr("Open Facebook"), QUrl(QStringLiteral("https://www.facebook.com/groups/freestreamerspromotion"))));

        cl->addWidget(make_link_card(
            content, QString::fromUtf8("🐘"), QObject::tr("Mastodon"),
            QObject::tr("Federated updates and community posts."),
            QObject::tr("Open Mastodon"), QUrl(QStringLiteral("https://mastodon.social/@obscountdown"))));

        cl->addWidget(make_link_card(
            content, QString::fromUtf8("📸"), QObject::tr("Instagram"),
            QObject::tr("Design previews, templates, and behind-the-scenes."),
            QObject::tr("Open Instagram"), QUrl(QStringLiteral("https://www.instagram.com/obscountdown/"))));
    }

    // Video guides
    {
        auto *secTitle = new QLabel(QObject::tr("Video Guides"), content);
        secTitle->setStyleSheet(QStringLiteral("font-weight:700;"));
        cl->addWidget(secTitle);

        cl->addWidget(make_link_card(
            content, QString::fromUtf8("▶"), QObject::tr("Guide 1"),
            QObject::tr("Setup, browser source, and basic workflow."),
            QObject::tr("Watch on YouTube"), QUrl(QStringLiteral("https://www.youtube.com/watch?v=AunKJCyrSmM"))));

        cl->addWidget(make_link_card(
            content, QString::fromUtf8("▶"), QObject::tr("Guide 2"),
            QObject::tr("Advanced configuration, templates, and automation."),
            QObject::tr("Watch on YouTube"), QUrl(QStringLiteral("https://www.youtube.com/watch?v=79Qh2hg9Z_o"))));
    }

    cl->addStretch(1);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    // Footer buttons
    {
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
        QObject::connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::close);
        root->addWidget(bb);
    }

    // Styling (self-contained)
    dlg->setStyleSheet(
        "#ocHelpHeader {"
        "  background: rgba(255,255,255,0.06);"
        "  border: 1px solid rgba(255,255,255,0.10);"
        "  border-radius: 12px;"
        "}"
        "QScrollArea { background: transparent; }"
        "QDialog { background: #141416; color: white; }"
        "#ocHelpCard {"
        "  background: rgba(255,255,255,0.05);"
        "  border: 1px solid rgba(255,255,255,0.10);"
        "  border-radius: 12px;"
        "}"
        "#ocHelpCard:hover { border-color: rgba(255,255,255,0.18); }"
        "#ocHelpCta {"
        "  background: rgba(255,255,255,0.10);"
        "  border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 10px;"
        "  padding: 6px 12px;"
        "  font-weight: 700;"
        "}"
        "#ocHelpCta:hover { background: rgba(255,255,255,0.14); }"
        "#ocHelpCta:pressed { background: rgba(255,255,255,0.18); }"
    );

    dlg->show();
}
