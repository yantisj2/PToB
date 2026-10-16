// Render the exact firmware framebuffer offline; no hardware/RF operation.
#define W12_DISPLAY_PREVIEW 1
#include "../legacy/display.cpp"
#include "../main/native_dashboard.hpp"
#include <cassert>
#include <fstream>
#include <string>
void save(const DisplayState& state,const std::string& path){
  display_show(state);std::ofstream out(path);out<<"P1\n128 64\n";
  for(int y=0;y<64;y++){
    for(int x=0;x<128;x++)out<<bool(fb[(y/8)*128+x]&(1u<<(y%8)))<<' ';
    out<<'\n';
  }
}
int main(int argc,char** argv){
  if(argc!=2)return 2;
  const std::string directory=argv[1];DisplayState state;state.unit_label="C-1";
  v12::R2Dashboard dashboard;v12::R2Status mesh;
  auto render=[&](const char* file){dashboard.apply(state,mesh);save(state,directory+'/'+file+".pbm");};
  render("off");assert(!state.valid&&!std::strcmp(state.followed_clock,"NONE")&&!std::strcmp(state.role,"OFF"));
  mesh.mode=v12::R2Mode::Passive;mesh.requested=true;render("passive");assert(!state.valid);
  mesh.mode=v12::R2Mode::Client;render("joining");assert(!state.valid&&!std::strcmp(state.role,"SEARCH"));
  mesh.clock_valid=mesh.following=true;mesh.root_boot=11;mesh.source_epoch=4;mesh.publication=1;
  mesh.follower_utc_us=UINT64_C(1791586531000000);
  const uint64_t identities[]={UINT64_C(0x0000000000000001),UINT64_C(0x0000000000000002),UINT64_C(0x0000000000000003)};
  for(unsigned root=0;root<3;root++){
    mesh.root_identity=identities[root];render(("follow-tc0"+std::to_string(root+1)).c_str());
    assert(state.valid&&!std::strcmp(state.role,"FOLLOW")&&!std::strcmp(state.followed_clock,v12::R2Dashboard::name(identities[root])));
    assert(!state.fit&&state.precision_ns==UINT32_MAX&&state.rms_ns==UINT32_MAX);
  }
  // Actual diagnosed display case: root TC-01, immediate parent TC-02 RC1.
  mesh.root_identity=identities[0];mesh.g1_clock_origin=identities[0];mesh.rc7_bound=true;mesh.rc7_depth=1;
  mesh.joined_peer=identities[1];mesh.joined_boot=22;render("relay-tc02-rc1");
  assert(!std::strcmp(state.followed_clock,"TC-02 RC1"));
  assert(!dashboard.needs_authority_update(mesh));mesh.rc7_depth=2;assert(dashboard.needs_authority_update(mesh));
  mesh.joined_peer=identities[2];render("relay-tc03-rc2");assert(!std::strcmp(state.followed_clock,"TC-03 RC2"));
  mesh.joined_peer=UINT64_C(0x0000000000000008);render("relay-ss3-rc2");
  assert(!std::strcmp(state.followed_clock,"SS3 RC2")&&!std::strcmp(state.r2_root_clock,"TC-01"));
  mesh.joined_peer=UINT64_C(0x11111111f6791470);render("relay-new-identity-rc2");
  assert(!std::strcmp(state.followed_clock,"B791470 RC2")&&!std::strcmp(state.r2_root_clock,"TC-01"));
  mesh.joined_peer=identities[2];
  mesh.following=false;mesh.root_identity=0;render("relay-held-tc03-rc2");
  assert(!std::strcmp(state.role,"HOLD")&&!std::strcmp(state.followed_clock,"TC-03 RC2"));
  mesh.clock_valid=false;render("relay-expired");assert(!std::strcmp(state.followed_clock,"NONE"));
  mesh.clock_valid=mesh.following=true;mesh.root_identity=identities[0];mesh.rc7_depth=1;mesh.joined_peer=identities[1];
  // Restore direct path for retained GM/held-clock/lock-duration previews.
  mesh.rc7_bound=false;mesh.joined_peer=mesh.joined_boot=0;mesh.rc7_depth=0;mesh.root_identity=identities[2];
  mesh.following=false;mesh.g1_clock_origin=mesh.root_identity;mesh.root_identity=0;render("hold");
  assert(state.valid&&!std::strcmp(state.role,"HOLD")&&!std::strcmp(state.followed_clock,"TC-03"));
  mesh.clock_valid=false;render("expired");assert(!state.valid&&!std::strcmp(state.followed_clock,"NONE"));
  mesh.clock_valid=mesh.following=true;mesh.root_identity=identities[0];mesh.fault=true;render("fault");
  assert(!state.valid&&!std::strcmp(state.role,"FAULT")&&!std::strcmp(state.followed_clock,"NONE"));
  mesh.fault=false;mesh.requested=false;mesh.mode=v12::R2Mode::Off;render("stopped");
  assert(!state.valid&&!std::strcmp(state.role,"OFF")&&!std::strcmp(state.followed_clock,"NONE"));
  mesh.requested=true;mesh.mode=v12::R2Mode::Client;mesh.model_available20=100;mesh.rms_ns=321;mesh.precision_fit=true;
  render("rms");assert(state.rms_ns==321&&!state.hold_1us_ms);
  mesh.hold_ms=1;mesh.hold_samples=99;render("rms-p99-acquiring");assert(!state.hold_1us_ms);
  mesh.hold_samples=100;render("p99-1ms");assert(state.hold_1us_ms==1);
  mesh.precision_fit=false;mesh.model_available20=0;mesh.g1_clock_origin=identities[0];mesh.g1_clock_model={40000000,2000000000,0,2000,0};
  mesh.follower_snapshot_us=2000000;mesh.g1_anchor_deadline_us=4000000;
  auto& q=mesh.g1_relative_quality;q.original20=38000000;q.evaluated20=40000000;q.samples=16;q.rms_ns=234;q.max_ns=500;q.rate_guard_ppb=20;
  render("relative-settling");assert(!state.r2_relative_lock&&!state.fit&&state.rms_ns==234&&!state.hold_1us_ms);
  q.evaluated20+=40000000;render("relative-lock-est");assert(state.r2_relative_lock&&!state.fit&&state.rms_ns==234&&state.hold_1us_ms==2000);
  assert(state.r2_lock_seconds==0);
  q.evaluated20+=59*20000000ULL;q.original20=q.evaluated20;mesh.g1_clock_model.tick=q.evaluated20;
  render("relative-lock-00-59");assert(state.r2_lock_seconds==59);
  q.evaluated20+=20000000;q.original20=q.evaluated20;mesh.g1_clock_model.tick=q.evaluated20;
  render("relative-lock-01-00");assert(state.r2_lock_seconds==60);
  q.evaluated20+=uint64_t(9999*60+59-60)*20000000;q.original20=q.evaluated20;mesh.g1_clock_model.tick=q.evaluated20;
  render("relative-lock-9999-59");assert(state.r2_lock_seconds==9999*60+59);
  mesh.rc7_bound=true;mesh.rc7_depth=1;mesh.joined_peer=identities[1];mesh.joined_boot=22;
  render("relay-lock-tc02-rc1-9999-59");assert(!std::strcmp(state.followed_clock,"TC-02 RC1")&&state.r2_relative_lock);
  mesh.rc7_bound=false;mesh.rc7_depth=0;mesh.joined_peer=mesh.joined_boot=0;
  mesh.rc7_relay_only=mesh.rc7_relay_only_requested=true;render("relative-lock-forced-relay");
  mesh.rc7_relay_only=mesh.rc7_relay_only_requested=false;
  state.page=display_pages::Hold;render("relative-hold-page");state.page=0;
  mesh.following=false;mesh.root_identity=0;mesh.g1_clock_origin=identities[0];render("relative-held");
  assert(state.r2_relative_lock&&!state.fit&&state.hold_1us_ms==2000&&state.rms_ns==234&&!std::strcmp(state.role,"HOLD"));
  assert(state.r2_lock_seconds==9999*60+59);
  mesh.following=true;mesh.root_identity=identities[0];render("relative-lock-reacquired");assert(state.r2_relative_lock&&state.r2_lock_seconds==9999*60+59);
  q.evaluated20+=40000000;q.original20=q.evaluated20;mesh.g1_clock_model.tick=q.evaluated20;
  render("relative-lock-new-session");assert(state.r2_lock_seconds==9999*60+61);
  q.rms_ns=500;q.max_ns=1600;render("relative-zero-budget");assert(state.hold_estimated&&!state.hold_1us_ms&&!state.r2_relative_lock&&!state.r2_lock_seconds);
  q.samples=3;q.rms_ns=50321;render("relative-acquiring");assert(!state.hold_estimated&&state.rms_ns==50321);
  q.samples=16;q.rms_ns=UINT32_MAX-1;render("relative-large-error");assert(!state.hold_estimated&&state.rms_ns==UINT32_MAX-1);
  mesh.rc7_upstream_mode_pending=true;render("mode-switch-pending");
  assert(!state.valid&&!state.r2_relative_lock&&!std::strcmp(state.followed_clock,"NONE"));
  // A roster position or coincident low32 identity must never label another GM.
  assert(!std::strcmp(v12::R2Dashboard::name(UINT64_C(0x11111111f6791470)),"UNKNOWN"));
  std::puts("PASS dashboard: exact identity names, GM rotation, expiry, fault/OFF, current RMS and P99 1ms with100 fresh trials; synthetic quality preview only");
}
