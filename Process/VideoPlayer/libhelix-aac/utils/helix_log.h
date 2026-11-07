#pragma once
#include "ConfigHelix.h"

#if defined(USE_IDF_LOGGER)
#  include "utils/helix_log_idf.h"
#else
#  include "utils/helix_log_std.h"
#endif