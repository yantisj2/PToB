// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "lora_view.h"
#include "holdover.h"

// Use the same C projection as the passive owner's delivered clock.
bool w12_lora_project(uint64_t tick,uint64_t anchor,uint64_t ns,int32_t rate,int32_t phase,uint64_t* out){
 return r2_hold_lora_project(tick,anchor,ns,rate,phase,out);
}
