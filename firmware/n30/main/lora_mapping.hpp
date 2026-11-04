// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "ptob_gnss_reference.hpp"
namespace n30 {
// Only a wide, otherwise coherent software bracket is recoverable. This does
// not install the rejected anchor or renew the qualified anchor's availability.
// CounterMap still enforces its original preceding-anchor/two-second limits.
inline bool recoverable_map_bracket(const ptob_gnss::CounterMap::Anchor& a,
 const ptob_gnss::CounterMap::Anchor& qualified){
 if(!a.before||!qualified.before||a.after<a.before||a.available<a.after||
    a.after-a.before<=400||a.before<=qualified.after||
    a.before-qualified.after>20000000||a.available<qualified.available||
    a.available-qualified.available>40000000||a.after-qualified.before>40000000)return false;
 const uint32_t dc=a.capture-qualified.capture;
 const uint64_t low=(a.before-qualified.after)*4,high=(a.after-qualified.before)*4;
 return uint64_t(dc)+8>=low&&uint64_t(dc)<=high+8;
}
}
