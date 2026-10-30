// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#define W12_DISPLAY_PREVIEW 1
#include "../legacy/display.cpp"
#include "../main/native_dashboard.hpp"
#include <cassert>
#include <array>
#include <fstream>
#include <string>
void image(const DisplayState& s,const std::string& path){display_show(s);std::ofstream out(path);out<<"P1\n128 64\n";
  for(int y=0;y<64;++y){for(int x=0;x<128;++x)out<<bool(fb[(y/8)*128+x]&(1u<<(y%8)))<<' ';out<<'\n';}}
int main(int argc,char** argv){
  assert(argc==2);const std::string out=argv[1];DisplayState s;s.unit_label="C-1";
  v12::R2Dashboard dashboard;v12::R2Status mesh;mesh.requested=mesh.clock_valid=mesh.following=true;
  mesh.root_identity=1;mesh.root_boot=2;mesh.source_epoch=mesh.publication=1;mesh.follower_utc_us=1793318400000000ULL;
  mesh.model_available20=1;mesh.rms_ns=49;mesh.g1_shadow.samples=99;
  dashboard.apply(s,mesh);assert(s.shadow_holdover&&s.shadow_p99_ns==UINT64_MAX);image(s,out+"/shadow-warmup.pbm");
  mesh.g1_shadow.samples=100;mesh.g1_shadow.p99_ns=123;dashboard.apply(s,mesh);
  assert(s.shadow_samples==100&&s.shadow_p99_ns==123);image(s,out+"/shadow-ready.pbm");
  std::array<uint8_t,sizeof fb> actual{};std::copy(std::begin(fb),std::end(fb),actual.begin());
  std::fill(std::begin(fb),std::end(fb),0);text(2,56,"RMS 49ns P99:123ns");
  for(unsigned x=0;x<128;++x)assert(actual[7*128+x]==fb[7*128+x]);
  mesh.g1_shadow.holding=true;mesh.g1_shadow.age20=mesh.g1_shadow.safe20=65*20000000ULL;dashboard.apply(s,mesh);
  assert(s.shadow_age_ms==65000&&s.shadow_safe_ms==65000);s.page=display_pages::Hold;image(s,out+"/shadow-details.pbm");
  mesh.g1_shadow.cross20=66*20000000ULL;dashboard.apply(s,mesh);assert(s.shadow_cross_ms==66000);image(s,out+"/shadow-crossed.pbm");
  mesh.g1_shadow.gaps=3;dashboard.apply(s,mesh);assert(s.shadow_gaps==3);image(s,out+"/shadow-gaps.pbm");
  mesh.g1_shadow.p99_ns=UINT64_MAX;dashboard.apply(s,mesh);assert(s.shadow_p99_ns==UINT64_MAX);
  s.holdover_production=true;s.holdover_selected=true;s.shadow_gaps=0;image(s,out+"/production-hold.pbm");
  std::puts("PASS actual shadow framebuffer: warmup, measured P99 footer pixels, extended run and threshold details and expiry");
}
