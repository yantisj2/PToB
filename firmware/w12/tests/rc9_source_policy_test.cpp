// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "rc9_source_policy.hpp"
#include <cassert>
int main(){
 using w12::rc9_source;constexpr uint64_t at=1000000,lease=at+360000000;
 auto d=rc9_source(false,true,0,at);assert(d.select_rc8&&d.admit_rc9);
 d=rc9_source(true,true,lease,at);assert(d.select_rc8&&!d.admit_rc9);
 d=rc9_source(true,false,lease,at+100);assert(d.select_rc8&&!d.admit_rc9);
 d=rc9_source(true,true,lease,lease-1);assert(d.select_rc8&&!d.admit_rc9);
 d=rc9_source(true,true,lease,lease);assert(!d.select_rc8&&!d.admit_rc9);
 d=rc9_source(false,false,0,at);assert(!d.select_rc8&&!d.admit_rc9);
 d=rc9_source(false,false,lease,at);assert(d.select_rc8&&!d.admit_rc9);
 d=rc9_source(false,true,lease,at,false);assert(!d.select_rc8&&!d.admit_rc9);
}
