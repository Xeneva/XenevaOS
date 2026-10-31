#pragma once
#include "ConfigHelix.h"

#if defined(ARDUINO) && __has_include ("Arduino.h") && !defined(USE_IDF_LOGGER)
#  include "stdio.h"  // for snprintf
#  include  "Arduino.h" // for Serial; include Serial.h does not work

// Logging Implementation
#if HELIX_LOGGING_ACTIVE 
    extern char log_buffer_helix[HELIX_LOG_SIZE];
    enum class LogLevelHelix {Debug, Info, Warning, Error};
    static LogLevelHelix LOGLEVEL_HELIX = HELIX_LOG_LEVEL;
    // We print the log based on the log level
    #define LOG_HELIX(level,...) { if(level>=LOGLEVEL_HELIX) { int l = snprintf(log_buffer_helix, HELIX_LOG_SIZE, __VA_ARGS__); HELIX_LOGGING_OUT.print("libhelix - "); HELIX_LOGGING_OUT.write(log_buffer_helix,l); HELIX_LOGGING_OUT.println(); } }
    #define LOGD_HELIX(...) LOG_HELIX(LogLevelHelix::Debug,__VA_ARGS__)
    #define LOGI_HELIX(...) LOG_HELIX(LogLevelHelix::Info,__VA_ARGS__)
    #define LOGW_HELIX(...) LOG_HELIX(LogLevelHelix::Warning,__VA_ARGS__)
    #define LOGE_HELIX(...) LOG_HELIX(LogLevelHelix::Error,__VA_ARGS__)
#else
    // Remove all log statments from the code
    #define LOGD_HELIX(...) 
    #define LOGI_HELIX(...) 
    #define LOGW_HELIX(...) 
    #define LOGE_HELIX(...) 
#endif

#endif
