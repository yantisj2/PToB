// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstring>
namespace watch {
inline bool console_shape(unsigned rows,const char* body){
 return rows==10 ||
  (rows==11&&std::strstr(body,"\nN30_LORA_HARDWARE ")&&std::strstr(body,"\nTWATCH_LORA_SHADOW version=1 ")) ||
  (rows==5&&(std::strstr(body,"\nTWATCH_SOURCE version=2 ")||std::strstr(body,"\nTWATCH_IMU version=1 ")));
}
}
