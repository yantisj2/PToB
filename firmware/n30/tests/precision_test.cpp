// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "ui.hpp"
#include <cassert>
#include <cstring>
#include <algorithm>
namespace {
using Frame=uint8_t[n30::FrameBytes];
void draw(Frame& f,const n30::Model& m){n30::View v;v.page=n30::PrecisionPage;n30::render(f,m,v,m.sampled_us);}
bool same(const Frame& a,const Frame& b,unsigned from,unsigned until){return !std::memcmp(a+from*8,b+from*8,(until-from)*8);}
}
int main(){
 n30::Model m;m.sampled_us=m.utc_at_us=1000000;m.until_us=121000000;m.utc_us=1700000000000000;
 m.valid=m.locked=true;m.state=n30::State::Follow;m.source=n30::Source::LoRa;
 std::strcpy(m.root,"TC-01");std::strcpy(m.parent,"TC-03");
 auto& l=m.lora;l.sampled_us=m.sampled_us;l.until_us=m.until_us;l.utc_us=m.utc_us;l.state=2;
 l.requested=l.active=l.metadata_known=l.trained=l.model_valid=true;l.link_rms_ns=275;l.link_samples=8;
 l.rate_ppb=-231;l.phase_ns=143;l.age_ms=12000;l.reference_age_ms=166000;l.snapshot_age_ms=234000;l.capture_bound_ns=12000;
 auto& p=m.precision;p.valid=p.locked=p.relative_known=p.lock_age_valid=true;p.until_us=m.until_us;p.scope=1;p.rms_ns=49;p.relative_ppb=123;
 Frame a,b;draw(a,m);
 // Selected LoRa diagnostics do not use the independently healthy BLE model.
 p.rms_ns=12345;p.relative_ppb=-456;p.locked=false;p.counter_hz=1;p.source=n30::Source::Rc2;draw(b,m);assert(same(a,b,0,128));
 l.link_rms_ns=800;draw(b,m);assert(!same(a,b,57,79));assert(same(a,b,82,128));l.link_rms_ns=275;
 l.age_ms+=1000;draw(b,m);assert(!same(a,b,101,106));assert(same(a,b,110,115));l.age_ms-=1000;
 l.reference_age_ms+=1000;draw(b,m);assert(same(a,b,101,106));assert(!same(a,b,110,115));l.reference_age_ms-=1000;
 l.snapshot_age_ms+=1000;draw(b,m);assert(same(a,b,0,128)); // META age is not CLOCK REF age.
 l.rate_ppb=231;draw(b,m);assert(!same(a,b,85,90));l.rate_ppb=-231;
 l.phase_ns=-143;draw(b,m);assert(!same(a,b,93,98));l.phase_ns=143;
 // An old RMS number must disappear on loss of training, map, freshness or expiry.
 l.trained=false;draw(a,m);l.link_rms_ns=1234;draw(b,m);assert(same(a,b,49,80));l.trained=true;
 l.model_valid=false;draw(a,m);l.link_rms_ns=9999;draw(b,m);assert(same(a,b,49,80));l.model_valid=true;
 l.sampled_us-=n30::SnapshotMaxUs;draw(a,m);l.link_rms_ns=4;draw(b,m);assert(same(a,b,49,80));l.sampled_us=m.sampled_us;
 l.until_us=m.sampled_us;draw(a,m);l.link_rms_ns=456;draw(b,m);assert(same(a,b,49,80));l.until_us=m.until_us;
 // BLE retains its independent fitter, rate, counter and lock presentation.
 m.source=n30::Source::Ble;p.counter_hz=80000000;draw(a,m);l.link_rms_ns=10;l.age_ms=500;draw(b,m);assert(same(a,b,0,128));
 p.rms_ns=50;draw(b,m);assert(!same(a,b,57,79));
}
