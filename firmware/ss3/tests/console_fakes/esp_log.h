#pragma once
#include <cstdarg>
inline void esp_log_set_vprintf(int (*)(const char*,va_list)){}
