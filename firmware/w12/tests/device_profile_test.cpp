// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "w12_device_profile.hpp"
#include <cassert>
int main(){
 const uint8_t factory[6]=PTOB_FACTORY_MAC;
 const auto* bound=w12_device::find(factory);
 assert(bound&&bound->gps_inputs==bool(W12_GNSS_VERIFIED_INPUTS));
 uint8_t other[6]={};assert(!w12_device::find(other));
}
