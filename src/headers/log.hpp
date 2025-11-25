#pragma once
#include <obs-module.h>
#include "config.hpp"

#ifndef LOG_TAG
#define LOG_TAG "[" PLUGIN_NAME "]"
#endif

#define LOGI(fmt, ...) blog(LOG_INFO,    LOG_TAG " " fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) blog(LOG_WARNING, LOG_TAG " " fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) blog(LOG_ERROR,   LOG_TAG " " fmt, ##__VA_ARGS__)

#if !defined(NDEBUG) || defined(ENABLE_LOG_DEBUG)
#define LOGD(fmt, ...) blog(LOG_INFO, LOG_TAG " [D] " fmt, ##__VA_ARGS__)
#else
#define LOGD(...) do {} while (0)
#endif

#define LOGI_T(tag, fmt, ...) blog(LOG_INFO,    "[" tag "] " fmt, ##__VA_ARGS__)
#define LOGW_T(tag, fmt, ...) blog(LOG_WARNING, "[" tag "] " fmt, ##__VA_ARGS__)
#define LOGE_T(tag, fmt, ...) blog(LOG_ERROR,   "[" tag "] " fmt, ##__VA_ARGS__)
