#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#undef CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#include "server.hpp"
#include "config.hpp"
#define LOG_TAG "[" PLUGIN_NAME "][server]"
#include "log.hpp"

#include <thread>
#include <atomic>
#include <memory>
#include <cstring>

#include <QString>
#include <QFileInfo>

#include "httplib.h"

static std::unique_ptr<httplib::Server> g_srv;
static std::thread g_thread;
static std::atomic<int> g_port{0};
static std::string g_doc_root;

static void register_routes(httplib::Server &srv)
{
    srv.set_logger(nullptr);

    srv.set_error_handler([](const httplib::Request &req, httplib::Response &res) {
        LOGW("ERROR %d for %s %s", res.status, req.method.c_str(), req.path.c_str());
    });

    srv.Get("/__slt/health", [](const httplib::Request &, httplib::Response &res) {
        res.set_content("ok", "text/plain; charset=utf-8");
    });

    if (!srv.set_mount_point("/", g_doc_root.c_str())) {
        LOGE("Failed to mount docRoot '%s' at '/'", g_doc_root.c_str());
    }

    srv.set_post_routing_handler([](const httplib::Request &req, httplib::Response &res) {
        const auto &p = req.path;
        auto ends = [&](const char *s) {
            size_t n = strlen(s);
            return p.size() >= n && p.compare(p.size() - n, n, s) == 0;
        };
        if (ends(".js") || ends(".json"))
            res.set_header("Cache-Control", "no-store, must-revalidate");
        else
            res.set_header("Cache-Control", "no-cache");
    });
}

int server_start(const QString &doc_root_q, int preferred_port)
{
    if (g_srv)
        return g_port.load();

    g_doc_root = QFileInfo(doc_root_q).absoluteFilePath().toStdString();
    LOGI("docRoot: %s", g_doc_root.c_str());

    auto srv = std::make_unique<httplib::Server>();
    srv->set_tcp_nodelay(true);
    srv->set_read_timeout(5, 0);
    srv->set_write_timeout(5, 0);

    register_routes(*srv);

    const char *hosts[] = {"127.0.0.1", "0.0.0.0"};
    int port = 0;
    std::string host_bound;

    for (auto host : hosts) {
        for (int p = preferred_port; p < preferred_port + 10; ++p) {
            if (srv->bind_to_port(host, p)) {
                port = p;
                host_bound = host;
                break;
            }
        }
        if (port)
            break;
    }

    if (!port) {
        LOGW("Failed to bind near port %d", preferred_port);
        return 0;
    }

    g_port = port;
    g_srv = std::move(srv);

    g_thread = std::thread([host_bound]() {
        LOGI("START http://%s:%d (docRoot=%s)",
             host_bound.c_str(), g_port.load(), g_doc_root.c_str());
        g_srv->listen_after_bind();
        LOGI("LOOP ended (port %d)", g_port.load());
    });

    return port;
}

void server_stop()
{
    if (!g_srv)
        return;

    LOGI("STOP requested (port %d)", g_port.load());
    g_srv->stop();
    if (g_thread.joinable())
        g_thread.join();
    g_srv.reset();
    g_port = 0;
}

int server_port() { return g_port.load(); }
bool server_is_running() { return g_srv && g_srv->is_running(); }

QString server_doc_root()
{
    return QString::fromStdString(g_doc_root);
}