// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "w12_device_profile.hpp"
#include <cassert>
int main(){
 const uint8_t factory[6]=PTOB_FACTORY_MAC;
 const auto* bound=w12_device::find(factory);
 assert(bound&&bound->gps_inputs==bool(W12_GNSS_VERIFIED_INPUTS));
 assert(w12_device::display_permitted(factory,2));
 assert(w12_device::display_permitted(factory,12)==bound->gps_inputs);
 assert(!w12_device::display_permitted(factory,8));
 uint8_t other[6]={};assert(!w12_device::find(other));
 assert(!w12_device::display_permitted(other,2));
 assert(!w12_device::display_permitted(other,12));
}
