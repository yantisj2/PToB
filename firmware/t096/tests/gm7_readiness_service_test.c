/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Real service preparation and ordinary wire nomination; fake hardware only.
 * The fitter is not simulated: its exported map/fit/phase inputs are explicit. */
#define main historical_service_main
#include "gm7_service_test.c"
#undef main
static struct gm7_owner leader;
static void deliver(struct gm7_owner *to,const struct gm7_packet *p){
 uint8_t ad[GM7_MAX_AD_BYTES];assert(!gm7_ad_encode(ad,sizeof ad,p,&group));
 assert(!gm7_owner_receive(to,ad,gm7_wire_size(p->kind)+4,NULL,fake_hf,fake_hf));
}
static void follower(void){
 setup(false);healthy_input();
 assert(!gm7_owner_init(&leader,&group,501,777,true,0));
 struct gm7_preparation empty={0};
 for(uint64_t t=0;t<=fake_hf;t+=1000000){struct gm7_source s=source_at(t);gm7_owner_update(&leader,&s,&empty,t);}
 leader.quiet_since=fake_hf-GM7_QUIET_US-500000;
 struct gm7_source s=source_at(fake_hf);gm7_owner_update(&leader,&s,&empty,fake_hf);
 assert(!gm7_owner_activate(&leader,fake_hf,fake_hf+4000,fake_hf));
 struct gm7_packet ann;assert(!gm7_owner_control(&leader,&ann,fake_hf));deliver(&owner,&ann);
 gm7_owner_update(&owner,&s,&empty,fake_hf);
 fake_timing=(struct r2_gm7_timing){.scope={.root_identity=leader.identity,.boot=leader.boot,
   .source_epoch=leader.scope.timing_epoch,.publication_epoch=leader.scope.train},
   .capture_gen=leader.scope.capture_gen,.counter_epoch=4,.shadow_gen=9,
   .last_correction=(fake_hf-500000)*16,.retained=true,.phase_enclosure_us=1000,
   .utc_offset_ns=INT64_C(1792800000000000000),
   .map={.delivery=fake_hf*16,.anchor=(int64_t)fake_hf*1000,.valid=true},
   .view={.map_valid=true,.fit_valid=true,.locked=false,.rms_valid=true,.rms_ns=3058}};
}
static struct gm7_preparation evaluate(struct gm7_source s){
 struct gm7_bridge b;assert(bridge(&b));struct gm7_preparation p;
 preparation(&p,b,&fake_timing);gm7_owner_update(&owner,&s,&p,fake_hf);return p;
}
int main(void){
 follower();struct gm7_preparation p=evaluate(source_at(fake_hf));
 assert(p.map_valid&&p.fit_valid&&!p.locked&&p.phase_available&&owner.prepared&&!owner.armed);
 assert(!(owner.blockers&GM7_B_LOCK)&&!fake_timing.view.locked);
 struct gm7_packet status;assert(!gm7_owner_control(&owner,&status,fake_hf));
 assert(status.kind==GM7_STATUS&&(status.flags&GM7_SHADOW)&&!(status.flags&GM7_HOT));
 deliver(&leader,&status);struct gm7_preparation empty={0};struct gm7_source s=source_at(fake_hf);
 gm7_owner_update(&leader,&s,&empty,fake_hf);
 assert(leader.nominee==identity&&leader.nominee_boot==boot&&leader.serving&&!owner.serving);
 struct gm7_packet ann;assert(!gm7_owner_control(&leader,&ann,fake_hf));deliver(&owner,&ann);
 p=evaluate(s);assert(owner.armed&&!owner.serving&&!p.locked);
 assert(!gm7_owner_control(&owner,&status,fake_hf)&&(status.flags&GM7_HOT));deliver(&leader,&status);
 assert(!gm7_owner_request_yield(&leader,fake_hf));
 assert(!fake_timing.view.locked&&fake_timing.view.rms_ns==3058);
 puts("PASS actual service fitted/unlocked3058ns map -> ordinary SHADOW STATUS -> leader nomination -> HOT echo; strict precision label unchanged, no dual output");
 for(unsigned mode=0;mode<13;mode++){
  follower();s=source_at(fake_hf);
  switch(mode){
   case 0:fake_timing.scope.boot++;break;
   case 1:fake_timing.scope.source_epoch++;break;
   case 2:fake_timing.scope.publication_epoch++;break;
   case 3:fake_timing.capture_gen++;break;
   case 4:fake_timing.view.map_valid=false;break;
   case 5:fake_timing.view.fit_valid=false;break;
   case 6:fake_timing.phase_enclosure_us=2001;break;
   case 7:fake_timing.retained=false;break;
   case 8:fake_timing.last_correction=(fake_hf-GM7_ARM_RETAIN_US)*16;break;
   case 9:s.until_us=fake_hf;break;
   case 10:s.gnss_healthy=false;break;
   case 11:s.pps_healthy=false;break;
   case 12:fake_timing.map.valid=false;break;
  }
  p=evaluate(s);assert(!owner.prepared&&!owner.armed&&!owner.serving);
  assert(!gm7_owner_control(&owner,&status,fake_hf)&&!(status.flags&(GM7_SHADOW|GM7_HOT)));
  deliver(&leader,&status);s=source_at(fake_hf);gm7_owner_update(&leader,&s,&empty,fake_hf);
  assert(!leader.nominee&&!fake_timing.view.locked);
 }
 puts("PASS wrong boot/source/train/capture, missing map/fit, bad/unknown physical phase, stale correction, source expiry and local GNSS/PPS loss cannot advertise SHADOW or earn nomination");
 return 0;
}
