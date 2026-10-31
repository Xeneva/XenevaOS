#pragma once
#include "../ConfigHelix.h"
#include "esp_log.h"

// Logging Implementation
#if HELIX_LOGGING_ACTIVE
    #define TAG_HELIX "libhelix"
    // Unlike helix_log_std.h's LOG_HELIX macro, ESP_LOGx() is not filtered
    // by HELIX_LOG_LEVEL - without this check every LOGI_HELIX/LOGD_HELIX
    // call (there's one or more per decoded frame) unconditionally emits at
    // esp-idf's INFO/DEBUG level, which is easily slow enough over UART to
    // starve a real-time audio decode task and cause dropped packets.
    enum class LogLevelHelix {Debug, Info, Warning, Error};
    static LogLevelHelix LOGLEVEL_HELIX = HELIX_LOG_LEVEL;
    #define LOGD_HELIX(...) { if (LogLevelHelix::Debug >= LOGLEVEL_HELIX) ESP_LOGD(TAG_HELIX,__VA_ARGS__); }
    #define LOGI_HELIX(...) { if (LogLevelHelix::Info >= LOGLEVEL_HELIX) ESP_LOGI(TAG_HELIX,__VA_ARGS__); }
    #define LOGW_HELIX(...) { if (LogLevelHelix::Warning >= LOGLEVEL_HELIX) ESP_LOGW(TAG_HELIX,__VA_ARGS__); }
    #define LOGE_HELIX(...) { if (LogLevelHelix::Error >= LOGLEVEL_HELIX) ESP_LOGE(TAG_HELIX,__VA_ARGS__); }
#else
    // Remove all log statments from the code
    #define LOGD_HELIX(...)
    #define LOGI_HELIX(...)
    #define LOGW_HELIX(...)
    #define LOGE_HELIX(...)
#endif

