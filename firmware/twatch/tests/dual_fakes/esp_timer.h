#pragma once
#include "shadow_platform.hpp"
inline int64_t esp_timer_get_time(){return int64_t(shadow_fake::now);}
