#pragma once

#include <vector>
#include <string>

#include "entities.hpp"

#include <QString>

namespace smart_lt {

	std::vector<LowerThirdConfig> &all();

	std::string add_default_lower_third();
	std::string clone_lower_third(const std::string &id);
	void remove_lower_third(const std::string &id);
	LowerThirdConfig *get_by_id(const std::string &id);
	void toggle_active(const std::string &id);
	void set_active_exact(const std::string &id);
	void handle_scene_changed();

	void set_output_dir(const std::string &dir);
	bool has_output_dir();
	const std::string &output_dir();
	const std::string &index_html_path();

	int  get_preferred_port();
	void set_preferred_port(int port);

	bool write_index_html();
	void ensure_browser_source();  
	void refresh_browser_source();

	QString cache_bust_url(const QString &in);

	void init_state_from_disk();
	bool save_state_json();
	bool load_state_json();

}
