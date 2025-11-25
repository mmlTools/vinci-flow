#include "widget.hpp"

#include <QStackedWidget>
#include <QTimer>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QAbstractAnimation>
#include <QToolButton>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDesktopServices>
#include <QUrl>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTimer>
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

QWidget *widget_create_shopping_card(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("sltShopCard"));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(6);

    auto *headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(6);

    auto *badge = new QLabel(QStringLiteral("PRO"), card);
    badge->setObjectName(QStringLiteral("sltShopBadge"));

    auto *title = new QLabel(QObject::tr("Get custom Lower Third styles"), card);
    title->setObjectName(QStringLiteral("sltShopTitle"));

    headerRow->addWidget(badge);
    headerRow->addWidget(title, 1);
    headerRow->addStretch();

    auto *subtitle = new QLabel(
        QObject::tr("Want unique, animated lower thirds tailored to your stream?\n"
                    "Visit my web shop and order custom styles ready for this plugin."),
        card);
    subtitle->setObjectName(QStringLiteral("sltShopSubtitle"));
    subtitle->setWordWrap(true);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);

    auto *shopBtn = new QPushButton(QObject::tr("Open Web Shop"), card);
	shopBtn->setCursor(Qt::PointingHandCursor);
    shopBtn->setObjectName(QStringLiteral("sltShopButton"));

    auto *supportLbl = new QLabel(QObject::tr("Your support helps future updates ❤️"), card);
    supportLbl->setObjectName(QStringLiteral("sltShopSupport"));
    supportLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    buttonRow->addWidget(shopBtn, 0);
    buttonRow->addStretch();
    buttonRow->addWidget(supportLbl, 0);

    layout->addLayout(headerRow);
    layout->addWidget(subtitle);
    layout->addLayout(buttonRow);

    card->setStyleSheet(
        "QFrame#sltShopCard {"
        "  border-radius: 6px;"
        "  border: 1px solid rgba(255, 255, 255, 40);"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "    stop:0 rgba(45, 50, 65, 255),"
        "    stop:1 rgba(18, 105, 160, 255));"
        "}"
        "QLabel#sltShopBadge {"
        "  padding: 2px 6px;"
        "  border-radius: 4px;"
        "  background: rgba(255, 255, 255, 0.12);"
        "  color: #ffffff;"
        "  font-weight: 700;"
        "  font-size: 9px;"
        "  letter-spacing: 0.12em;"
        "  text-transform: uppercase;"
        "}"
        "QLabel#sltShopTitle {"
        "  color: #ffffff;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QLabel#sltShopSubtitle {"
        "  color: rgba(255, 255, 255, 0.85);"
        "  font-size: 10px;"
        "}"
        "QLabel#sltShopSupport {"
        "  color: rgba(255, 255, 255, 0.7);"
        "  font-size: 9px;"
        "}"
        "QPushButton#sltShopButton {"
        "  padding: 4px 10px;"
        "  border-radius: 4px;"
        "  border: 1px solid rgba(255, 255, 255, 80);"
        "  background: rgba(255, 255, 255, 0.08);"
        "  color: #ffffff;"
        "  font-size: 10px;"
        "  font-weight: 600;"
        "}"
        "QPushButton#sltShopButton:hover {"
        "  background: rgba(255, 255, 255, 0.20);"
        "}"
        "QPushButton#sltShopButton:pressed {"
        "  background: rgba(255, 255, 255, 0.30);"
        "}"
    );

    const QUrl shopUrl(QStringLiteral("https://ko-fi.com/mmltech/shop"));

    QObject::connect(shopBtn, &QPushButton::clicked, card, [shopUrl]() {
        QDesktopServices::openUrl(shopUrl);
    });

    return card;
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

	stack->addWidget(widget_create_shopping_card(stack));
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
			if (!dots[i])
				continue;
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
		auto *dot = dots[i];
		if (!dot)
			continue;

		QObject::connect(dot, &QToolButton::clicked, wrapper, [i, timer, switchToIndex]() {
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
