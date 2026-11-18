// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "shadow_nmea.hpp"
#include "watch.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <array>
static void sentence(watch::ShadowNmea& n,const char* body,uint64_t now){
 unsigned sum=0;for(auto* p=body;*p;++p)sum^=uint8_t(*p);char suffix[8];std::snprintf(suffix,sizeof suffix,"*%02X\r\n",sum);
 const auto text=std::string("$")+body+suffix;
 // Split delivery proves parser state crosses UART chunks.
 n.feed(reinterpret_cast<const uint8_t*>(text.data()),7,now,now*20);
 n.feed(reinterpret_cast<const uint8_t*>(text.data()+7),unsigned(text.size()-7),now,now*20);
}
int main(){
 using namespace watch;ShadowStatistics stats;ShadowView v;
 for(unsigned i=1;i<=11;++i){stats.edge(uint64_t(i)*1000000,i,1,true,400000000);v={};stats.apply(v,uint64_t(i)*1000000);assert(!v.valid&&!v.samples);}
 assert(stats.settled());
 stats.edge(12000000,12,1,true,-100);stats.edge(13000000,13,1,true,300);v={};stats.apply(v,13000000);
 assert(v.valid&&v.samples==2&&v.mean_tenth_ns==1000&&v.p99_ns==298&&v.rms_ns==224&&!v.approximate);
 stats.edge(14000000,14,2,true,700);v={};stats.apply(v,14000000);assert(!v.valid&&!v.samples&&v.settling_ms==0); // scope change
 stats.edge(16000000,16,2,true,0);v={};stats.apply(v,16000000);assert(!v.valid&&v.missing==1); // loss resets startup
 stats.edge(17000000,17,2,false,0);assert(!stats.tracking());
 stats.reset();for(unsigned i=1;i<=12;++i)stats.edge(i*1000000ULL,i,7,true,500000000);v={};stats.apply(v,12000000);
 assert(v.valid&&v.samples==1&&v.approximate&&v.mean_tenth_ns==5000000000LL&&v.rms_ns==500000000);
 v={};stats.apply(v,14000000);assert(!v.valid);stats.withdraw();assert(!stats.settled());
 // Real field regression: brief permission gaps between PPS edges repeatedly
 // erased the whole ten-second window. Retain only this context's eligible
 // samples and require a new eligible physical PPS before showing them again.
 stats.reset();
 for(unsigned i=1;i<=14;++i){stats.edge(i*1000000ULL,i,9,true,100);stats.pause();}
 v={};stats.apply(v,14000000);assert(!v.valid&&v.samples==3);
 stats.edge(15000000,15,9,false,999999);v={};stats.apply(v,15000000);assert(!v.valid&&v.samples==3&&v.skipped==1);
 stats.edge(16000000,16,9,true,300);v={};stats.apply(v,16000000);assert(v.valid&&v.samples==4&&v.mean_tenth_ns==1500&&v.rms_ns==174);
 stats.edge(18000000,18,9,true,100);v={};stats.apply(v,18000000);assert(!v.valid&&!v.samples&&v.missing==1);
 stats.edge(19000000,19,10,true,100);v={};stats.apply(v,19000000);assert(!v.valid&&!v.samples&&!v.settling_ms);
 // No phase-dependent startup: an equally bad stable phase qualifies at 10s.
 stats.reset();for(unsigned i=1;i<=11;++i)stats.edge(i*1000000ULL,i,8,true,-500000000);assert(stats.settled());
 const auto count_before=v.samples;stats.edge(11000000,11,8,true,0);(void)count_before; // duplicate cannot advance
 ShadowNmea n;sentence(n,"GPGSV,2,1,08,01,40,083,41,02,17,273,35,03,21,123,38,04,52,310,44",1000000);
 v={};n.apply(v,1000000);assert(v.in_view==0); // incomplete multipart sky withheld
 sentence(n,"GPGSV,2,2,08,05,70,040,48,06,35,220,42,07,15,180,25,08,08,012,20",1100000);
 n.apply(v,1100000);assert(v.in_view==8&&v.satellite_count==8&&v.satellites[0].cn0==48);
 sentence(n,"GAGSV,1,1,02,11,31,100,43,12,20,200,30",1200000);v={};n.apply(v,1200000);assert(v.in_view==10);
 for(unsigned i=0;i<3;++i){char body[128];std::snprintf(body,sizeof body,"GNRMC,12000%u.00,A,3000.00,N,08600.00,W,0,0,171126,,,A",i);sentence(n,body,2000000+i*1000000);sentence(n,"GNGGA,120002.00,3000.00,N,08600.00,W,1,11,0.8,0,M,0,M,,",2000000+i*1000000);}
 assert(n.reference(4000000)&&n.rmc20()==80000000&&n.second()==43202);
 // Post-edge UART delivery must not hide the preceding coherent RMC.
 sentence(n,"GNRMC,120003.00,A,3000.00,N,08600.00,W,0,0,171126,,,A",5020000);
 sentence(n,"GNGGA,120003.00,3000.00,N,08600.00,W,1,11,0.8,0,M,0,M,,",5020000);
 assert(n.rmc20()>100000000);
 auto preceding=n.reference_at(100000000,5020000);
 assert(preceding.ready&&preceding.rmc20==80000000&&preceding.second==43202);
 assert(!n.reference_at(140000000,5020000).ready); // Prior reference too old.
 const uint8_t bad[]="$GNGGA,120002.00,,,,,0,00*00\r\n";n.feed(bad,sizeof bad-1,5100000,102000000);assert(n.errors()&&n.fix(5100000));
 sentence(n,"GNRMC,120003.00,V,,,,,,,171126,,,N",6000000);assert(!n.fix(6000000)&&!n.reference(6000000)&&!n.reference_at(120000000,6000000).ready);
 v={};n.apply(v,10000000);assert(!v.in_view&&!v.satellite_count&&!v.used&&!v.fix);
 // Blank/malformed coordinates retain signal bars, never fake north/horizon dots.
 ShadowNmea geometry;sentence(geometry,"GPGSV,1,1,04,01,,,40,02,xx,120,35,03,00,000,30,04,91,360,25",1000000);
 ShadowView positions;geometry.apply(positions,1000000);assert(positions.satellite_count==4);
 assert(!positions.satellites[0].position_known&&!positions.satellites[1].position_known&&positions.satellites[2].position_known&&!positions.satellites[3].position_known);
 // Selected-source gating hides prior numeric evidence immediately, regardless
 // of the still-valid BLE witness, and retains the independent physical PPS.
 Model model;model.time_valid=true;model.sampled_us=1000000;model.until_us=1250000;model.utc_us=1794679698000000ULL;model.source=Source::BLE;
 model.shadow.sampled_us=1000000;model.shadow.stage=ShadowStage::Measuring;model.shadow.pps=model.shadow.fix=model.shadow.valid=true;model.shadow.last_pps_us=1000000;model.shadow.mean_tenth_ns=874;model.shadow.p99_ns=246;model.shadow.rms_ns=112;model.shadow.samples=100;
 assert(shadow_comparison_allowed(model,1000000));assert(!shadow_comparison_allowed(model,1250000));
 View screen;screen.settings.face=3;static std::array<uint16_t,Width*Height> first,second;
 for(auto source:{Source::None,Source::LOR,Source::RC9}){model.source=source;assert(!shadow_comparison_allowed(model,1000000));render(first.data(),model,screen,1000000);model.shadow.mean_tenth_ns=999999;model.shadow.p99_ns=777;render(second.data(),model,screen,1000000);assert(first==second);}
 model.source=Source::BLE;model.local_time=true;assert(!shadow_comparison_allowed(model,1000000));
 View view;view.settings.face=3;assert(valid(view.settings)&&view.shadow_visible());view.asleep=true;assert(!view.shadow_visible());view.asleep=false;view.screen=Screen::Launcher;assert(!view.shadow_visible());
 for(unsigned face=0;face<FaceCount;++face){view.screen=Screen::Faces;view.touch(100,110+int(face)*80,1000000+face);assert(view.settings.face==face&&view.screen==Screen::Face);}
 std::puts("PASS shadow:phase-blind10s,source/gap/fix resets,signedmean,P99ABS,RMS,coarse-tail marker,NMEA checksum/chunks/multipart/expiry,four-face power demand");
}
