// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "rc9_source_policy.hpp"
#include <cassert>
int main(){
 using w12::rc9_source;constexpr uint64_t at=1000000,lease=at+360000000;
 auto d=rc9_source(false,true,true,0,at);assert(d.select_rc8&&d.serve_rc9&&!d.allow_rc7);
 d=rc9_source(true,true,true,lease,at);assert(d.client_hold&&d.select_rc8&&d.serve_rc9&&!d.allow_rc7&&!d.display_rc8&&!d.admit_rc9);
 d=rc9_source(true,false,true,lease,at+100);assert(d.client_hold&&d.select_rc8&&!d.serve_rc9&&!d.allow_rc7&&!d.display_rc8&&!d.admit_rc9);
 d=rc9_source(true,true,true,lease,lease-1);assert(d.select_rc8&&!d.allow_rc7);
 d=rc9_source(true,true,true,lease,lease);assert(!d.client_hold&&!d.select_rc8&&!d.serve_rc9&&d.allow_rc7);
 d=rc9_source(false,false,true,0,at);assert(d.select_rc8&&!d.serve_rc9&&!d.allow_rc7);
 assert(d.display_rc8&&d.admit_rc9);
 d=rc9_source(true,true,true,lease,at,false);assert(!d.client_hold&&!d.select_rc8&&!d.serve_rc9&&!d.allow_rc7);
 // Passive observations cannot synthesize the old wire1 source envelope.
 w12_lora_view l{};l.edge_us=at;l.until_us=at+12000000;l.event_ns=1000000000000;
 assert(!w12::rc9_original_envelope(l));
 l.created_ns=l.event_ns-1000;l.expires_ns=l.event_ns+1000;assert(w12::rc9_original_envelope(l));
 ++l.until_us;assert(!w12::rc9_original_envelope(l));--l.until_us;
 l.expires_ns=l.event_ns;assert(!w12::rc9_original_envelope(l));
}
