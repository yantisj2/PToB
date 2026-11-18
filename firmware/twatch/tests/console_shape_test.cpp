// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "console_shape.hpp"
#include <cassert>
int main(){
 assert(watch::console_shape(4,"\nTWATCH_DUAL_PPS version=1 contract=TWATCH_DUAL_RADIO_PPS_V1\n"));
 assert(watch::console_shape(4,"\nTWATCH_CLOCK_SNAPSHOT version=1 contract=TWATCH_GNSS_FREE_SNAPSHOT_V1\n"));
 assert(!watch::console_shape(4,"normal"));
 assert(watch::console_shape(10,"normal"));
 assert(watch::console_shape(11,"\nN30_LORA_HARDWARE enabled=1\nTWATCH_LORA_SHADOW version=1 shadow_only=1\n"));
 assert(!watch::console_shape(11,"normal"));
 assert(!watch::console_shape(11,"\nTWATCH_LORA_SHADOW version=1 shadow_only=1\n"));
 assert(!watch::console_shape(12,"\nN30_LORA_HARDWARE enabled=1\nTWATCH_LORA_SHADOW version=1 shadow_only=1\n"));
 assert(watch::console_shape(12,"\nN30_LORA_HARDWARE active=1\nTWATCH_LORA_SHADOW version=2 shadow_only=0\nTWATCH_LORA_SLEEP version=1 sleeping=1\n"));
 assert(!watch::console_shape(12,"\nN30_LORA_HARDWARE active=1\nTWATCH_LORA_SHADOW version=2 shadow_only=0\n"));
 assert(!watch::console_shape(11,"\nN30_LORA_HARDWARE active=1\nTWATCH_LORA_SHADOW version=2 shadow_only=0\nTWATCH_LORA_SLEEP version=1 sleeping=1\n"));
 assert(watch::console_shape(5,"\nTWATCH_IMU version=1 ready=1\n"));
 assert(watch::console_shape(5,"\nTWATCH_SHADOW version=1 shadow_requested=1\n"));
 assert(!watch::console_shape(5,"normal"));
}
