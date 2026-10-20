/* Actual PA owner with cancellation/termination at hostile public boundaries. */
#include "radio_service_fake.h"
#include "../src/radio_audit.c"
#include "../src/radio_service.c"
#include <stdio.h>
#include <stdlib.h>
#include "relay_precision.h"
#include "gm_standby_handoff.h"
#ifdef R2_LORA
static bool lora_needed=true;static unsigned lora_publishes,lora_withdraws;static struct rc8_export lora_export;
bool r2_lora_source_needed(void){return lora_needed;}
void r2_lora_source_publish(const struct rc8_export *s){lora_export=*s;++lora_publishes;}
void r2_lora_source_withdraw(void){++lora_withdraws;}
void r2_lora_quality_publish(const struct rc8_scope *s,const struct rc8_quality *q){(void)s;assert(!(q->values&~q->known));}
#endif
static struct bt_le_per_adv_sync fixture;
static bool owned,armed;
static unsigned mode,deletes;
static bool retain_native_owner;
static void end(void){struct bt_le_per_adv_sync_term_info i={12,&target};owned=false;terminated(&fixture,&i);}
static int bt_le_per_adv_sync_create(const struct bt_le_per_adv_sync_param *p,struct bt_le_per_adv_sync **out){
 owned=true;*out=&fixture;
 if(mode==1)end();
 return 0;
}
static int bt_le_per_adv_sync_delete(struct bt_le_per_adv_sync *object){
 assert(object==&fixture);++deletes;
 if(retain_native_owner)return 0; /* Only the real term callback releases it. */
 if(mode==2&&deletes==1)return 0; /* cancel remains pending */
 if(mode==3){end();return -EINVAL;} /* natural termination wins */
 if(mode==4)return -EIO; /* controller still owns it */
 end();return 0;
}
static struct bt_le_per_adv_sync *bt_le_per_adv_sync_lookup_addr(const bt_addr_le_t *a,uint8_t sid){return owned?&fixture:NULL;}
static unsigned bootstrap_requests,refresh_requests,timing_lamp_events;
static bool timing_lamp_tx,timing_lamp_corrections;
void r2_ble_timing_lamps(bool tx,bool corrections,uint64_t at){
 ++timing_lamp_events;timing_lamp_tx=tx;timing_lamp_corrections=corrections;assert(at);
}
void r2_group_admission_refresh_anchor(void){++refresh_requests;}
void r2_group_admission_require_anchor(void){++bootstrap_requests;}
void r2_ble_precision_discovery(bool enabled){}
static bool scan_drain;
void r2_ble_precision_draining(bool enabled){scan_drain=enabled;}
void r2_ble_sync_acquiring(bool enabled){}
bool r2_ble_initiating(void){return mode==6;}
bool r2_ble_admission_due(uint64_t now){return mode==14;}
bool r2_ble_admission_reserves_scan(uint64_t now){return mode==14&&now%1000000<200000;}
static uint64_t capture_tick=16000000,capture_us=1000000;
bool r2_capture_now(struct r2_capture *out){*out=(struct r2_capture){capture_tick,1,R2_CAPTURE_NOW};return true;}
bool r2_capture_now_at(struct r2_capture *out,uint64_t *at){bool valid=r2_capture_now(out);if(at)*at=capture_us;return valid;}
bool r2_ble_client_align(uint64_t now,uint64_t utc_us){return true;}
bool r2_ble_client_window_open(uint64_t now,uint32_t window){return now%1000000<window;}
uint64_t r2_ble_client_window_start(uint64_t now){return now-now%1000000;}
bool r2_capture_radio(uint32_t raw,uint32_t epoch,struct r2_capture *out){*out=(struct r2_capture){raw,epoch,R2_CAPTURE_RADIO};return true;}
static bool queued;static struct r2_radio_record packet;
bool r2_radio_capture_pop(struct r2_radio_record *out){if(mode==19)assert(pt_lock.held);if(!queued)return false;*out=packet;queued=false;return true;}
void r2_radio_capture_stats(struct r2_radio_capture_stats *out){*out=(struct r2_radio_capture_stats){.armed=armed};}
int r2_radio_capture_flush(void){if(armed)return -EBUSY;int n=queued;queued=false;return n;}
static unsigned handoff_withdraw_probes;
static void handoff_during_withdraw(void){assert(!r2_radio_service_handoff_ready(111,100,4));++handoff_withdraw_probes;}
static void handoff_fixture(const struct r2_radio_precision *history){
 error=0;armed=native_owned=queued=false;r2_radio_service_activate();precision=*history;
 r2_radio_service_handoff_request(111,100,4);assert(!r2_radio_service_handoff_ready(111,100,4));
 r2_radio_service_stop(64000000);bool running=true;handoff_withdraw_probes=0;shadow_withdraw_probe=handoff_during_withdraw;precision_poll(64000001,&running);shadow_withdraw_probe=NULL;
 assert(handoff_withdraw_probes==1);
 assert(r2_radio_service_handoff_ready(111,100,4)&&r2_radio_service_closed());
 assert(!r2_radio_service_handoff_ready(112,100,4));
}
int main(int argc,char **argv){
 assert(argc==2||argc==3);mode=(unsigned)strtoul(argv[1],NULL,10);
 struct r2_enrollment roster={.count=2};assert(!r2_radio_service_init(0,&roster));
 if(mode>=35&&mode<=49){
  assert(argc==3);struct gm5_handoff_fixture f;FILE *file=fopen(argv[2],"rb");assert(file&&fread(&f,sizeof f,1,file)==1&&!fclose(file));
  roster=(struct r2_enrollment){.count=2,.identity={333,111},.group={.id=7,.epoch=1,.psk={1}}};
  struct r2_group_client c=f.promoted;struct r2_radio_scope grant=f.selected;
  assert(!c.passive&&c.source&&c.at==f.accepted.at&&c.until==f.accepted.until&&grant.root==1&&grant.boot==11&&!grant.gm&&!grant.follower);
  fake_time=capture_us=f.at+10000;capture_tick=capture_us*16;pt_input.stopped=false;struct r2_source_clock source={0};bool running=false;
  if(mode==36||mode>=45){
   struct r2_group_client first=f.accepted;first.at--;if(mode==46||mode==47)first.root++;
   r2_radio_service_client(&first,&roster,fake_time);r2_radio_service_tick(grant,&source,0,fake_time);precision_poll(fake_time,&running);assert(public_client&&pt_output.acquisition);
  }
  if(mode==37)c.root++;
  if(mode==38)c.boot++;
  if(mode==39)grant.source_epoch=c.source_epoch+1;
  if(mode==40){grant.root=0;grant.boot=33;}
  if(mode==41)fake_time=capture_us=r2_group_anchor_deadline(c.at,c.uncertainty_us);
  if(mode==42){grant.follower=true;grant.source_epoch=c.source_epoch;grant.publication_epoch=c.publication;grant.until_us=f.at+1000000;}
  if(mode==43)c.until=fake_time-1; /* Replay anchor can acquire, never output on expired source. */
  if(mode==44)grant.publication_epoch=c.publication+1;
  if(mode==45||mode==47)c.root++;
  if(mode==48)c.source=false;
  if(mode==49){grant.source_epoch=c.source_epoch;c.source_epoch++;}
  r2_radio_service_client(&c,&roster,fake_time);r2_radio_service_tick(grant,&source,0,fake_time);precision_poll(fake_time,&running);
  if((mode>=37&&mode<=41)||mode==44||mode==47){assert(!precision_needed(fake_time)&&!pt_output.scope.follower&&!pt_output.acquisition);}
  else if(mode==42){assert(!public_client&&pt_output.scope.follower&&!pt_output.acquisition&&same(pt_output.scope,grant));}
  else{
   uint64_t expected_at=c.at-((mode==45||mode==48||mode==49)?1:0);
   assert(public_client&&pt_output.scope.follower&&pt_output.acquisition&&client_clock.at==expected_at&&client_clock.until==c.until&&client_clock.root==111&&client_clock.boot==11&&client_clock.source_epoch==1);
   for(unsigned pass=0;pass<25&&!sync_object;pass++){
    r2_radio_service_tick(grant,&source,0,fake_time);struct bt_le_scan_recv_info info={.sid=12,.interval=100,.addr=&target};scan_received(&info,NULL);
    r2_radio_service_tick(grant,&source,0,fake_time);precision_poll(fake_time,&running);
    if(!sync_object){fake_time+=10000;capture_us=fake_time;capture_tick=capture_us*16;}
   }
   assert(sync_object&&owned);
  }
  assert(!pt_output.view.map_valid&&!pt_output.view.locked&&!pt_output.utc_valid&&!error);
  if(mode==43)assert(!pt_output.bootstrap.source&&!pt_output.view.authorized&&pt_output.scope.until_us==c.until);
  printf("PASS actual producer/PT mailbox mode%u: selected scope, delayed/early PT, original anchor/source expiry and mismatch/own-ACTIVE fences; no precision/output invented\n",mode);return 0;
 }
 if(mode>=30&&mode<=34){
  bool stuck=mode==31;roster.identity[0]=1;roster.identity[1]=3;pt_input.roster=roster;
  struct r2_radio_scope grant={.root=stuck?1:0,.root_identity=stuck?3:1,.boot=9,.source_epoch=3,.publication_epoch=1,.until_us=10000000,.gm=!stuck,.follower=stuck};
  pt_input.stopped=false;pt_input.grant=grant;pt_input.source_ready=true;pt_input.source_epoch=3;pt_input.utc=1000000000000LL;pt_input.at=capture_us;
  pt_input.source=(struct r2_source_clock){.epoch=1,.head=1,.count=1,.utc_bound=true,.utc_source_epoch=3,.utc_offset_ns=1000000000000LL};
  pt_input.source.maps[0]=(struct r2_source_map){.from=8000000,.until=32000000,.anchor_ns=1000000000,.generation=11,.rate_q16=16000000*UINT64_C(65536)};
  bool running=false;precision_poll(fake_time,&running);assert(same(pt_output.scope,grant)&&!error);
  static struct bt_le_ext_adv ad;
  if(stuck){sync_object=&fixture;owned=established=retain_native_owner=true;}
  else{assert(pt_output.length);advertiser=&ad;pa_started=ext_started=true;}
  native_owned=native_gm_started=true;native_grant=native_scope=grant;
  /* PT really withdraws while CONTROL is between cycles, then recovers the
   * same scope10ms later. Its original close intent must still be consumed. */
  fake_time+=2000;pt_input.client.passive=true;precision_poll(fake_time,&running);
  uint64_t first=fake_time,until=native_close_until;assert(until==first+2000000&&!pt_output.scope.gm&&!pt_output.scope.follower&&!closing);
  fake_time+=10000;pt_input.client.passive=false;precision_poll(fake_time,&running);
  assert(same(pt_output.scope,grant)&&native_close_until==until&&!closing&&!error);
  if(mode==32||mode==33)fake_time=until+(mode==33);
  if(mode==34){fake_time=until-10000;adv_close_delay=20000;}
  r2_radio_service_tick(grant,&pt_input.source,pt_input.utc,fake_time);
  if(mode>=32){
   assert(!closing&&!native_owned&&!native_close_until&&!advertiser&&r2_radio_service_closed());
   assert(fake_time>=until&&r2_radio_service_error()==-ETIMEDOUT);
   close_poll(fake_time+1);assert(r2_radio_service_error()==-ETIMEDOUT);
   puts(mode==34?"PASS HCI close crosses original PT deadline: fresh post-call observation retains timeout despite released resources":
                  "PASS first CONTROL consumption at/after original PT deadline: synchronous release cannot evade sticky timeout");return 0;
  }
  if(!stuck){
   assert(!closing&&!native_owned&&!native_close_until&&!advertiser&&!native_gm_started&&r2_radio_service_closed());
   fake_time=until;assert(!r2_radio_service_supervise(fake_time));
   puts("PASS actual PT10ms withdrawal/recovery: CONTROL consumes original intent, closes GM resources, no stranded2s timeout or silent scope reuse");return 0;
  }
  assert(closing&&native_owned&&native_close_until==until&&close_until==until&&sync_object==&fixture&&owned&&deletes==1);
  for(unsigned pass=0;pass<3;pass++){
   fake_time=first+500000+pass*500000;begin_close(fake_time);r2_radio_service_tick(grant,&pt_input.source,pt_input.utc,fake_time);
   assert(closing&&native_owned&&native_close_until==until&&close_until==until&&sync_object==&fixture&&owned&&deletes==1&&!error);
  }
  fake_time=until-1;assert(!r2_radio_service_supervise(fake_time));
  fake_time=until;assert(r2_radio_service_supervise(fake_time)==-ETIMEDOUT);
  assert(native_owned&&sync_object==&fixture&&owned&&!r2_radio_service_closed());
  retain_native_owner=false;end();r2_radio_service_stop(fake_time+1);
  assert(r2_radio_service_closed()&&!native_owned&&!native_close_until&&!sync_object&&!owned&&r2_radio_service_error()==-ETIMEDOUT);
  puts("PASS actual retained native callback owner: repeated closes preserve first2s deadline, no early reuse, exact timeout stays sticky after real callback drain");return 0;
 }
#ifdef R2_LORA
 if(mode>=23&&mode<=29){
  bool gm=mode==23||mode==28;
  if(!gm){capture_us=64000000;capture_tick=64*R2_CAPTURE_HZ;}
  roster.identity[0]=1;roster.identity[1]=3;enrollment=roster;pt_input.roster=roster;
  struct r2_radio_scope grant={.root=gm?0:1,.root_identity=gm?1:3,.boot=9,.source_epoch=3,.publication_epoch=1,.until_us=capture_us+1000000,.gm=gm,.follower=!gm};
  pt_input.stopped=false;pt_input.grant=grant;pt_input.source_ready=mode==23||mode==25||mode==29;pt_input.source_epoch=3;pt_input.utc=1000000000000LL;pt_input.at=capture_us;
  if(pt_input.source_ready){
   pt_input.source=(struct r2_source_clock){.epoch=1,.head=1,.count=1,.utc_bound=true,.utc_source_epoch=3,.utc_offset_ns=1000000000000LL};
   pt_input.source.maps[0]=(struct r2_source_map){.from=8000000,.until=32000000,.anchor_ns=1000000000,.generation=11,.rate_q16=16000000*UINT64_C(65536)};
  }
  if(mode==29){
   /* A local qualified-source snapshot is an input prerequisite only; this
    * fixture cannot assert actual HOT reservations or hardware qualification. */
   pt_input.source.maps[0].from=capture_tick-8000000;
   pt_input.source.maps[0].until=capture_tick+16000000;
   int64_t own_utc;assert(r2_source_utc_read(&pt_input.source,(struct r2_capture){capture_tick,1,R2_CAPTURE_NOW},3,&own_utc));
   assert(own_utc!=1002062500000LL);
  }
  if(!gm){
   active_scope=grant;precision.scope=grant;precision.local_epoch=1;precision.train=1;precision.last_available=capture_tick-100000;precision.last_accepted=precision.last_available;precision.accepted=12;
   precision.origin_created_ns=1900000000;precision.origin_expires_ns=3000000000;
   precision.servo.output=(r2_servo_map_t){capture_tick-1000000,2000000000,200,123,mode!=25};follower_utc_offset=1000000000000LL;
   precision.servo.fit_valid=true;precision.servo.quality_count=precision.servo.output_quality.count=32;
   precision.servo.streak=precision.servo.output_quality.streak=64;
   precision.servo.last_local=precision.servo.output_quality.last_local=63*R2_CAPTURE_HZ;
   for(unsigned i=0;i<32;i++){
    precision.servo.quality_ticks[i]=precision.servo.output_quality.ticks[i]=(32+i)*R2_CAPTURE_HZ;
    precision.servo.quality_predictions[i]=precision.servo.output_quality.predictions[i]=1;
   }
   precision.servo.pll.state=mode==27?R2_SERVO_PLL_TRACKING:R2_SERVO_PLL_LOCKED;
   precision.servo.pll.last_update=precision.servo.output.delivery;
   if(mode==24){assert(!pt_input.source_ready&&!pt_input.source.count);assert(r2_servo_pll_locked(&precision.servo,capture_tick));}
  }
  if(mode==26)lora_needed=false;
  precision_step(capture_us);
  if(mode>=25&&mode!=29)assert(!lora_publishes&&lora_withdraws);
  else{assert(lora_publishes==1&&lora_export.valid&&lora_export.scope.root==grant.root_identity&&lora_export.scope.root_boot==9);
   assert(lora_export.scope.kind==(gm?RC8_OWN_GM:RC8_SELECTED_BLE));
   if(mode==24||mode==29){
    r2_servo_map_t expected;int64_t exact;assert(r2_radio_precision_map(&precision,&expected));
    assert(lora_export.anchor_ns==(uint64_t)expected.anchor+1000000000000ULL&&lora_export.phase_ns==expected.phase&&lora_export.rate_ppb==expected.rate);
    assert(lora_export.created_ns==1001900000000ULL&&lora_export.expires_ns==1003000000000ULL&&lora_export.anchor_tick==expected.delivery&&lora_export.map_generation==12);
    struct rc8_correction mapped;assert(rc8_export_event(&mapped,&lora_export,1,(struct rc8_event){.tick=capture_tick,.capture_epoch=1,.kind=RC8_EVENT_TX_DONE}));
    assert(r2_servo_mapped(&expected,capture_tick,&exact)&&mapped.event_ns==(uint64_t)exact+1000000000000ULL);
    if(mode==29){int64_t own_utc;assert(r2_source_utc_read(&pt_input.source,(struct r2_capture){capture_tick,1,R2_CAPTURE_NOW},3,&own_utc));assert(mapped.event_ns!=(uint64_t)own_utc);}
    unsigned before=lora_withdraws;struct r2_source_clock no_pps={0};
    r2_radio_service_source(&no_pps,false,3);assert(lora_withdraws==before);
    precision_step(capture_us);assert(lora_publishes==2&&lora_withdraws==before&&lora_export.scope.kind==RC8_SELECTED_BLE);
    assert(lora_export.anchor_ns==(uint64_t)expected.anchor+1000000000000ULL);
   }
  }
  puts("PASS actual PT export: own GM PPS /locked BLE without local PPS /invalid or unlocked refusal /disabled guard");return 0;
 }

#endif
 if(mode==22){
  static struct bt_le_ext_adv ad;advertiser=&ad;
  struct r2_group_config g={.psk={1},.id=99,.epoch=1};
  struct r2_role_record r={.sender=1,.root=1,.sender_boot=10,.root_boot=10,.hot=3,.hot_boot=20,.coordinator=3,.created_us=1000000,.source_epoch=4,.valid_us=2000000,.flags=3,.members=3};
  pt_input.stopped=false;pt_input.source_ready=true;pt_input.source_epoch=4;
  pt_output=(struct pt_output){.sampled=1000000,.scope={.gm=true,.root_identity=1,.root=0,.boot=10,.source_epoch=4,.until_us=9000000},.payload_until=9000000};
  struct pt_output out=pt_output;r2_radio_service_role(&r,&g);install_role_status(&out,fake_time);
  struct r2_role_record decoded;assert(role_ad_updates==1&&!r2_role_decode(&decoded,role_ad_wire,sizeof role_ad_wire,&g)&&decoded.hot==3&&decoded.sequence==1);
  install_role_status(&out,fake_time+999999);assert(role_ad_updates==1);
  fake_time+=1000000;r.created_us=fake_time;r2_radio_service_role(&r,&g);role_ad_error=-EIO;install_role_status(&out,fake_time);
  assert(role_ad_updates==2&&role_update_errors==1&&!error&&!closing); // Optional failure preserves timing.
  uint32_t diag[6];r2_radio_service_gm_diagnostics(diag);assert(diag[0]==2&&diag[1]==1&&!diag[5]);
  fake_time+=1000000;role_ad_error=0;r.created_us=fake_time;r2_radio_service_role(&r,&g);pt_input.source_ready=false;install_role_status(&out,fake_time);assert(role_ad_updates==2);
  pt_input.source_ready=true;r.root=r.sender=7;r2_radio_service_role(&r,&g);install_role_status(&out,fake_time);assert(role_ad_updates==2);
  fake_time+=1000000;r.root=r.sender=1;r.created_us=fake_time-2000000;r2_radio_service_role(&r,&g);install_role_status(&out,fake_time);assert(role_ad_updates==2);
  fake_time+=1000000;r.created_us=fake_time;r2_radio_service_role(&r,&g);install_role_status(&out,fake_time);assert(role_ad_updates==3&&!r2_role_decode(&decoded,role_ad_wire,sizeof role_ad_wire,&g)&&decoded.sequence==3);
  pt_input.stopped=true;fake_time+=1000000;r.created_us=fake_time;r2_radio_service_role(&r,&g);install_role_status(&out,fake_time);assert(role_ad_updates==3&&!error);
  pa_started=ext_started=true;gm_radio_publish();r2_radio_service_gm_diagnostics(diag);assert(diag[2]&&diag[3]&&diag[4]);
  r2_radio_service_stop(fake_time+1);assert(r2_radio_service_closed());r2_radio_service_gm_diagnostics(diag);
  assert(!diag[2]&&!diag[3]&&!diag[4]); /* Direct close publishes without another tick. */
  puts("PASS existing GM advertiser role update1s signed bytes;optional API failure nonfatal;source/root/expiry/stop fences");return 0;
 }
 if(mode==21){
  /* Actual decoded/captured receive path, independent of servo/map readiness. */
  struct r2_radio_scope grant={.root=1,.boot=9,.source_epoch=3,.publication_epoch=1,.follower=true,.until_us=1500000};
  pt_input.stopped=false;pt_input.grant=grant;pt_input.utc=1000000000;
  struct r2_precision_packet p={.root=1,.boot=9,.source_epoch=3,.train=1,.publication=1,.capture_epoch=1,.flags=1,.created_ns=1000000000,.expires_ns=2000000000};
  packet=(struct r2_radio_record){.raw=15999900,.epoch=1,.length=R2_PRECISION_AD_BYTES,.event=1,.channel=1};
  assert(!r2_precision_encode(packet.ad,&p,roster.key[1]));queued=true;
  precision_step(1000010);assert(timing_lamp_events==1&&!timing_lamp_tx&&!timing_lamp_corrections);
  p.boot=8;assert(!r2_precision_encode(packet.ad,&p,roster.key[1]));queued=true;
  precision_step(1000020);assert(timing_lamp_events==1);
  p.boot=9;p.count=1;p.correction[0]=(struct r2_precise_correction){.publication=1,.event=1,.channel=1,.ticks=16000000,.generation=1,.expires_ns=2000000000};
  assert(!r2_precision_encode(packet.ad,&p,roster.key[1]));queued=true;
  precision_step(1000030);assert(timing_lamp_events==2&&timing_lamp_corrections&&!timing_lamp_tx);
  packet.ad[20]^=1;queued=true;precision_step(1000040);assert(timing_lamp_events==2);
  puts("PASS actual captured original/correction lamps; stale boot/bad authentication refuse activity");return 0;
 }
 if(mode==20){
  /* CONTROL can read the last PT snapshot after PT becomes idle at anchor
   * expiry. A stale acquisition bit must not reserve PA RX forever. */
  pt_input.stopped=false;
  pt_output=(struct pt_output){.sampled=1000000,.acquisition=true,
   .scope={.follower=true,.boot=9,.root=1,.source_epoch=3,.until_us=2000000},
   .bootstrap={.source=true,.passive=true,.at=1000000,.uncertainty_us=40000},
   .context={.passive=true}};
  uint64_t expiry=r2_group_anchor_deadline(1000000,40000);
  struct pt_output out;
  fake_time=expiry-1;output_read(&out,fake_time);
  assert(out.acquisition&&out.scope.follower&&!out.view.authorized);
  fake_time=expiry;output_read(&out,fake_time);
  assert(!out.acquisition&&!out.scope.follower&&!out.view.authorized);
  fake_time=expiry+1000000;
  r2_radio_service_tick((struct r2_radio_scope){0},&(struct r2_source_clock){0},0,fake_time);
  assert(!owned&&!sync_object&&!creating&&!discovering);
  pt_output.bootstrap.at=fake_time;pt_output.scope.until_us=fake_time+1000000;
  r2_radio_service_continuous(true);
  r2_radio_service_tick((struct r2_radio_scope){0},&(struct r2_source_clock){0},0,fake_time);
  assert(owned&&sync_object&&!pt_output.view.authorized);
  r2_radio_service_stop(fake_time+1);assert(r2_radio_service_closed());
  puts("PASS stale PT acquisition expires at its original coarse-anchor deadline and cannot starve fresh JOIN recovery");return 0;
 }
 if(mode==18){
  error=-ENOBUFS;armed=true;begin_close(1000000);close_poll(1000001);
  assert(!r2_radio_service_closed()&&r2_radio_service_error()==-ENOBUFS&&close_until==3000000);
  armed=false;close_poll(1002000);assert(r2_radio_service_closed()&&r2_radio_service_error()==-ENOBUFS);
  puts("PASS fault retained independently of real controller/capture closure; no false closure while armed, original2s drain deadline");return 0;
 }
 if(mode==19){
  bool running=false;pt_input.stopped=true;native_owned=true;queued=true;
  precision_poll(1000000,&running);assert(!queued&&idle_capture_discards==1&&!running);
  native_owned=false;queued=true;precision_poll(1002000,&running);assert(queued&&idle_capture_discards==1);
  puts("PASS no-map owned capture drain continues without discipline; foreign relay owner records untouched");return 0;
 }
 if(mode==17){
  armed=true;begin_close(1000000);assert(scan_drain&&close_until==3000000);
  close_poll(1000001);assert(scan_drain&&closing&&!r2_radio_service_closed());
  armed=false;close_poll(1002000);assert(!scan_drain&&!closing&&r2_radio_service_closed()&&!r2_radio_service_error());
  puts("PASS PA drain pauses discovery until actual unarmed capture and closed objects; original2s deadline unchanged, scan restored only after closure");return 0;
 }
 if(mode==16){
  pt_input.stopped=true;native_owned=false;queued=true;pt_output.utc_valid=true;precision.accepted=99;client_clock.source=true;
  assert(r2_radio_service_reset(1)==-EAGAIN);bool was_running=false;precision_poll(1000000,&was_running);
  assert(!r2_radio_service_reset(1)&&!queued&&!pt_output.utc_valid&&!precision.accepted&&!client_clock.source&&reset_flushes==1);
  armed=true;assert(r2_radio_service_reset(2)==-EBUSY);armed=false;
  puts("PASS actual PT owner reset: drains before acknowledgement, purges old physical capture records, clears cached maps/source context, preserves flush evidence, armed resource rejected");return 0;
 }
 if(mode==14){
  public_client=true;client_clock=(struct r2_group_client){.source=true,.at=1000000,.uncertainty_us=40000,.utc_us=1791500000000000ULL};
  client_scope=(struct r2_radio_scope){.boot=9,.source_epoch=3,.publication_epoch=1,.root=1,.follower=true,.until_us=3000000};
  struct r2_source_clock empty={0};r2_radio_service_tick((struct r2_radio_scope){0},&empty,0,1000000);
  assert(!owned&&!sync_object&&!opportunity_seen);
  r2_radio_service_tick((struct r2_radio_scope){0},&empty,0,1200000);assert(owned&&opportunity_seen);
  r2_radio_service_tick((struct r2_radio_scope){0},&empty,0,1400000);assert(!owned);
  uint32_t accepted;assert(r2_radio_service_direct_opportunity(&accepted)&&!accepted);
  assert(!r2_radio_service_direct_opportunity(&accepted));
  puts("Public admission runs before sync acquisition inside unchanged window; only actual acquisition counts as missed opportunity PASS");return 0;
 }
 if(mode==13||mode==15||mode==20){
  enrollment.group.id=1;enrollment.group.epoch=1;enrollment.group.psk[0]=1;
  enrollment.identity[0]=111;enrollment.identity[1]=99999;
  struct r2_relay_precision receiver;r2_relay_precision_init(&receiver,1,0,1,101,1);receiver.v14=true;receiver.passive_receiver=mode==15;
  struct r2_radio_precision direct={0};
  const int64_t offset=1791500000000000000LL;
  struct r2_radio_scope scope={.boot=100,.source_epoch=4,.publication_epoch=1,.root=0,.gm=true,.until_us=100000000};
  r2_radio_precision_scope(&precision,scope,1);
  for(unsigned i=0;i<1000;i++){
   uint64_t ns=1000000000ULL+(uint64_t)i*62500000,tick=ns*2/125;
   struct r2_precision_packet p={.root=0,.boot=100,.source_epoch=4,.train=1,.publication=i+1,.capture_epoch=1,.flags=1,
    .created_ns=ns-1000000,.expires_ns=ns+1000000000,.utc_offset_ns=offset,.count=precision.sent_count};
   memcpy(p.correction,precision.sent,sizeof p.correction);
   uint8_t ad[190];assert(!generic_encode(ad,&p));
   struct r2_relay_packet packet;assert(!r2_group_relay_decode3(&packet,ad,sizeof ad,&enrollment.group,enrollment.identity));
   struct r2_precision_packet original;assert(!generic_decode(&original,ad,sizeof ad));
   if(!generic_frame)generic_frame=ns;
   assert(r2_radio_precision_tx(&precision,&original,(struct r2_capture){tick,1,R2_CAPTURE_RADIO},i,3,ns,i/16+1,ns+1000000000,ns/1000));
   uint8_t proof[32]={0};memcpy(proof,ad+74,16);
   if(mode==20){
    r2_radio_precision_scope(&direct,(struct r2_radio_scope){.root=0,.root_identity=111,.boot=100,.source_epoch=4,.publication_epoch=1,.until_us=ns/1000+250000,.follower=true},1);
    (void)r2_radio_precision_rx_serving_origin(&direct,&original,(struct r2_capture){tick+144,1,R2_CAPTURE_RADIO},i,3,(struct r2_capture){tick+16000,1,R2_CAPTURE_NOW},ns/1000+1000,offset+ns+1000000);
    (void)r2_radio_precision_step(&direct,(struct r2_capture){tick+16000,1,R2_CAPTURE_NOW},ns/1000+1000);
   }else if(mode!=15||i%16<6)(void)r2_relay_precision_rx(&receiver,&packet,proof,(struct r2_capture){tick+144,1,R2_CAPTURE_RADIO},i,3,
    (struct r2_capture){tick+16000,1,R2_CAPTURE_NOW},ns/1000+1000,offset+ns+1000000);
  }
  printf("Generic source adapter end-to-end accepted=%u rejected=%u map=%u\n",receiver.upstream.accepted,receiver.rejected+receiver.upstream.rejected,receiver.upstream.servo.output.valid);
  /* Stable signed origins retain corrections; passive6/16 opportunities are
   * enough to build a causal map without requiring always-online RX. */
  if(mode!=20)assert(receiver.upstream.accepted>(mode==15?100:400)&&!receiver.rejected&&receiver.upstream.servo.output.valid);
  if(mode==20){
   assert(direct.accepted>400&&direct.servo.fit_valid&&direct.servo.output.valid&&!direct.servo.fault);
   struct r2_enrollment wire_enrollment=enrollment;
   struct r2_radio_precision transmitter=precision,history=direct;
   history.scope.root_identity=111;history.scope.root=2; /* stable GM, different local roster index */
   for(unsigned refusal=0;refusal<8;refusal++){
    struct r2_relay_precision to;r2_relay_precision_init(&to,1,0,1,101,1);handoff_fixture(&history);
    if(refusal==4)to.self=2;if(refusal==5)native_owned=true;if(refusal==6)error=-ENOBUFS;if(refusal==7)armed=true;
    assert(!r2_radio_service_handoff_take(&to,refusal==0?112:111,refusal==1?101:100,refusal==2?5:4,refusal==3?2:1));
    assert(!to.upstream.servo.fit_valid&&!to.origin_valid&&!to.map[0].used);
   }
   r2_relay_precision_init(&receiver,1,0,1,101,1);receiver.v14=true;handoff_fixture(&history);
   assert(r2_radio_service_handoff_take(&receiver,111,100,4,1));
   assert(receiver.upstream.servo.quality_count==history.servo.quality_count&&receiver.upstream.servo.observations==history.servo.observations);
   assert(!receiver.upstream.servo.output.valid&&!receiver.origin_valid&&!receiver.map[0].used&&!receiver.upstream.scope.until_us&&!receiver.upstream.original[0].used&&!receiver.upstream.correction[0].used);
   assert(!r2_radio_service_handoff_take(&receiver,111,100,4,1)); /* single-use PT snapshot */
   unsigned fresh_map_at=0;
   enrollment=wire_enrollment; /* PT's stopped owner withdrew its local roster; source fixture is independent. */
   for(unsigned i=1000;i<1008;i++){
    uint64_t ns=1000000000ULL+(uint64_t)i*62500000,tick=ns*2/125;
    struct r2_precision_packet p={.root=0,.boot=100,.source_epoch=4,.train=1,.publication=i+1,.capture_epoch=1,.flags=1,
     .created_ns=ns-1000000,.expires_ns=ns+1000000000,.utc_offset_ns=offset,.count=transmitter.sent_count};
    memcpy(p.correction,transmitter.sent,sizeof p.correction);uint8_t ad[190];assert(!generic_encode(ad,&p));
    struct r2_relay_packet packet;assert(!r2_group_relay_decode3(&packet,ad,sizeof ad,&enrollment.group,enrollment.identity));
    assert(r2_radio_precision_tx(&transmitter,&p,(struct r2_capture){tick,1,R2_CAPTURE_RADIO},i,3,ns,i/16+1,ns+1000000000,ns/1000));
    uint8_t proof[32]={0};memcpy(proof,ad+74,16);
    (void)r2_relay_precision_rx(&receiver,&packet,proof,(struct r2_capture){tick+144,1,R2_CAPTURE_RADIO},i,3,(struct r2_capture){tick+16000,1,R2_CAPTURE_NOW},ns/1000+1000,offset+ns+1000000);
    if(receiver.map[0].used&&!fresh_map_at)fresh_map_at=i;
   }
   assert(fresh_map_at&&fresh_map_at<1004&&receiver.upstream.accepted>0&&receiver.upstream.servo.observations>history.servo.observations);
   r2_radio_service_activate();assert(!r2_radio_service_handoff_ready(111,100,4));
   puts("PASS PT-owned same-GM captured history handover: eight mismatches/refusals, single-use snapshot, no old permission/map/tuples, fresh corrections restore map without30s reset");
  }
  return 0;
 }
 if(mode==12){
  enrollment.group.id=1;enrollment.group.epoch=1;enrollment.group.psk[0]=1;
  enrollment.identity[0]=111;generic_frame=1000000000;
  struct r2_precision_packet p={.root=0,.boot=100,.source_epoch=4,.train=3,.publication=9,.capture_epoch=1,.flags=1,.created_ns=1000000000,.expires_ns=2000000000};
  uint8_t ad[190];struct r2_relay_packet decoded;
  assert(!generic_encode(ad,&p));assert(!r2_group_relay_decode3(&decoded,ad,sizeof ad,&enrollment.group,enrollment.identity));
  assert(decoded.route.map_generation==3&&!decoded.count&&decoded.origin.expires_ns==2150000000);
  p.count=1;p.correction[0]=(struct r2_precise_correction){8,3,4,16001000,77,2000000000};
  assert(!generic_encode(ad,&p));assert(!r2_group_relay_decode3(&decoded,ad,sizeof ad,&enrollment.group,enrollment.identity));
  assert(decoded.count==1&&decoded.route.map_generation==3&&decoded.correction[0].generation==3);
  p.correction[0].generation=78;p.count=0;
  assert(!generic_encode(ad,&p));assert(!r2_group_relay_decode3(&decoded,ad,sizeof ad,&enrollment.group,enrollment.identity));
  assert(decoded.route.map_generation==3&&!decoded.count&&decoded.origin.expires_ns==2150000000);
  p.created_ns+=1000000;p.expires_ns-=1;
  assert(!generic_encode(ad,&p));assert(!r2_group_relay_decode3(&decoded,ad,sizeof ad,&enrollment.group,enrollment.identity));
  assert(decoded.origin.created_ns==1000000000&&decoded.origin.frame_ns==1000000000&&decoded.origin.expires_ns==2149999999);
  puts("Generic root route generation and physical reference survive narrowed deadline/count-zero publication PASS");return 0;
 }
 if(mode==11){
  public_client=false;enrollment.identity[1]=111;enrollment.local_capabilities=7;
  active_scope=(struct r2_radio_scope){.root=1,.boot=9,.source_epoch=4,.follower=true,.until_us=1000000};
  precision.scope=active_scope;precision.local_epoch=1;precision.last_available=15900000;
  precision.servo.output=(r2_servo_map_t){16000000,1000000000,0,0,true};
  follower_utc_offset=1791500000000000000LL;precision.scope.publication_epoch=123;
  struct r2_group_client context;r2_radio_service_context(&context,1000);
  assert(context.source&&context.root==111&&context.boot==9&&context.source_epoch==4&&context.publication==123);
  assert(context.until==1000000&&context.map_until==51000&&!context.passive);
  active_scope.follower=false;active_scope.gm=true;r2_radio_service_context(&context,1000);assert(!context.source);
  puts("Combined control follower supplies actual map context without passive demotion; serving GM refuses relay-demand context PASS");return 0;
 }
 if(mode==9||mode==10){
  public_client=true;enrollment.group.id=1;enrollment.group.epoch=1;enrollment.group.psk[0]=1;
  enrollment.identity[0]=22;enrollment.identity[1]=11;
  client_clock=(struct r2_group_client){.passive=true,.source=true,.at=1000,.uncertainty_us=40000,.utc_us=1000000};
  client_scope=(struct r2_radio_scope){.boot=9,.source_epoch=3,.publication_epoch=1,.root=1,.follower=true,.until_us=1500000};
  if(mode==10)client_scope.until_us=1500;
  active_scope=client_scope;
  struct r2_precision_packet p={.root=1,.boot=10,.source_epoch=3,.train=1,.publication=1,.capture_epoch=1,.flags=1,.created_ns=1000000000,.expires_ns=2000000000};
  if(mode==10)p.boot=9;
  packet=(struct r2_radio_record){.raw=15999900,.epoch=1,.length=190,.event=1,.channel=1};
  assert(!generic_encode(packet.ad,&p));queued=true;
  struct r2_source_clock empty={0};r2_radio_service_tick((struct r2_radio_scope){0},&empty,0,2000);
  if(mode==10){
   assert(!bootstrap_requests&&client_clock.source&&client_scope.until_us>2000);
   uint64_t expired=client_scope.until_us;
   r2_radio_service_tick((struct r2_radio_scope){0},&empty,0,expired+1499999);
   assert(!bootstrap_requests&&client_clock.source);
   r2_radio_service_tick((struct r2_radio_scope){0},&empty,0,expired+1500000);
   assert(!bootstrap_requests&&refresh_requests==1&&client_clock.source&&client_scope.until_us==expired);
   r2_radio_service_tick((struct r2_radio_scope){0},&empty,0,expired+3000000);assert(!bootstrap_requests&&refresh_requests==1&&client_clock.source);
   puts("Same-scope original renews after quiet; expired permission requests once while retaining bounded coarse context, no grace authority PASS");return 0;
  }
  assert(bootstrap_requests==1&&!client_clock.source&&!client_scope.follower);
  /* Only a new bounded handshake may install either the same root or a new root. */
  struct r2_enrollment provision=enrollment;
  struct r2_group_client c={.passive=true,.source=true,.root=33,.boot=11,.source_epoch=4,.publication=1,.at=3000,.until=1503000,.utc_us=1002000,.uncertainty_us=40000};
  r2_radio_service_client(&c,&provision,3000);assert(enrollment.identity[1]==33&&client_scope.boot==11&&client_clock.source);
  c.at=4000;c.root=33;c.until=1504000;r2_radio_service_client(&c,&provision,4000);
  assert(enrollment.identity[1]==33&&client_scope.until_us==1504000&&bootstrap_requests==1);
  puts("Actual direct owner rejects fresh changed boot, then accepts only new handshake root/same-root bindings PASS");return 0;
 }
 if(mode==8){
  public_client=true;client_clock=(struct r2_group_client){.passive=true,.source=true,.at=1000,.uncertainty_us=75000,.utc_us=1791500000000000ULL};
  client_scope=(struct r2_radio_scope){.boot=9,.source_epoch=3,.publication_epoch=1,.root=1,.follower=true,.until_us=1500};
  struct r2_source_clock empty={0};r2_radio_service_tick((struct r2_radio_scope){0},&empty,0,2000);
  struct r2_group_client state;r2_radio_service_client_state(&state,2000);
  struct r2_radio_precision_view view;r2_radio_service_view(&view,2000);
  assert(!bootstrap_requests&&!state.source&&!view.authorized&&!view.map_valid);
  r2_radio_service_tick((struct r2_radio_scope){0},&empty,0,1501500);
  assert(!bootstrap_requests&&refresh_requests==1&&client_clock.source&&client_scope.until_us==1500);
  r2_radio_service_tick((struct r2_radio_scope){0},&empty,0,1503500);
  assert(!owned&&!sync_object);puts("Expired source permission requests a new causal binding; quiet still closes PASS");return 0;
 }
 if(mode==7){
  public_client=true;client_clock=(struct r2_group_client){.passive=true,.source=true,.at=1000,.uncertainty_us=75000};
  client_scope.until_us=1501000;
  assert(client_anchor_valid(17001000)&&!client_anchor_valid(100001000));
  struct r2_group_client state;r2_radio_service_client_state(&state,17001000);
  uint64_t utc;assert(!state.source&&!r2_radio_service_client_clock(&utc,17001000));
  puts("Coarse anchor survives fit acquisition but never extends original source permission PASS");return 0;
 }
 struct r2_radio_scope scope={.boot=9,.source_epoch=3,.publication_epoch=1,.root=1,.follower=true,.until_us=20000000};
 struct r2_source_clock source={0};
 r2_radio_service_tick(scope,&source,0,1000);
 struct bt_le_scan_recv_info i={12,25,&target};scan_received(&i,NULL);
 assert(!found); /* Retired 32 Hz train cannot enter the 16 Hz service. */
 i.interval=R2_PRECISION_INTERVAL_UNITS;scan_received(&i,NULL);assert(found);
 r2_radio_service_tick(scope,&source,0,2000);
 if(mode==6){assert(!sync_object&&!owned);mode=0;r2_radio_service_tick(scope,&source,0,3000);assert(sync_object&&owned);r2_radio_service_stop(4000);assert(r2_radio_service_closed());puts("PA creation defers to owned connection initiation PASS");return 0;}
 if(mode==1){assert(!sync_object&&!owned);puts("PA create/early-term cannot resurrect object PASS");return 0;}
 assert(sync_object==&fixture&&owned);
 if(mode==0){r2_radio_service_tick(scope,&source,0,2002000);assert(deletes==1&&!owned&&!sync_object);}
 else{
  armed=mode==5;r2_radio_service_stop(3000);
  if(mode==2){assert(owned&&cancelling);struct bt_le_per_adv_sync_synced_info info={12,&target};synced(&fixture,&info);r2_radio_service_stop(4000);assert(deletes==2&&!owned);}
  if(mode==4){assert(owned&&sync_object&&r2_radio_service_error()==-EIO&&!r2_radio_service_closed());return 0;}
  if(mode==5){assert(!r2_radio_service_closed());armed=false;r2_radio_service_stop(4000);}
 }
 assert(r2_radio_service_closed()&&!r2_radio_service_error());
 puts("PA original acquisition/close deadlines, callback races and capture ownership PASS");
}
