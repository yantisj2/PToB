// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "console_shape.hpp"
#include <cassert>
int main(){
 assert(watch::console_shape(10,"normal"));
 assert(watch::console_shape(11,"\nN30_LORA_HARDWARE enabled=1\nTWATCH_LORA_SHADOW version=1 shadow_only=1\n"));
 assert(!watch::console_shape(11,"normal"));
 assert(!watch::console_shape(11,"\nTWATCH_LORA_SHADOW version=1 shadow_only=1\n"));
 assert(!watch::console_shape(12,"\nN30_LORA_HARDWARE enabled=1\nTWATCH_LORA_SHADOW version=1 shadow_only=1\n"));
 assert(watch::console_shape(5,"\nTWATCH_IMU version=1 ready=1\n"));
 assert(!watch::console_shape(5,"normal"));
}
