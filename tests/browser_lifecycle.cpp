// Exercise the actual generator and browser binding with libobs and a fake
// browser source. No CEF, graphics device, or running OBS application is needed.
#include "../src/core.cpp"
#include <QCoreApplication>
#include <stdexcept>

obs_module_t *obs_current_module(void) { return nullptr; }
char *obs_frontend_get_current_scene_collection(void) { return bstrdup("Regression"); }

static int updates = 0;
static int refreshes = 0;
static void require(bool ok, const char *message)
{
	if (!ok)
		throw std::runtime_error(message);
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if (argc != 2)
		return 2;
	require(obs_startup("en-US", nullptr, nullptr), "OBS startup failed");
	obs_source_info info{};
	info.id = "browser_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.get_name = [](void *) { return "Test browser"; };
	info.create = [](obs_data_t *, obs_source_t *source) -> void * { return source; };
	info.destroy = [](void *) {};
	info.update = [](void *, obs_data_t *) { ++updates; };
	info.get_properties = [](void *) {
		auto *props = obs_properties_create();
		obs_properties_add_button(props, "refreshnocache", "Refresh", [](obs_properties_t *, obs_property_t *, void *) {
			++refreshes;
			return true;
		});
		return props;
	};
	obs_register_source(&info);

	auto *settings = obs_data_create();
	const std::string css = "body { background: transparent; }\n#slt-root { opacity: .7; }\n";
	obs_data_set_string(settings, "css", css.c_str());
	obs_data_set_bool(settings, "shutdown", true);
	obs_data_set_bool(settings, "restart_when_active", true);
	auto *source = obs_source_create("browser_source", "Shared graphics", settings, nullptr);
	obs_data_release(settings);
	require(source != nullptr, "source creation failed");
	vflow::g_target_browser_source = "Shared graphics";
	vflow::g_output_dir = QDir(QString::fromLocal8Bit(argv[1])).absolutePath().toStdString();
	vflow::ensure_dir(vflow::g_output_dir);
	auto item = vflow::default_cfg();
	item.id = "lt_test";
	item.anim_in.clear();
	item.anim_out.clear();
	item.js_template = "root.initializations = (root.initializations || 0) + 1;";
	vflow::g_items = {item};
	// Avoid a CDN dependency in the generated test page.
	vflow::write_text_file(vflow::path_animate_css(), "/* test */");
	vflow::save_state_json();
	const auto htmlPath = vflow::bundle_html_current_path();

	updates = refreshes = 0;
	require(vflow::rebuild_and_swap(), "initial rebuild failed");
	require(updates == 1 && refreshes == 0, "binding must update once without an extra refresh");
	settings = obs_source_get_settings(source);
	require(!obs_data_get_bool(settings, "shutdown"), "hidden sources must stay alive");
	require(!obs_data_get_bool(settings, "restart_when_active"), "activation must not reload");
	require(std::string(obs_data_get_string(settings, "css")).empty(), "OBS CSS injection must be disabled");
	require(css == obs_data_get_string(settings, "vflow_custom_css"), "custom CSS backup lost");
	require(css == vflow::read_text_file(vflow::join_path(vflow::g_output_dir, "lt-browser.css")), "custom CSS lost");
	const auto html = vflow::read_text_file(htmlPath);
	const auto cssLink = html.find("lt-browser.css?v=");
	require(cssLink != std::string::npos && cssLink > html.find("animate.min.css") &&
		cssLink < html.find("</head>"), "custom CSS link missing or precedence changed");

	for (int i = 0; i < 20; ++i)
		require(vflow::rebuild_and_swap(), "repeated rebuild failed");
	require(updates == 1 && refreshes == 20, "unchanged binding must refresh once per rebuild");
	require(css == vflow::read_text_file(vflow::join_path(vflow::g_output_dir, "lt-browser.css")), "rebuild lost CSS");

	obs_data_set_string(settings, "css", "body { opacity: .5; }");
	require(vflow::rebuild_and_swap(), "new custom CSS rebuild failed");
	require(updates == 2 && refreshes == 20, "CSS migration caused multiple reloads");
	require(std::string(obs_data_get_string(settings, "vflow_custom_css")) == "body { opacity: .5; }", "new CSS lost");

	// A blocked output path must leave the source and its CSS untouched.
	const auto blocked = vflow::join_path(vflow::g_output_dir, "blocked");
	vflow::write_text_file(blocked, "not a directory");
	obs_data_set_string(settings, "css", "body { opacity: .9; }");
	require(!vflow::swap_target_browser_source_to_file(blocked + "/lt.html"), "failed writes must abort binding");
	require(std::string(obs_data_get_string(settings, "css")) == "body { opacity: .9; }", "failed write erased CSS");
	require(updates == 2 && refreshes == 20, "failed write touched the browser");
	require(htmlPath == obs_data_get_string(settings, "local_file"), "failed write changed the source path");
	obs_data_set_string(settings, "css", "");

	vflow::g_target_browser_width = 1280;
	require(vflow::rebuild_and_swap(), "resize rebuild failed");
	require(updates == 3 && refreshes == 21, "resize must update dimensions and refresh the document once");
	require(obs_data_get_int(settings, "width") == 1280, "resize lost dimensions");

	// Rebinding another source must not overwrite the original source's backup.
	auto *otherSettings = obs_data_create();
	obs_data_set_string(otherSettings, "css", "body { opacity: .3; }");
	auto *other = obs_source_create("browser_source", "Other graphics", otherSettings, nullptr);
	obs_data_release(otherSettings);
	require(other != nullptr, "second source creation failed");
	vflow::g_target_browser_source = "Other graphics";
	require(vflow::rebuild_and_swap(), "second source rebuild failed");
	require(vflow::read_text_file(vflow::join_path(vflow::g_output_dir, "lt-browser.css")) ==
		"body { opacity: .3; }", "second source CSS lost");
	vflow::g_target_browser_source = "Shared graphics";
	require(vflow::rebuild_and_swap(), "original source rebind failed");
	require(vflow::read_text_file(vflow::join_path(vflow::g_output_dir, "lt-browser.css")) ==
		"body { opacity: .5; }", "original source CSS not restored");
	obs_source_release(other);
	obs_data_release(settings);
	obs_source_release(source);
	obs_shutdown();
	return 0;
}
