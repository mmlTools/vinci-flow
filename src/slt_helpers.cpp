#include "slt_helpers.hpp"
#include "log.hpp"

#include <obs-frontend-api.h>
#include <QComboBox>
#include <QObject>
#include <QString>
#include <QSignalBlocker>

namespace smart_lt {

void populate_scene_combo(QComboBox *combo,
                          const std::string &boundSceneName)
{
	if (!combo)
		return;

	QSignalBlocker blocker(combo);

	combo->clear();

	combo->addItem(QObject::tr("None (no scene binding)"), QString());

	obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);

	for (size_t i = 0; i < list.sources.num; ++i) {
		obs_source_t *src = list.sources.array[i];
		if (!src)
			continue;

		const char *name = obs_source_get_name(src);
		if (!name || !*name)
			continue;

		QString qName = QString::fromUtf8(name);

		combo->addItem(qName, qName);
	}

	int indexToSet = 0; 

	if (!boundSceneName.empty()) {
		QString wanted = QString::fromStdString(boundSceneName);

		int idx = combo->findText(wanted, Qt::MatchExactly);
		if (idx >= 0)
			indexToSet = idx;
	}

	combo->setCurrentIndex(indexToSet);

	obs_frontend_source_list_free(&list);
}

}
