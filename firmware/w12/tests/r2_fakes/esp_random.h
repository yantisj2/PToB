#pragma once
#include <cstring>
inline void esp_fill_random(void* p,unsigned n){std::memset(p,0x91,n);}
