// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ui.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <string>
using namespace corese;
int main(int argc,char** argv){
 if(argc!=2)return 2;const std::filesystem::path dir=argv[1];
 std::array<uint16_t,Pixels> frame{};constexpr uint64_t now=21600000000;
 Model m;m.sampled_us=m.utc_at_us=m.offset_sampled_us=m.rssi_sampled_us=now;
 m.until_us=m.precision_until_us=m.offset_until_us=now+200000;
 m.utc_us=1792986374188267+uint64_t(2*86400+6*3600+10*60)*1000000;
 m.utc_valid=m.precision_valid=m.offset_valid=m.rssi_valid=m.counter_healthy=m.synthetic=true;
 m.source=Source::Relay1;m.state=State::Locked;m.root=1;m.boot=2;m.source_epoch=3;
 m.offset_root=1;m.offset_boot=2;m.offset_epoch=3;
 m.rms_ns=72;m.offset_ns=-18;m.counter_hz=80000000;m.xtal_hz=40000000;m.cpu_hz=240000000;
 m.psram_total=8*1024*1024;m.psram_free=7512*1024;m.rssi_dbm=-54;
 std::snprintf(m.parent,sizeof m.parent,"TC-01");std::snprintf(m.root_name,sizeof m.root_name,"TC-03");
 m.tz={2,-300};m.tz_until_us=now+750000;m.tz_received=33;
 m.holdover.last20=now*20;m.holdover.witness_age_us=0;m.holdover.p95_ns=240;m.holdover.p99_ns=380;m.holdover.max_ns=620;m.holdover.run_max_ns=840;
 m.holdover.age20=37*20000000ull;m.holdover.samples=128;m.holdover.trials=128;m.holdover.duration_ms=24000;
 m.holdover.ready=m.holdover.lower_bound=m.holdover.holding=true;m.holdover.started=12;m.holdover.completed=11;
 auto& r=m.mesh.roles;r.known=true;r.result=mesh::Result::Accepted;
 r.root=1;r.root_boot=2;r.epoch=3;r.hot=4;r.hot_boot=5;r.coordinator=4;r.members=7;r.sequence=12881;r.accepted=12880;r.replayed=2;
 r.wire_version=3;r.birth_utc_us=1792986374188267;r.founder=1;r.term=16;r.announcements=528;r.statuses=1641;r.role_reports=510;
 std::snprintf(r.founder_name,25,"TC-03");
 r.hot_confirmed=r.hot_report_known=true;r.hot_role=3;r.hot_report_kind=2;r.hot_received_us=now-1000000;r.hot_until_us=now+3500000;
 r.created_us=uint64_t(2*86400+6*3600+10*60)*1000000;r.epoch_since_us=now-312000000;r.received_us=now-180000;r.until_us=now+1820000;r.primary_since_us=now-uint64_t(2*3600+14*60+32)*1000000;r.hot_since_us=now-uint64_t(52*60+8)*1000000;
 std::snprintf(r.primary_name,sizeof r.primary_name,"TC-03");std::snprintf(r.hot_name,sizeof r.hot_name,"TC-02");std::snprintf(r.coordinator_name,sizeof r.coordinator_name,"TC-02");
 auto& path=m.mesh.path;path.known=path.authenticated=true;path.sampled_us=now;path.until_us=now+200000;path.source_epoch=3;path.count=2;
 path.nodes[0].id=1;path.nodes[0].boot=2;path.nodes[0].train=121;path.nodes[0].epoch=3;std::snprintf(path.nodes[0].name,25,"TC-03");
 path.nodes[1].id=6;path.nodes[1].boot=7;path.nodes[1].train=443;path.nodes[1].epoch=17;std::snprintf(path.nodes[1].name,25,"TC-01");
 auto& link=m.mesh.link;link.scope={1,2,3};link.known=true;link.sampled_us=now;link.until_us=now+200000;
 link.originals=952;link.missed=8;link.matched=949;link.duplicates=2;link.source_age_us=125000;link.source_ttl_us=1525000;
 m.mesh.capabilities={6,7,now,now+200000,6,true};
 auto& reference=m.mesh.reference;reference.reported=true;reference.scope={1,2,3};
 reference.received_us=now-200000;reference.until_us=now+29800000;reference.remote_sampled_us=1000000;
 reference.sequence=42;reference.fields=63;reference.hdop_centi=80;std::snprintf(reference.root_name,25,"TC-03");reference.values=7;reference.sats=14;reference.source_kind=1;
 reference.rmc_age_ms=100;reference.pps_age_ms=120;reference.gga_age_ms=150;
 reference.priority1=128;reference.clock_class=6;reference.accuracy=254;reference.variance=UINT16_MAX;reference.priority2=128;
 auto save=[&](const std::string& name,Screen screen,Model model,uint64_t at=now,Face face=Face::Instrument,Action pressed=Action::None,mesh::Page page=mesh::Page::Mesh){
  View v;v.screen=screen;v.face=face;v.pressed=pressed;v.diagnostic=page;render(frame.data(),model,v,at);
  std::ofstream f(dir/(name+".ppm"),std::ios::binary);f<<"P6\n320 240\n255\n";
  for(auto p:frame){const char rgb[]={char(((p>>11)&31)*255/31),char(((p>>5)&63)*255/63),char((p&31)*255/31)};f.write(rgb,3);}
 };
 save("launcher",Screen::Launcher,m);save("ptob",Screen::Clock,m);save("diagnostics",Screen::Diagnostics,m);
 auto between=m;between.utc_us+=SourcePulseUs;save("ptob-pps-off",Screen::Clock,between);
 save("launcher-pressed",Screen::Launcher,m,now,Face::Instrument,Action::OpenClock);
 save("ptob-register",Screen::Clock,m,now,Face::UtcRegister);
 m.mesh.reference_observation={{1,2,3},now-8072000000ull,now+29800000,true};
 save("face3-reference-watch",Screen::Clock,m,now,Face::ReferenceWatch);
 save("face4-epoch-counter",Screen::Clock,m,now,Face::EpochCounter);
 DisplayMemory history;history.retain(m);
 auto history_rc9=m;history_rc9.source=Source::Rc9;history_rc9.precision_valid=history_rc9.offset_valid=false;history.retain(history_rc9);
 save("register-rc9-history",Screen::Clock,history_rc9,now,Face::UtcRegister);
 auto history_search=m;history_search.source=Source::None;history_search.state=State::Search;history_search.utc_valid=false;history.retain(history_search);
 save("register-search-history",Screen::Clock,history_search,now,Face::UtcRegister);
 save("register-expired-history",Screen::Clock,m,now+SnapshotMaxUs,Face::UtcRegister);
 // Explicit brief-loss fixtures use the same presentation cache as hardware.
 DisplayMemory smoothing;auto seed=m;seed.delivery_observed_us=now;smoothing.retain(seed);
 for(uint64_t gap:{250000ull,499999ull,500000ull}){
  auto lost=seed;lost.sampled_us=now+gap;lost.utc_valid=lost.precision_valid=false;
  lost.root=lost.boot=lost.source_epoch=0;lost.state=State::Acquire;lost.source=Source::None;
  smoothing.retain(lost);const auto suffix=std::to_string(gap/1000)+"ms";
  save("clock-gap-"+suffix,Screen::Clock,lost,now+gap);
  save("shadow-gap-"+suffix,Screen::Diagnostics,lost,now+gap,Face::Instrument,Action::None,mesh::Page::Shadow);
 }
 auto ble=m;ble.source=Source::Grandmaster;save("ptob-ble",Screen::Clock,ble);
 auto rc2=m;rc2.source=Source::Relay2;save("ptob-rc2",Screen::Clock,rc2);
 auto rc9=m;rc9.source=Source::Rc9;rc9.state=State::Acquire;
 save("ptob-rc9",Screen::Clock,rc9);
 auto search=m;search.source=Source::None;search.state=State::Search;search.utc_valid=false;
 save("launcher-search",Screen::Launcher,search);save("ptob-search",Screen::Clock,search);
 save("ptob-stale",Screen::Clock,m,now+SnapshotMaxUs);
 auto fault=m;fault.state=State::Fault;save("ptob-fault",Screen::Clock,fault);
 auto wide=m;wide.rms_ns=99999;wide.offset_ns=-99999;save("ptob-wide",Screen::Clock,wide);
 auto hold=m;hold.state=State::Hold;save("ptob-hold",Screen::Clock,hold);
 auto uncal=m;uncal.precision_valid=uncal.offset_valid=false;uncal.state=State::Acquire;
 save("ptob-uncalibrated",Screen::Clock,uncal);
 auto diag=[&](const char* name,mesh::Page page,Model model){save(name,Screen::Diagnostics,model,now,Face::Instrument,Action::None,page);};
 diag("diag-mesh",mesh::Page::Mesh,m);diag("diag-path",mesh::Page::Path,m);diag("diag-quality",mesh::Page::Quality,m);
 diag("diag-shadow",mesh::Page::Shadow,m);
 auto shadow_warm=m;shadow_warm.holdover={};diag("diag-shadow-warm",mesh::Page::Shadow,shadow_warm);
 auto shadow_tail=m;shadow_tail.holdover.p99_ns=1480;shadow_tail.holdover.max_ns=3800;shadow_tail.holdover.run_max_ns=5800;shadow_tail.holdover.gaps=3;shadow_tail.holdover.lower_bound=false;shadow_tail.holdover.duration_ms=2000;shadow_tail.holdover.bad=1;diag("diag-shadow-tail",mesh::Page::Shadow,shadow_tail);
 auto shadow_units=m;shadow_units.holdover.p95_ns=1250;shadow_units.holdover.p99_ns=2410000;shadow_units.holdover.max_ns=1230000000;shadow_units.holdover.run_max_ns=1320000000;diag("diag-shadow-units",mesh::Page::Shadow,shadow_units);
 auto shadow_overflow=shadow_units;shadow_overflow.holdover.max_ns=1000000000000ull;diag("diag-shadow-overflow",mesh::Page::Shadow,shadow_overflow);
 auto tz_unknown=m;tz_unknown.tz_until_us=now;save("ptob-utc",Screen::Clock,tz_unknown);
 auto winter=m;winter.tz.offset_min=-360;save("ptob-cst",Screen::Clock,winter);
 diag("diag-reference",mesh::Page::Reference,m);diag("diag-wire",mesh::Page::Wire,m);
 auto expired=m;expired.mesh.roles.until_us=now;diag("diag-expired",mesh::Page::Mesh,expired);
 auto nohot=m;nohot.mesh.roles.hot=0;diag("diag-no-hot",mesh::Page::Mesh,nohot);
 auto empty=m;empty.mesh={};diag("diag-no-packets",mesh::Page::Mesh,empty);
 rc9.mesh.path={};rc9.mesh.link.upstream_rms_known=true;rc9.mesh.link.upstream_rms_ns=6800;rc9.mesh.link.upstream_samples=7;
 diag("diag-rc9-quality",mesh::Page::Quality,rc9);
 auto direct=m;direct.source=Source::Grandmaster;direct.mesh.path.count=1;direct.mesh.capabilities={1,2,now,now+200000,3,true};diag("diag-direct-gnss",mesh::Page::Reference,direct);
 auto reference_expired=m;reference_expired.mesh.reference.received_us=now-31000000;reference_expired.mesh.reference.until_us=now-1000000;diag("diag-reference-expired",mesh::Page::Reference,reference_expired);
 auto reference_unknown=m;reference_unknown.mesh.reference.fields=0;reference_unknown.mesh.reference.hdop_centi=UINT16_MAX;reference_unknown.mesh.reference.values=0;reference_unknown.mesh.reference.sats=UINT8_MAX;reference_unknown.mesh.reference.rmc_age_ms=reference_unknown.mesh.reference.pps_age_ms=reference_unknown.mesh.reference.gga_age_ms=UINT16_MAX;diag("diag-reference-unknown",mesh::Page::Reference,reference_unknown);
 auto reference_no=m;reference_no.mesh.reference.values=0;reference_no.mesh.reference.sats=0;reference_no.mesh.reference.rmc_age_ms=reference_no.mesh.reference.pps_age_ms=reference_no.mesh.reference.gga_age_ms=UINT16_MAX-1;diag("diag-reference-no",mesh::Page::Reference,reference_no);
 auto cached_no=reference_no;cached_no.mesh.reference.received_us=now-31000000;cached_no.mesh.reference.until_us=now-1000000;diag("diag-reference-cached-no",mesh::Page::Reference,cached_no);
 auto reference_changed=m;++reference_changed.source_epoch;diag("diag-reference-new-scope",mesh::Page::Reference,reference_changed);
 auto empty_selection=m;empty_selection.root=empty_selection.boot=0;empty_selection.source_epoch=0;empty_selection.utc_valid=empty_selection.precision_valid=false;empty_selection.source=Source::None;diag("diag-reference-last-gm",mesh::Page::Reference,empty_selection);
 auto reference_long=m;std::snprintf(reference_long.root_name,25,"LONG-ROOT-SOURCE-NAME");reference_long.mesh.reference.sats=99;reference_long.mesh.reference.hdop_centi=65534;reference_long.mesh.reference.clock_class=255;reference_long.mesh.reference.priority1=reference_long.mesh.reference.priority2=255;reference_long.mesh.reference.rmc_age_ms=reference_long.mesh.reference.pps_age_ms=reference_long.mesh.reference.gga_age_ms=UINT16_MAX-1;diag("diag-reference-long",mesh::Page::Reference,reference_long);
 auto no_utc=m;no_utc.utc_valid=false;diag("diag-wire-no-utc",mesh::Page::Wire,no_utc);
 auto other_scope=m;++other_scope.source_epoch;diag("diag-wire-other-scope",mesh::Page::Wire,other_scope);
 auto legacy_wire=m;legacy_wire.mesh.roles.wire_version=1;legacy_wire.mesh.roles.birth_utc_us=0;diag("diag-wire-legacy",mesh::Page::Wire,legacy_wire);
 auto nominee=m;nominee.mesh.roles.hot_confirmed=nominee.mesh.roles.hot_report_known=false;diag("diag-nominee",mesh::Page::Mesh,nominee);
 auto cached_hot=m;cached_hot.mesh.roles.hot_confirmed=false;cached_hot.mesh.roles.hot_until_us=now-1000000;cached_hot.mesh.roles.hot_received_us=now-5500000;diag("diag-hot-cached",mesh::Page::Mesh,cached_hot);
 auto yielding=m;yielding.mesh.roles.flags=8;diag("diag-yield",mesh::Page::Mesh,yielding);
 auto stale=m;stale.sampled_us=now-SnapshotMaxUs;diag("diag-stale",mesh::Page::Mesh,stale);
 auto longnames=m;std::snprintf(longnames.mesh.roles.primary_name,25,"PRIMARY-LONG-SOURCE-NAME");std::snprintf(longnames.mesh.roles.hot_name,25,"HOT-STANDBY-LONG-NAME");diag("diag-long-names",mesh::Page::Mesh,longnames);
 return 0;
}
