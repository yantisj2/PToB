// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Production renderer with illustrative values, never a hardware capture.
#include "ui.hpp"
#include <cstdio>
#include <cstring>
int main(int argc,char** argv){
 if(argc!=3)return 2;n30::Model m;std::snprintf(m.unit,sizeof m.unit,"%s",argv[2]);
 std::snprintf(m.parent,sizeof m.parent,"TC-02");m.sampled_us=m.utc_at_us=1000000;m.until_us=1250000;
 m.utc_us=10ULL*3600000000+10ULL*60000000+42000000;m.valid=m.locked=true;
 m.state=n30::State::Follow;m.source=n30::Source::Ble;m.rms_ns=65;m.hz=16;m.delivery=950;
 m.lamps[0]=m.lamps[2]=m.lamps[4]=1;m.lamps[3]=1;
 const auto fixture_utc=m.utc_us;
 m.ble={m.utc_at_us,m.utc_us,m.until_us,1,2,3,true,true};
 uint8_t frame[n30::FrameBytes];n30::View view;
 const char* names[]={"clock","rf","saver-ble","saver-rc1","saver-rc2","clock-relay","clock-pending","clock-grace","clock-syncing","saver-grace","saver-recovered","saver-expired","clock-pps-off","saver-pps-off","lora-off","lora-on","lora-fallback","lora-fault","rf-dual","minimal-top-on","minimal-middle-on","minimal-bottom-on","minimal-top-off","minimal-middle-off","minimal-bottom-off"};
 for(unsigned i=0;i<25;i++){
  view={};view.page=i==1;view.saver=i>=2&&i<=4;view.saver_position=view.saver?i-2:0;
  m.source=i==3?n30::Source::Rc1:i==4?n30::Source::Rc2:n30::Source::Ble;
  m.relay_only=i==5;m.mode_pending=i==6;if(m.mode_pending)m.valid=m.locked=false;
  uint64_t now=1000000;
  n30::ClockHold hold;
  if(i>=7){
   m.valid=m.locked=true;m.mode_pending=false;
   m.sampled_us=m.utc_at_us=now;m.until_us=now+250000;m.utc_us=fixture_utc+(i<9?900000:0);
   if(i>=9){view.saver=true;view.saver_position=2;m.source=n30::Source::Rc2;}
   hold.update(m,view,now,i>=9);hold.draw(frame,m,view,now);hold.presented(m,view,now,frame);m.locked=false;
   view.saver=false;hold.update(m,view,1100000,i>=9);now=i==8||i==11?1600000:1200000;
   m.sampled_us=m.utc_at_us=now;m.until_us=now+250000;m.utc_us+=now-1000000;
   if(i==10){m.locked=true;view.saver=true;view.saver_position=2;}
   else view.saver=false;
   hold.update(m,view,now,i>=9);
  }
  if(i>=12){m.valid=m.locked=true;m.mode_pending=false;m.sampled_us=m.utc_at_us=now;m.until_us=now+250000;m.utc_us=fixture_utc+250000;view={};view.saver=i==13;view.saver_position=2;m.source=i==13?n30::Source::Rc2:n30::Source::Ble;}
  if(i>=14){
   view={};view.page=i==18?1:n30::LoRaPage;m.source=n30::Source::Ble;m.ble={now,fixture_utc,now+250000,1,2,3,true,true};m.sampled_us=now;
   m.lora={};auto& l=m.lora;l.sampled_us=now;l.mode=i==15?1:0;l.transition_remaining_ms=127000;l.age_ms=UINT32_MAX;
   l.requested=l.active=i==15||i==16||i==18;l.fault=i==17;
   if(l.active){l.utc_us=fixture_utc;l.until_us=now+12000000;l.state=2;l.age_ms=400;l.root=0x1234;l.parent=0x5678;l.link_samples=16;l.link_rms_ns=275;l.metadata_known=true;l.kind=2;}
   if(i==16){m.ble.valid=false;m.source=n30::Source::LoRa;}
   if(i==18){m.delta_valid=m.drift_valid=true;m.delta_ns=-1123;m.drift_ppb=24;m.lora_lamps[0]=m.lora_lamps[2]=m.lora_lamps[4]=1;}
  }
  if(i>=19){
   view={};view.saver=view.minimal=true;view.saver_position=(i-19)%3;
   m.valid=m.locked=true;m.mode_pending=false;m.source=n30::Source::Ble;
   m.sampled_us=m.utc_at_us=now;m.until_us=now+250000;m.utc_us=fixture_utc+(i>=22?250000:0);
   hold.reset();
  }
  hold.draw(frame,m,view,now);char path[1024];std::snprintf(path,sizeof path,"%s/%s.pgm",argv[1],names[i]);
  auto f=std::fopen(path,"wb");if(!f)return 3;std::fprintf(f,"P5\n64 128\n255\n");
  for(unsigned y=0;y<128;y++)for(unsigned x=0;x<64;x++)std::fputc(frame[y*8+x/8]&(0x80>>(x%8))?255:0,f);
  std::fclose(f);
 }
 // New page fixtures exercise the actual renderer, not a graphic mockup.
 const char* precision_names[]={"precision-lock","precision-acquire","precision-stale","precision-legacy-rc1","precision-microseconds","precision-long-lock","precision-long-root-rc2","precision-rc1","precision-fault","precision-root-unknown","precision-rc2-pps-off"};
 for(unsigned i=0;i<11;++i){
  n30::Model p;p.sampled_us=p.utc_at_us=5000000000;p.until_us=p.sampled_us+250000;
  p.utc_us=10ULL*3600000000;p.valid=p.locked=true;p.state=n30::State::Follow;p.source=n30::Source::Ble;
  std::snprintf(p.root,sizeof p.root,"TC-02");
  auto& q=p.precision;q.valid=q.locked=q.lock_age_valid=q.relative_known=true;q.scope=1;
  q.until_us=p.until_us;q.rms_ns=170;q.relative_ppb=123;q.lock_since_us=p.sampled_us-754000000;
  q.counter_hz=80000000;q.source=p.source;
  if(i==0)q.rms_ns=49; // Observed N3-2 internal causal RMS; other fields remain illustrative.
  if(i==1){q.locked=q.lock_age_valid=false;q.rms_ns=UINT32_MAX;q.relative_known=false;}
  if(i==3){q.counter_hz=20000000;q.source=n30::Source::Rc1;p.utc_us+=250000;q.relative_ppb=-123;}
  if(i==4){q.rms_ns=1234;q.relative_ppb=-12;}
  if(i==5){p.sampled_us=p.utc_at_us=400000000000;p.until_us=p.sampled_us+250000;q.until_us=p.until_us;q.lock_since_us=1;}
  if(i==6){std::snprintf(p.root,sizeof p.root,"ABCDEFGHIJKLMNOPQRSTUVWX");q.source=n30::Source::Rc2;q.relative_known=false;}
  if(i==7){q.source=n30::Source::Rc1;q.relative_ppb=-123;}
  if(i==8){p.state=n30::State::Fault;p.valid=p.locked=q.valid=q.locked=q.lock_age_valid=q.relative_known=false;}
  if(i==9)std::snprintf(p.root,sizeof p.root,"----");
  if(i==10){q.source=n30::Source::Rc2;p.utc_us+=250000;q.relative_ppb=0;}
  p.ble={p.utc_at_us,p.utc_us,p.until_us,1,2,3,p.valid,true};
  n30::View v;v.page=n30::PrecisionPage;n30::render(frame,p,v,p.sampled_us+(i==2?250000:0));
  char path[1024];std::snprintf(path,sizeof path,"%s/%s.pgm",argv[1],precision_names[i]);
  auto f=std::fopen(path,"wb");if(!f)return 3;std::fprintf(f,"P5\n64 128\n255\n");
  for(unsigned y=0;y<128;y++)for(unsigned x=0;x<64;x++)std::fputc(frame[y*8+x/8]&(0x80>>(x%8))?255:0,f);
  std::fclose(f);
 }
 n30::render_splash(frame);
 {char path[1024];std::snprintf(path,sizeof path,"%s/splash.pgm",argv[1]);auto f=std::fopen(path,"wb");if(!f)return 3;
  std::fprintf(f,"P5\n64 128\n255\n");for(unsigned y=0;y<128;y++)for(unsigned x=0;x<64;x++)std::fputc(frame[y*8+x/8]&(0x80>>(x%8))?255:0,f);std::fclose(f);}
 const char* rc9_names[]={"clock-rc9","precision-rc9","saver-rc9"};
 for(unsigned i=0;i<3;i++){
  n30::Model p;p.sampled_us=p.utc_at_us=1000000;p.until_us=1250000;p.utc_us=fixture_utc;
  p.valid=true;p.state=n30::State::Follow;p.source=n30::Source::Rc9;
  std::snprintf(p.unit,sizeof p.unit,"%s",argv[2]);std::snprintf(p.parent,sizeof p.parent,"C-4");std::snprintf(p.root,sizeof p.root,"TC-03");
  n30::View v;v.page=i==1?n30::PrecisionPage:0;v.saver=i==2;v.saver_position=1;
  n30::render(frame,p,v,p.sampled_us);char path[1024];std::snprintf(path,sizeof path,"%s/%s.pgm",argv[1],rc9_names[i]);
  auto f=std::fopen(path,"wb");if(!f)return 3;std::fprintf(f,"P5\n64 128\n255\n");
  for(unsigned y=0;y<128;y++)for(unsigned x=0;x<64;x++)std::fputc(frame[y*8+x/8]&(0x80>>(x%8))?255:0,f);
  std::fclose(f);
 }

}
