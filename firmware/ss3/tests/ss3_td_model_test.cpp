#include "ss3_td_model.hpp"
#include "coarse_time_zone.hpp"
#include "../legacy/m5stick_s3/ui.hpp"
#include "../legacy/m5stick_s3/td_chart.hpp"
#include "../legacy/m5stick_s3/td_controls.hpp"
#include "../legacy/m5stick_s3/td_health.hpp"
#include "../legacy/m5stick_s3/td_relays.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
int main(int argc,char** argv){
 using namespace v12;TdModel model;
 static_assert(sticks3::PanelMadctl==0x08);
 {TdModel h;
  // Five relays formerly alternated four rows / one row at each 8s boundary.
  for(unsigned i=1;i<=5;++i)h.seen(i,9,true,1,i%2,true,int8_t(-30-i*5),1,7500000);
  h.peer(5).rssi=-10; // A raw spike must not outrank the displayed smoothed RSSI.
  h.peer(3).signal.value=h.peer(2).signal.value; // Stable identity tie-break.
  h.peer(4).signal={}; // Fresh advert, unknown signal sorts last.
  sticks3::View view;view.td.enabled=true;view.page=2;view.td=h.view;
  uint8_t before[sticks3::FrameBytes]{},after[sticks3::FrameBytes]{};
  for(const uint64_t boundary:{8000000ULL,16000000ULL}){
   for(auto& p:view.td.peers)if(p.id){p.seen_us=boundary-500000;if(p.signal.initialized)p.signal.seen_us=p.seen_us;}
   view.td.now_us=boundary-1;const sticks3::TdRelayList list(view.td);
   assert(list.count==4&&list.total==5&&list.rows[0]->id==1&&list.rows[1]->id==2&&list.rows[2]->id==3&&list.rows[3]->id==5);
   sticks3::RenderAudit audit;sticks3::render(before,view,&audit);
   ++view.td.now_us;sticks3::render(after,view,&audit);
   assert(!std::memcmp(before,after,sizeof before)&&!audit.clipped_pixels&&!audit.text_overflow);
  }
  view.td.now_us=26000000;assert(sticks3::TdRelayList(view.td).count==0);
  // Full capacity, fresh non-relays excluded, unknown and expired RSSI last.
  for(unsigned i=6;i<=8;++i)h.seen(i,9,true,1,true,true,-40,1,8000000);
  h.view.now_us=8000000;h.peer(8).relay=false;h.peer(7).signal.seen_us=18000000;
  const sticks3::TdRelayList full(h.view);assert(full.total==7&&full.count==4&&full.rows[0]->id==1&&full.rows[3]->id==6);
 }
 {TdModel h;h.seen(1,1,true,1,true,true,-50,1,1000000);h.update_chart(1000000);assert(h.view.chart_relay==1);
  h.seen(2,1,true,1,true,true,-48,1,2000000);h.update_chart(2000000);assert(h.view.chart_relay==1);
  for(uint64_t now=3000000;now<=32000000;now+=1000000){h.seen(1,1,true,1,true,true,-50,1,now);h.seen(2,1,true,1,true,true,-42,1,now);h.update_chart(now);assert(h.view.chart_relay==(now<31000000?1:2));}
  h.view.plot_relay=2;h.view.relay[22]={32000000,2,450,990,-55};h.seen(1,1,true,1,true,true,-30,1,33000000);h.update_chart(33000000);assert(h.view.chart_relay==2&&h.view.plot_relay==2);
  h.peer(2).publishing=false;h.update_chart(34000000);h.seen(1,1,true,1,true,true,-30,1,53000000);h.update_chart(53000000);assert(h.view.chart_relay==2);
  h.seen(1,1,true,1,true,true,-30,1,54000000);h.update_chart(54000000);assert(h.view.chart_relay==1&&h.view.plot_relay==2);
  assert(sticks3::chart_point(h.view.relay[22],2,54000000,0));}
 {TdModel loss;loss.seen(1,1,true,1,true,true,-50,1,1);loss.begin(1);loss.packet(1,1,1,20,1);loss.packet(2,4,1,40,2);loss.packet(3,5,1,60,3);loss.signal(-50,1);loss.signal(-70,2);loss.finish(4);
  assert(loss.view.relay[23].delivery==600&&loss.view.relay[23].rssi==-60);
  loss.begin(1);loss.packet(4,100,1,80,5);loss.packet(5,101,1,100,6);loss.finish(7);assert(loss.view.relay[23].delivery==1000);
  loss.begin(1);loss.finish(8);assert(loss.view.relay[23].delivery==UINT16_MAX&&loss.view.relay[23].rssi==127);}
 {sticks3::TdLogScale scale{0,4};assert(scale.y(0)==scale.y(1));assert(scale.y(1)-scale.y(10)==16&&scale.y(10)-scale.y(100)==16&&scale.y(100)-scale.y(1000)==16);
  TdSnapshot d;d.now_us=125000000;d.gm[23]={124000000,1,180,1000};d.relay[23]={124000000,2,620,1000};d.gm[22]={123000000,99,UINT32_MAX-1,1};
  auto field=sticks3::TdLogScale::range(d,1,2);assert(field.lower==2&&field.upper==3&&field.y(180)-field.y(620)>30);
  d.relay[22]={120000000,2,1000000,1000};field=sticks3::TdLogScale::range(d,1,2);assert(field.lower==2&&field.upper==6&&field.y(180)-field.y(620)>8);
  d.gm[23].jitter_ns=0;field=sticks3::TdLogScale::range(d,1,2);assert(field.lower==0&&field.upper==6);}
 model.seen(1,1,false,0,true,true,-60,1,1000000);model.seen(2,1,true,1,true,true,-45,1,1000000);model.seen(3,1,true,2,false,false,-30,1,1000000);
 assert(model.best(false,1000000)==1&&model.best(true,1000000)==2&&model.best(true,1000000,true)==3);assert(!model.best(true,11000000));
 {TdModel roles;roles.seen(10,10,false,0,true,true,-50,1,1000000);roles.peer(10).boot=4;roles.peer(10).pa_seen_us=1000000;
  roles.view.role.known=roles.view.role.available=true;roles.view.role.root=10;roles.view.role.root_boot=4;roles.view.role.hot=30;
  assert(roles.snapshot(1000000).role.available);
  roles.seen(9,9,false,0,true,true,-50,1,1000000);roles.peer(9).pa_seen_us=1000000;
  assert(TdModel::best_peer(roles.view,false,1000000)->id==9&&!roles.snapshot(1000000).role.available); // Same selector for LCD and owner, including stable tie.
  roles.peer(9).publishing=false;roles.peer(10).boot=5;assert(!roles.snapshot(1000000).role.available);
  roles.peer(10).boot=4;roles.view.role.source_epoch=4;roles.peer(10).source_epoch=5;assert(!roles.snapshot(1000000).role.available);roles.peer(10).source_epoch=4;assert(roles.snapshot(1000000).role.available);roles.seen(10,10,false,0,true,true,-50,1,12000000);assert(!roles.snapshot(12000000).role.available);}
 model.begin(1);model.scope(4,5,6);
 for(unsigned i=0;i<12;i++){const auto rx=20000000ULL+i*1250000,tx=1000000000ULL+i*62500000;
  model.packet(10+i,uint16_t(65530+i),i%37,rx,1000000+i*62500);assert(model.correction(10+i,uint16_t(65530+i),i%37,tx,1000000+i*62500));assert(!model.correction(10+i,uint16_t(65530+i),i%37,tx,1000000+i*62500));}
 assert(model.peer(1).jitter_ns==0&&model.peer(1).matched==12&&model.peer(1).missed==0);
 model.packet(23,8,4,38000000,1800000);assert(model.peer(1).missed==2);model.packet(23,8,4,38000000,1800000);assert(model.peer(1).duplicates==1);
 model.finish(1800000);assert(model.view.gm[23].jitter_ns==0);model.scope(44,5,6);model.finish(1900000);assert(model.view.gm[23].jitter_ns==UINT32_MAX);
 assert(!model.correction(23,8,4,1900000000,1900000));
 for(unsigned i=0;i<100;i++)model.seen(100+i,1,true,i%3,true,true,-50,1,2000000+i);assert(model.snapshot(2001000).count==8);
 // The real strip renderer must equal the full frame, with no clipping or text overlap.
 for(unsigned state=0;state<4;state++)for(unsigned metric=0;metric<3;metric++)for(unsigned page=0;page<v12::TdPageCount;page++){
  sticks3::View view;view.native_dashboard=true;view.td.enabled=true;view.page=page;view.td_chart=metric;view.td.now_us=135000000;
  if(state){view.accepted=789;view.td.count=2;view.td.target=0x0000000000000001ULL;auto& gm=view.td.peers[0];gm.id=gm.root=view.td.target;gm.seen_us=gm.sampled_us=gm.quality_us=view.td.now_us;gm.rssi=-54;gm.rssi_min=-62;gm.rssi_max=-49;gm.publishing=true;gm.signal.observe(gm.rssi,view.td.now_us);gm.jitter_ns=180;gm.max_ns=750;gm.packets=789;gm.matched=755;gm.missed=21;gm.channels=0x1fffffffffULL;gm.time_known=gm.time_valid=true;gm.time_seen_us=view.td.now_us-200000;gm.origin_age_us=250000;gm.source_ttl_us=1400000;gm.source_epoch=4;
   auto& rc=view.td.peers[1];rc=gm;rc.id=0x0000000000000003ULL;rc.relay=true;rc.depth=1;rc.stream_slot=1;rc.rssi=-43;rc.signal={};rc.signal.observe(rc.rssi,view.td.now_us);rc.jitter_ns=620;rc.max_ns=2420;
   auto& rc2=view.td.peers[2];rc2=rc;rc2.id=0x0000000000000004ULL;rc2.stream_slot=2;
   auto& rc3=view.td.peers[3];rc3=rc;rc3.id=0x0000000000000002ULL;rc3.stream_slot=3;
   auto& idle=view.td.peers[4];idle=rc;idle.id=0x0000000000000005ULL;idle.publishing=false;idle.stream_slot=UINT8_MAX;idle.rssi=127;
   view.td.count=5;
   view.td.chart_gm=view.td.plot_gm=gm.id;view.td.chart_relay=view.td.plot_relay=rc.id;view.td.role.known=view.td.role.available=true;view.td.role.hot=0x0000000000000004ULL;view.td.role.root=gm.id;view.td.role.root_boot=0x123456789abcdefULL;view.td.role.source_epoch=4;view.td.role.seen_us=view.td.now_us-2000000;
   for(unsigned i=0;i<24;i++){const auto at=10000000ULL+i*5000000;view.td.gm[i]={at,gm.id,180+i*4,980,int8_t(-60+i%4)};view.td.relay[i]={at,rc.id,i%6?620+i*7:UINT32_MAX,950,int8_t(-45+i%4)};}
   auto& roles=view.td.mesh.roles;roles.known=true;roles.root=gm.id;roles.root_boot=view.td.role.root_boot;roles.epoch=state==3?UINT32_MAX:3093432306u;roles.sequence=state==3?UINT32_MAX:287;
   roles.received_us=view.td.now_us-200000;roles.until_us=roles.received_us+2000000;roles.created_us=state==3?UINT64_MAX-1000000:UINT64_C(7)*86400000000;roles.epoch_since_us=1000000;
   view.td.mesh.selected={roles.root,roles.root_boot,roles.epoch};
   auto& ref=view.td.mesh.reference;ref.reported=true;ref.scope=view.td.mesh.selected;ref.received_us=roles.received_us;ref.until_us=ref.received_us+30000000;ref.wire_version=2;ref.fields=63;ref.values=7;ref.sats=state==3?99:12;ref.hdop_centi=state==3?65534:75;ref.clock_class=state==3?255:6;ref.priority1=ref.priority2=255;
   roles.tz.value={2,int16_t(state==3?840:-300)};roles.tz.received=state==3?UINT16_MAX:12;roles.tz.until_us=state==2?1:view.td.now_us+600000;
   roles.wire_version=4;roles.birth_utc_us=1792986374188267;roles.founder=gm.id;roles.term=37;
   roles.utc_sample_us=roles.birth_utc_us+(state==3?UINT64_C(10000):1)*86400000000+125000000;roles.utc_received_us=view.td.now_us-1000000;
   ref.wire_version=4;
   if(state==2){ref.until_us=roles.until_us=1;roles.utc_received_us=1;}
   if(state==3){gm.id=gm.root=view.td.target=0x123456789abcdefULL;view.td.role.root=gm.id;view.td.role.hot=0xfedcba987654321ULL;gm.rssi=-127;gm.signal={};gm.signal.observe(gm.rssi,view.td.now_us);}
   if(state==2){rc.stream_slot=UINT8_MAX;view.td.role.available=false;view.td.role.seen_us=1;view.td.request=true;view.td.request_target=rc.id;view.td.pending=true;gm.jitter_ns=UINT32_MAX-1;rc.jitter_ns=UINT32_MAX-1;gm.packets=gm.matched=gm.missed=UINT32_MAX;gm.rssi_min=-127;gm.rssi_max=126;view.radio_error=UINT32_MAX;}
  }
  uint8_t frame[sticks3::FrameBytes]{},strip[sticks3::StripBytes]{};sticks3::RenderAudit audit;sticks3::render(frame,view,&audit);if(audit.clipped_pixels||audit.text_overflow)std::fprintf(stderr,"UI state=%u metric=%u page=%u clipped=%u overflow=%u\n",state,metric,page,audit.clipped_pixels,audit.text_overflow);assert(!audit.clipped_pixels&&!audit.text_overflow);
  for(unsigned row=0;row<sticks3::Height;row+=sticks3::StripRows){sticks3::render_strip(strip,row,view);assert(!std::memcmp(strip,frame+row*sticks3::Width*2,sticks3::StripBytes));}
  if(argc>1&&(state==1||page>=6)){std::ofstream out(std::string(argv[1])+"/screen-"+std::to_string(page)+"-"+std::to_string(metric)+(state==1?"":"-state"+std::to_string(state))+".ppm",std::ios::binary);out<<"P6\n135 240\n255\n";for(unsigned i=0;i<sticks3::FrameBytes;i+=2){unsigned c=frame[i]*256+frame[i+1];char rgb[]={char((c>>11)*255/31),char(((c>>5)&63)*255/63),char((c&31)*255/31)};out.write(rgb,3);}}
 }
 {TdModel h;h.seen(1,1,false,0,true,true,-60,1,1000000);h.source(1,4,1000000000,2000000000,1200000000,1000000);
  assert(h.peer(1).time_valid&&h.peer(1).origin_age_us==200000&&h.peer(1).source_ttl_us==800000);
  h.view.now_us=1100000;h.view.role.known=h.view.role.available=true;h.view.role.hot=2;
  h.view.gm[23]={1000000,1,200,990,-60};auto x=sticks3::TdHealth::evaluate(h.view);assert(x.level==sticks3::TdHealth::Good&&!std::strcmp(x.status,"GOOD HEALTH"));
  h.view.role.hot=0;assert(!std::strcmp(sticks3::TdHealth::evaluate(h.view).status,"NO HOT STANDBY"));
  h.view.gm[23].delivery=850;assert(!std::strcmp(sticks3::TdHealth::evaluate(h.view).status,"NO HOT STANDBY"));
  h.view.gm[23].delivery=849;assert(!std::strcmp(sticks3::TdHealth::evaluate(h.view).status,"LINK LOSS"));
  h.view.gm[23].delivery=800;assert(!std::strcmp(sticks3::TdHealth::evaluate(h.view).status,"LINK LOSS"));
  h.source(1,4,1000000000,1200000000,1200000000,1100000);assert(!std::strcmp(sticks3::TdHealth::evaluate(h.view).status,"TIME INVALID"));
  assert(!std::strcmp(sticks3::TdHealth::evaluate(h.view,1).status,"RADIO FAULT"));h.view.owner_stale=true;assert(!std::strcmp(sticks3::TdHealth::evaluate(h.view,1).status,"OWNER STALE"));
 }
 for(unsigned reason=TdSnapshot::Role::Waiting;reason<=TdSnapshot::Role::Report;reason++){
  sticks3::View view;view.td.enabled=true;view.page=0;view.td.role.result=TdSnapshot::Role::Result(reason);
  uint8_t frame[sticks3::FrameBytes]{};sticks3::RenderAudit audit;sticks3::render(frame,view,&audit);
  assert(!audit.clipped_pixels&&!audit.text_overflow);
 }
 for(unsigned expired=0;expired<2;++expired){
  sticks3::View v;v.native_dashboard=true;v.time_valid=true;v.native_rms_ns=123;v.hold_ms=1200;
  std::snprintf(v.role,sizeof v.role,"FOLLOW");std::snprintf(v.upstream,sizeof v.upstream,"SYNTHETIC");
  int64_t shown=0;const auto zone=v12::clock_zone({2,-300},1750000,expired?1750000:1749999,1792986374000000,shown);
  std::snprintf(v.zone,sizeof v.zone,"%s",zone);v.utc=uint32_t(shown/1000000);
  uint8_t frame[sticks3::FrameBytes]{};sticks3::RenderAudit audit;sticks3::render(frame,v,&audit);assert(!audit.clipped_pixels&&!audit.text_overflow);
  if(argc>1){std::ofstream out(std::string(argv[1])+(expired?"/native-rc-utc.ppm":"/native-rc-local.ppm"),std::ios::binary);out<<"P6\n135 240\n255\n";
   for(unsigned i=0;i<sticks3::FrameBytes;i+=2){unsigned c=frame[i]*256+frame[i+1];char rgb[]={char((c>>11)*255/31),char(((c>>5)&63)*255/63),char((c&31)*255/31)};out.write(rgb,3);}}
 }
 puts("PASS passive model event wrap/gaps/consume-once/scope fences/8-row cap;108 real UI cases and strip equivalence");
}
