#include "widget.hpp"

#include <QAbstractAnimation>
#include <QDesktopServices>
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
#include <QSequentialAnimationGroup>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

// Qt Network
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

// -----------------------------------------------------------------------------
// Endpoints (change these)
// -----------------------------------------------------------------------------
static const QString kBaseUrl = QStringLiteral("http://localhost:8080");
static const QString kAdStyleEndpoint = kBaseUrl + QStringLiteral("/api/plugin/ads/slot");
static const QString kAdClickEndpoint = kBaseUrl + QStringLiteral("/api/plugin/ads/click");
static const QString kAdvertiseContactUrl = kBaseUrl + QStringLiteral("/advertise");
static const QString kContactUrl = kBaseUrl + QStringLiteral("/contact");
static const QString kAdSlotKey = QStringLiteral("plugin-ads");

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static QString jsonStr(const QJsonObject &o, const char *k, const QString &def = {})
{
	const auto v = o.value(QLatin1String(k));
	return v.isString() ? v.toString() : def;
}

static QString jsonStr(const QJsonObject &o, const QString &k, const QString &def = {})
{
	const auto v = o.value(k);
	return v.isString() ? v.toString() : def;
}

static void apply_ad_style(QFrame *card, QLabel *icon, QLabel *title, QLabel *subtitle, QPushButton *cta,
			   QLabel *sponsorLine, const QJsonObject &adStyle)
{
	// adStyle = the "ad" object from API
	const QString gradientA = jsonStr(adStyle, "gradient_a", "#2a2d30");
	const QString gradientB = jsonStr(adStyle, "gradient_b", "#1e2124");
	const QString border = jsonStr(adStyle, "border", "rgba(255,255,255,0.16)");
	const QString text = jsonStr(adStyle, "text", "#ffffff");
	const QString muted = jsonStr(adStyle, "muted", "rgba(255,255,255,0.85)");

	const QString btnBg = jsonStr(adStyle, "button_bg", "rgba(255,255,255,0.10)");
	const QString btnBorder = jsonStr(adStyle, "button_border", "rgba(255,255,255,0.30)");
	const QString btnHover = jsonStr(adStyle, "button_hover", "rgba(255,255,255,0.20)");

	if (icon)
		icon->setText(jsonStr(adStyle, "icon", "📣"));

	if (title)
		title->setText(QStringLiteral("<b>%1</b>").arg(jsonStr(adStyle, "title", "Sponsored spot")));

	if (subtitle) {
		subtitle->setText(jsonStr(adStyle, "subtitle", "Your brand could be here."));
		subtitle->setWordWrap(true);
	}

	if (cta)
		cta->setText(jsonStr(adStyle, "cta", "Learn more"));

	if (sponsorLine) {
		sponsorLine->setText(jsonStr(adStyle, "sponsor", "Sponsored"));
		sponsorLine->setVisible(!sponsorLine->text().trimmed().isEmpty());
	}

	card->setStyleSheet(QString("QFrame#adShowCard {"
				    "  border-radius: 10px;"
				    "  border: 1px solid %1;"
				    "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 %2, stop:1 %3);"
				    "  padding: 6px;"
				    "}"
				    "QLabel#adShowTitle { color: %4; font-size: 12px; font-weight: 700; }"
				    "QLabel#adShowSubtitle { color: %5; font-size: 10px; }"
				    "QLabel#adShowSponsor { color: rgba(255,255,255,0.65); font-size: 9px; }"
				    "QPushButton#adShowCTA {"
				    "  padding: 5px 10px;"
				    "  border-radius: 6px;"
				    "  border: 1px solid %6;"
				    "  background: %7;"
				    "  color: %4;"
				    "  font-size: 10px;"
				    "  font-weight: 700;"
				    "}"
				    "QPushButton#adShowCTA:hover { background: %8; }")
				    .arg(border, gradientA, gradientB, text, muted, btnBorder, btnBg, btnHover));
}

// -----------------------------------------------------------------------------
// Existing cards (unchanged)
// -----------------------------------------------------------------------------
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

	auto *subtitle = new QLabel(QObject::tr("Want unique, animated lower thirds tailored to your stream?\n"
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

	card->setStyleSheet("QFrame#sltShopCard {"
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
			    "}");

	const QUrl shopUrl(QStringLiteral("https://ko-fi.com/mmltech/shop"));
	QObject::connect(shopBtn, &QPushButton::clicked, card, [shopUrl]() { QDesktopServices::openUrl(shopUrl); });

	return card;
}

// -----------------------------------------------------------------------------
// NEW: “Advertise in this plugin” card (static)
// -----------------------------------------------------------------------------
QWidget *widget_create_advertise_card(QWidget *parent)
{
	auto *card = new QFrame(parent);
	card->setObjectName(QStringLiteral("adSellCard"));
	card->setFrameShape(QFrame::NoFrame);
	card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	auto *lay = new QHBoxLayout(card);
	lay->setContentsMargins(12, 10, 12, 10);
	lay->setSpacing(10);

	auto *icon = new QLabel(card);
	icon->setText(QString::fromUtf8("📣"));
	icon->setStyleSheet(QStringLiteral("font-size:30px;"));

	auto *text = new QLabel(card);
	text->setTextFormat(Qt::RichText);
	text->setWordWrap(true);
	text->setText("<b>Advertise in this plugin</b><br>"
		      "Get a sponsored spot inside the UI.<br>"
		      "Contact me to reserve the slot.");

	auto *btn = new QPushButton(QStringLiteral("Contact for pricing"), card);
	btn->setCursor(Qt::PointingHandCursor);
	btn->setMinimumHeight(28);
	btn->setStyleSheet("QPushButton { background: rgba(255,255,255,0.10); color: white; "
			   "border: 1px solid rgba(255,255,255,0.25); border-radius: 6px; "
			   "padding: 6px 10px; font-weight: 700; }"
			   "QPushButton:hover { background: rgba(255,255,255,0.18); }"
			   "QPushButton:pressed { background: rgba(255,255,255,0.25); }");

	const QUrl contactUrl(kContactUrl);
	QObject::connect(btn, &QPushButton::clicked, card, [contactUrl]() { QDesktopServices::openUrl(contactUrl); });

	lay->addWidget(icon, 0, Qt::AlignVCenter);
	lay->addWidget(text, 1);
	lay->addWidget(btn, 0, Qt::AlignVCenter);

	card->setStyleSheet("#adSellCard {"
			    "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
			    "    stop:0 #2a2d30, stop:1 #1e2124);"
			    "  border:1px solid #3a3d40; border-radius:10px; padding:6px; }"
			    "#adSellCard QLabel { color:#ffffff; }");

	return card;
}

// -----------------------------------------------------------------------------
// NEW: “Show advertising” card (fetch style once; click logs + redirects)
// -----------------------------------------------------------------------------
QWidget *widget_create_show_advertise_card(QWidget *parent)
{
	auto *card = new QFrame(parent);
	card->setObjectName(QStringLiteral("adShowCard"));
	card->setFrameShape(QFrame::NoFrame);
	card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	auto *root = new QVBoxLayout(card);
	root->setContentsMargins(10, 10, 10, 10);
	root->setSpacing(6);

	auto *header = new QHBoxLayout();
	header->setContentsMargins(0, 0, 0, 0);
	header->setSpacing(8);

	auto *icon = new QLabel(QString::fromUtf8("⭐"), card);
	icon->setStyleSheet(QStringLiteral("font-size:28px;"));

	auto *title = new QLabel(card);
	title->setObjectName(QStringLiteral("adShowTitle"));
	title->setText(QStringLiteral("<b>Sponsored</b>"));

	auto *sponsorLine = new QLabel(card);
	sponsorLine->setObjectName(QStringLiteral("adShowSponsor"));
	sponsorLine->setText(QStringLiteral("Loading…"));
	sponsorLine->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

	header->addWidget(icon, 0, Qt::AlignVCenter);
	header->addWidget(title, 1, Qt::AlignVCenter);
	header->addWidget(sponsorLine, 0, Qt::AlignVCenter);

	auto *subtitle = new QLabel(card);
	subtitle->setObjectName(QStringLiteral("adShowSubtitle"));
	subtitle->setWordWrap(true);
	subtitle->setText(QStringLiteral("Fetching sponsored content…"));

	auto *btnRow = new QHBoxLayout();
	btnRow->setContentsMargins(0, 0, 0, 0);
	btnRow->setSpacing(8);

	auto *cta = new QPushButton(QStringLiteral("Open"), card);
	cta->setObjectName(QStringLiteral("adShowCTA"));
	cta->setCursor(Qt::PointingHandCursor);
	cta->setMinimumHeight(28);

	btnRow->addWidget(cta, 0);
	btnRow->addStretch(1);

	root->addLayout(header);
	root->addWidget(subtitle);
	root->addLayout(btnRow);

	// Default state
	{
		QJsonObject def;
		def.insert(QStringLiteral("icon"), QStringLiteral("⭐"));
		def.insert(QStringLiteral("title"), QStringLiteral("Sponsored spot"));
		def.insert(QStringLiteral("subtitle"), QStringLiteral("Loading…"));
		def.insert(QStringLiteral("cta"), QStringLiteral("Open"));
		def.insert(QStringLiteral("sponsor"), QStringLiteral("Sponsored"));
		apply_ad_style(card, icon, title, subtitle, cta, sponsorLine, def);
	}

	card->setProperty("ad_click_url", QString());
	card->setProperty("ad_loaded", false);

	// Click: open tracking URL (server logs click + shows secure redirect page)
	QObject::connect(cta, &QPushButton::clicked, card, [card]() {
		const QString urlStr = card->property("ad_click_url").toString();
		if (urlStr.isEmpty()) {
			QDesktopServices::openUrl(QUrl(kAdvertiseContactUrl));
			return;
		}
		QDesktopServices::openUrl(QUrl(urlStr));
	});

	auto startFetch = [card, icon, title, subtitle, cta, sponsorLine]() {
		if (!card)
			return;

		if (card->property("ad_loaded").toBool())
			return;

		card->setProperty("ad_loaded", true);

		auto *nam = new QNetworkAccessManager(card);

		// Build: /api/plugin/ads/slot?key=plugin-ads
		QUrl styleUrl(kAdStyleEndpoint);
		{
			QUrlQuery q(styleUrl);
			q.addQueryItem(QStringLiteral("key"), kAdSlotKey);
			styleUrl.setQuery(q);
		}

		QNetworkRequest req(styleUrl);
		req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("obs-plugin/smart-lower-thirds"));

		QNetworkReply *replyRaw = nam->get(req);
		if (!replyRaw) {
			QJsonObject noad;
			noad.insert(QStringLiteral("icon"), QStringLiteral("🧩"));
			noad.insert(QStringLiteral("title"), QStringLiteral("No sponsor available"));
			noad.insert(QStringLiteral("subtitle"), QStringLiteral("Check back later."));
			noad.insert(QStringLiteral("cta"), QStringLiteral("Advertise here"));
			noad.insert(QStringLiteral("sponsor"), QStringLiteral(""));
			apply_ad_style(card, icon, title, subtitle, cta, sponsorLine, noad);
			card->setProperty("ad_click_url", kAdvertiseContactUrl);
			return;
		}

		QPointer<QNetworkReply> reply(replyRaw);

		QObject::connect(
			replyRaw, &QNetworkReply::finished, card,
			[card, reply, icon, title, subtitle, cta, sponsorLine]() {
				if (!card || !reply)
					return;

				const QByteArray body = reply->readAll();
				const int httpStatus =
					reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
				const bool ok = (reply->error() == QNetworkReply::NoError && httpStatus >= 200 &&
						 httpStatus < 300);

				reply->deleteLater();

				if (!ok) {
					QJsonObject noad;
					noad.insert(QStringLiteral("icon"), QStringLiteral("🧩"));
					noad.insert(QStringLiteral("title"), QStringLiteral("No sponsor available"));
					noad.insert(QStringLiteral("subtitle"), QStringLiteral("Check back later."));
					noad.insert(QStringLiteral("cta"), QStringLiteral("Advertise here"));
					noad.insert(QStringLiteral("sponsor"), QStringLiteral(""));
					apply_ad_style(card, icon, title, subtitle, cta, sponsorLine, noad);
					card->setProperty("ad_click_url", kAdvertiseContactUrl);
					return;
				}

				QJsonParseError jerr{};
				const QJsonDocument doc = QJsonDocument::fromJson(body, &jerr);
				const QJsonObject rootObj = doc.isObject() ? doc.object() : QJsonObject();

				const bool apiOk = rootObj.value(QStringLiteral("ok")).toBool(false);

				const QJsonObject slotObj = rootObj.value(QStringLiteral("slot")).isObject()
								    ? rootObj.value(QStringLiteral("slot")).toObject()
								    : QJsonObject();

				const QJsonObject adObj = rootObj.value(QStringLiteral("ad")).isObject()
								  ? rootObj.value(QStringLiteral("ad")).toObject()
								  : QJsonObject();

				const QString clickUrlFromApi = rootObj.value(QStringLiteral("click_url")).toString();

				if (!apiOk || adObj.isEmpty()) {
					QJsonObject noad;
					noad.insert(QStringLiteral("icon"), QStringLiteral("🧩"));
					noad.insert(QStringLiteral("title"), QStringLiteral("No sponsor available"));
					noad.insert(QStringLiteral("subtitle"), QStringLiteral("Check back later."));
					noad.insert(QStringLiteral("cta"), QStringLiteral("Advertise here"));
					noad.insert(QStringLiteral("sponsor"), QStringLiteral(""));
					apply_ad_style(card, icon, title, subtitle, cta, sponsorLine, noad);
					card->setProperty("ad_click_url", kAdvertiseContactUrl);
					return;
				}

				const bool enabled = adObj.value(QStringLiteral("enabled")).toBool(true);
				if (!enabled) {
					QJsonObject off;
					off.insert(QStringLiteral("icon"), QStringLiteral("🧩"));
					off.insert(QStringLiteral("title"),
						   QStringLiteral("Sponsored content disabled"));
					off.insert(QStringLiteral("subtitle"),
						   QStringLiteral("No ads are being shown right now."));
					off.insert(QStringLiteral("cta"), QStringLiteral("Advertise here"));
					off.insert(QStringLiteral("sponsor"), QStringLiteral(""));
					apply_ad_style(card, icon, title, subtitle, cta, sponsorLine, off);
					card->setProperty("ad_click_url", kAdvertiseContactUrl);
					return;
				}

				// Apply style from "ad"
				apply_ad_style(card, icon, title, subtitle, cta, sponsorLine, adObj);

				// Click URL priority:
				// 1) click_url from API (root)
				// 2) build fallback using slot/ad guids
				// 3) advertise page
				QString finalClickUrl = clickUrlFromApi;

				if (finalClickUrl.isEmpty()) {
					const QString adGuid = adObj.value(QStringLiteral("guid")).toString();
					const QString slotGuid = slotObj.value(QStringLiteral("guid")).toString();

					if (!adGuid.isEmpty()) {
						QUrl u(kAdClickEndpoint);
						QUrlQuery q(u);

						// The backend can accept either slot guid or slot key; we pass both safely.
						q.addQueryItem(QStringLiteral("key"), kAdSlotKey);
						if (!slotGuid.isEmpty())
							q.addQueryItem(QStringLiteral("slot"), slotGuid);
						q.addQueryItem(QStringLiteral("ad"), adGuid);

						u.setQuery(q);
						finalClickUrl = u.toString();
					}
				}

				if (finalClickUrl.isEmpty())
					finalClickUrl = kAdvertiseContactUrl;

				card->setProperty("ad_click_url", finalClickUrl);
			});
	};

	QTimer::singleShot(0, card, startFetch);
	return card;
}

// -----------------------------------------------------------------------------
// Carousel (UPDATED): now includes ad cards
// -----------------------------------------------------------------------------
QWidget *create_widget_carousel(QWidget *parent)
{
	auto *wrapper = new QWidget(parent);
	wrapper->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	auto *root = new QVBoxLayout(wrapper);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(4);

	auto *stack = new QStackedWidget(wrapper);
	stack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	stack->addWidget(widget_create_show_advertise_card(stack));
	stack->addWidget(widget_create_shopping_card(stack));
	stack->addWidget(widget_create_discord_card(stack));
	stack->addWidget(widget_create_kofi_card(stack));
	stack->addWidget(widget_create_advertise_card(stack));

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
