// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "ui.hpp"
#include "g1_parent_selection.hpp"
#include <array>
#include <cassert>
#include <cstdio>
int main(){
 constexpr uint64_t start=1000000;
 n30::Model m;n30::BootSplash splash;
 assert(splash.active(m,start));assert(splash.active(m,start+10000000)); // Failed/unattempted transfer cannot start residence.
 splash.presented(start+10000000);
 assert(splash.active(m,start+11499999));assert(!splash.active(m,start+11500000));
 assert(!splash.active(m,start+11500001));
 m.sampled_us=m.utc_at_us=start;m.until_us=start+250000;m.valid=m.locked=true;m.state=n30::State::Follow;m.source=n30::Source::Ble;
 m.precision.valid=m.precision.locked=true;m.precision.until_us=m.until_us;m.precision.rms_ns=999;
 for(auto source:{n30::Source::Ble,n30::Source::Rc1,n30::Source::Rc2}){m.source=source;n30::BootSplash b;assert(!b.active(m,start));}
 m.source=n30::Source::Ble;
 for(unsigned rms:{1000u,1001u,UINT32_MAX}){m.precision.rms_ns=rms;n30::BootSplash b;assert(b.active(m,start));}
 m.precision.rms_ns=999;
 for(auto source:{n30::Source::None,n30::Source::LoRa,n30::Source::Rc9}){m.source=source;n30::BootSplash b;assert(b.active(m,start));}
 m.source=n30::Source::Ble;
 {n30::BootSplash b;assert(b.active(m,m.until_us));}
 m.until_us=m.precision.until_us=start+500000;
 {n30::BootSplash b;assert(b.active(m,start+n30::SnapshotMaxUs));}
 m.locked=false;{n30::BootSplash b;assert(b.active(m,start));}m.locked=true;
 m.state=n30::State::Hold;{n30::BootSplash b;assert(b.active(m,start));}m.state=n30::State::Follow;
 m.valid=false;{n30::BootSplash b;assert(b.active(m,start));}m.valid=true;
 // Actual renderer canaries and both mounted rotations.
 std::array<uint8_t,n30::FrameBytes+2> image{};image.front()=0xa5;image.back()=0x5a;
 n30::render_splash(image.data()+1);assert(image.front()==0xa5&&image.back()==0x5a);
 unsigned lit=0;for(unsigned i=1;i<=n30::FrameBytes;i++)lit+=__builtin_popcount(image[i]);assert(lit>100);
 uint8_t native[n30::FrameBytes];n30::rotate(native,image.data()+1,true);n30::rotate(native,image.data()+1,false);
 // Same fresh GM is immediately selectable only in N30 builds.
 using v12::G1ParentSelection;
 G1ParentSelection p;p.seen(11,false,-60,start);
#ifdef PTOB_N30
 assert(p.choose(start)==11);assert(p.choose(start+G1ParentSelection::FreshUs)==0);
#else
 assert(p.choose(start)==0);p.seen(11,false,-60,start+G1ParentSelection::StartupCollectUs);assert(p.choose(start+G1ParentSelection::StartupCollectUs)==11);
#endif
 G1ParentSelection cooled;cooled.seen(11,false,-60,start);cooled.cooldown(11,start,1000000);assert(!cooled.choose(start));
 G1ParentSelection relay;relay.seen(22,true,-30,start,true);assert(!relay.choose(start));
 G1ParentSelection pinned;pinned.target(22,true);pinned.seen(11,false,-20,start);assert(!pinned.choose(start));
 G1ParentSelection only;only.diagnostic(true);only.seen(11,false,-20,start);assert(!only.choose(start));
 std::puts("PASS splash exact residence/sub1us/fresh BLE only; immediate N30 GM with original W12 collection and selection fences");
}
