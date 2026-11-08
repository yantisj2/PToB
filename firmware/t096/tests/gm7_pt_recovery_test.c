/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual PT/mailbox/precision/codec/native-start bodies; fake HAL/HCI only. */
#include "gm7_pt_radio_fixture.inc"
#include "gm7_bridge.h"
static struct r2_enrollment path;
static struct r2_source_clock local;
static struct r2_radio_scope grant;
static struct r2_group_client anchor;
static bool running;
static bool replay_record;
static bool replay_query;
static uint32_t replay_hash;
static unsigned optin_unlocks;
static void setup_pt(void){
 path=(struct r2_enrollment){.count=2,.identity={17,99},.group={.id=7,.epoch=1,.psk={1}}};
 assert(!r2_radio_service_init(0,&path));r2_radio_service_continuous(true);fake_adv_success=true;
 local=(struct r2_source_clock){.epoch=1,.head=1,.count=1,.utc_bound=true,.utc_source_epoch=3,.utc_offset_ns=1000000000000LL};
 local.maps[0]=(struct r2_source_map){.from=8000000,.until=32000000,.anchor_ns=1000000000,.generation=11,.rate_q16=16000000*UINT64_C(65536)};
 r2_radio_service_source(&local,true,3);
 grant=(struct r2_radio_scope){.root=0,.root_identity=17,.boot=88,.source_epoch=4,.local_source_epoch=3,.publication_epoch=5,.until_us=fake_time+1000000,.gm=true};
}
static void publish_own(uint64_t resume){struct r2_group_client empty={0};r2_radio_service_gm7_publish(grant,&empty,&path,1000000000000LL,fake_time,resume);}
static void follower(void){
 anchor=(struct r2_group_client){.passive=true,.source=true,.root=99,.boot=77,.source_epoch=8,.publication=9,
  .uncertainty_us=UINT32_MAX,.utc_us=UINT64_C(1792800001000000),.at=fake_time,.until=fake_time+1000000,.map_until=fake_time+1000000};
 grant=(struct r2_radio_scope){.root=1,.root_identity=99,.boot=77,.source_epoch=8,.publication_epoch=9,.remote_capture_epoch=4,.until_us=anchor.until,.follower=true};
 r2_radio_service_gm7_publish(grant,&anchor,&path,-1,fake_time,anchor.until);
}
static void replace_grant(void){fake_after_stats=NULL;++grant.boot;++grant.source_epoch;++grant.publication_epoch;publish_own(grant.until_us);}
static void overflow_resume_probe(void){
 if(!pt_input.stopped)return;
 fake_mutex_unlocked=NULL;assert(error==-ENOBUFS);publish_own(grant.until_us);assert(pt_input.stopped&&error==-ENOBUFS);
}
static void first_optin_probe(void){
 if(++optin_unlocks!=2)return; /* After ordinary final-export precheck. */
 fake_mutex_unlocked=NULL;struct r2_radio_scope original=pt_input.grant;
 ++grant.boot;publish_own(grant.until_us);
 assert(!pt_input.gm7_managed&&pt_input.gm7_busy&&same(pt_input.grant,original));
}
static void expire_after_copy(void){fake_mutex_unlocked=NULL;fake_time=capture_us=grant.until_us;capture_tick=fake_time*16;}
static void stop_after_query(void){fake_mutex_unlocked=NULL;r2_radio_service_withdraw();}
static void drive_stream_range(unsigned first,unsigned samples){
  uint64_t base=1000000000;struct r2_enrollment receive_roster=enrollment;
  for(unsigned i=first;i<samples;i++){
   uint64_t ns=base+(uint64_t)i*62500000;
   struct r2_precision_packet p={.root=0,.boot=77,.source_epoch=8,.train=9,.publication=i+1,.capture_epoch=4,.flags=1,
    .created_ns=ns-1000000,.expires_ns=base+(i/16+1)*UINT64_C(1000000000)+450000000,.utc_offset_ns=INT64_C(1792800000000000000)};
   if(i){p.count=1;p.correction[0]=(struct r2_precise_correction){.publication=i,.event=(uint16_t)i,.channel=3,.ticks=(base+(uint64_t)(i-1)*62500000)*2/125,.generation=9,.expires_ns=p.expires_ns};}
   enrollment=receive_roster;enrollment.identity[0]=99;enrollment.identity[1]=17;if(!i)generic_frame=ns-2000000;
   packet=(struct r2_radio_record){.raw=16000000+i*1000000+144,.epoch=1,.length=R2_RELAY_AD_BYTES,.event=(uint16_t)(i+1),.channel=3};
   assert(!generic_encode(packet.ad,&p));enrollment=receive_roster;
   fake_time=capture_us=1000000+(uint64_t)i*62500+1000;capture_tick=(uint64_t)packet.raw+16000;queued=true;precision_step(fake_time);
   assert(!queued&&!error&&!pt_input.source_ready&&!pt_output.scope.gm);
   if(replay_query){struct r2_gm7_follow_diagnostic d;r2_radio_service_gm7_follow_diagnostic(&d,fake_time);assert(d.data_until_tick==precision.data_until);}
   if(replay_record){
    const r2_servo_map_t *m=&precision.servo.output;
    uint64_t values[]={pt_output.utc,pt_output.valid_until,pt_output.utc_valid,pt_output.view.map_valid,
     precision.accepted,precision.rejected,precision.data_until,precision.last_available,precision.last_accepted,
     m->delivery,(uint64_t)m->anchor,(uint64_t)(int64_t)m->rate,(uint64_t)(int64_t)m->phase,m->valid,
     precision.servo.count,precision.servo.span,(uint64_t)precision.servo.rate,
     pt_output.scope.root_identity,pt_output.scope.boot,pt_output.scope.source_epoch,precision.train,precision.remote_capture};
    replay_hash=(replay_hash*16777619u)^r2_crc32(values,sizeof values);
   }
  }
}
static void drive_stream(unsigned samples){drive_stream_range(0,samples);}
int main(int argc,char **argv){
 assert(argc==2);unsigned test=(unsigned)strtoul(argv[1],NULL,10);
 if(test>=100){char mode_text[16];snprintf(mode_text,sizeof mode_text,"%u",test-100);char *historical[]={argv[0],mode_text};return historical_radio_main(2,historical);}
 setup_pt();
#ifdef R2_LORA_FIXED500_EXPERIMENT
 if(test==90||test==91||test==92){
  lora_needed=false;ble_needed=test!=91;replay_record=true;replay_query=test==92;
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);follower();precision_poll(fake_time,&running);
  drive_stream(900);assert(precision.accepted>400&&pt_output.utc_valid&&!lora_publishes);
  assert(ble_needed?(ble_observations>400&&observed_ble.qualified):!ble_observations);
  printf("BLE_MODEL_REPLAY %08x accepted%u rejected%u until%llu observer%u\n",replay_hash,
   precision.accepted,precision.rejected,(unsigned long long)pt_output.valid_until,ble_observations);
  fake_time=capture_us+=2000000;capture_tick+=32000000;precision_step(fake_time);
  assert(!pt_output.utc_valid&&!pt_output.view.map_valid);
  puts("PASS identical realMAC900packet BLE model/UTC/permission trajectory with receiver observation ON/OFF; silence cannot extend original map life");return 0;
 }
#endif
 if(test==29){
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);follower();precision_poll(fake_time,&running);drive_stream(900);
  fprintf(stderr,"causal setup utc%d data%llu modeldata%llu physical%llu tick%llu scope%d/%d accepted%u\n",pt_output.utc_valid,(unsigned long long)pt_output.gm7.data_until,(unsigned long long)precision.data_until,(unsigned long long)pt_output.capture_until,(unsigned long long)capture_tick,pt_output.scope.follower,pt_output.scope.broadcast,precision.accepted);
  assert(pt_output.utc_valid&&pt_output.gm7.data_until==precision.data_until&&pt_output.gm7.data_until<pt_output.capture_until);
  struct r2_radio_precision saved=precision;struct r2_gm7_follow_diagnostic d;
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);assert(!d.refusals&&d.utc_readable_now&&d.scope_equal&&d.binding_current);
  ++grant.source_epoch;r2_radio_service_gm7_publish(grant,&anchor,&path,-1,fake_time,anchor.until);
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert(!d.scope_equal&&d.binding_current&&d.utc_readable_now&&!(d.refusals&R2_FOLLOW_BINDING));
  --grant.source_epoch;r2_radio_service_gm7_publish(grant,&anchor,&path,-1,fake_time,anchor.until);
  assert(!memcmp(&saved,&precision,sizeof saved));
  /* Same query/capture observes data expiry while the unchanged physical
   * map, causal fit, control permission and published UTC remain available. */
  uint64_t delta=pt_output.gm7.data_until+1-capture_tick;capture_tick+=delta;fake_time=capture_us+=delta/16+1;
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert((d.refusals&(R2_FOLLOW_DATA_EXPIRED|R2_FOLLOW_EFFECTIVE_EXPIRED))==(R2_FOLLOW_DATA_EXPIRED|R2_FOLLOW_EFFECTIVE_EXPIRED));
  assert(!(d.refusals&(R2_FOLLOW_PHYSICAL_EXPIRED|R2_FOLLOW_FIT|R2_FOLLOW_OUTPUT|R2_FOLLOW_BINDING))&&d.utc_at_sample&&d.map_at_sample&&!d.utc_readable_now);
  uint64_t utc;assert(!r2_radio_service_client_clock(&utc,fake_time));
  assert(!memcmp(&saved,&precision,sizeof saved));
  capture_test_epoch=2;r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert((d.refusals&R2_FOLLOW_COUNTER)&&!(d.refusals&(R2_FOLLOW_DATA_EXPIRED|R2_FOLLOW_PHYSICAL_EXPIRED))&&!d.utc_readable_now);
  capture_test_epoch=1;capture_test_available=false;r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert((d.refusals&R2_FOLLOW_CAPTURE)&&!(d.refusals&(R2_FOLLOW_DATA_EXPIRED|R2_FOLLOW_PHYSICAL_EXPIRED)));
  capture_test_available=true;capture_tick=pt_output.capture_until;fake_time=capture_us+=2000000;
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);assert(d.refusals&R2_FOLLOW_PHYSICAL_EXPIRED);
  fake_mutex_unlocked=stop_after_query;r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert(!d.stopped&&d.grant.root_identity==99&&pt_input.stopped);
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);assert(d.stopped&&(d.refusals&R2_FOLLOW_STOPPED)&&(d.refusals&R2_FOLLOW_BINDING));
  assert(!memcmp(&saved,&precision,sizeof saved));
  puts("PASS actual PT single-capture dataexpiry vs physical/control/quality; captureunknown/epoch mismatch; atomic scope transition and no model/lease mutation");return 0;
 }
 if(test==30){
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);follower();precision_poll(fake_time,&running);drive_stream(200);
  struct r2_radio_precision saved=precision;struct r2_gm7_follow_diagnostic d;uint64_t utc;
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert(d.utc_readable_now==r2_radio_service_client_clock(&utc,fake_time));
  printf("CAUSAL200 utc%d data%llu physical%llu tick%llu refusals%u\n",d.utc_readable_now,(unsigned long long)d.data_until_tick,(unsigned long long)d.physical_until_tick,(unsigned long long)d.query_tick,d.refusals);
  uint64_t delta=pt_output.capture_until+1-capture_tick;capture_tick+=delta;fake_time=capture_us+=delta/16+1;
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert((d.refusals&R2_FOLLOW_PHYSICAL_EXPIRED)&&!d.utc_readable_now&&!r2_radio_service_client_clock(&utc,fake_time));
  assert(!memcmp(&saved,&precision,sizeof saved));
  puts("PASS original200packet fixture retained: diagnostic agrees with original clock reader; originalphysicalexpiry still fences without model changes");return 0;
 }
 if(test==28){
  publish_own(grant.until_us);unsigned initial=lora_withdraws;uint64_t sampled=fake_time;
  grant.until_us=fake_time+100;fake_time+=101;
  struct r2_group_client empty={0};r2_radio_service_gm7_publish(grant,&empty,&path,-1,sampled,grant.until_us);
  assert(lora_withdraws==initial+1);puts("PASS delayed same-source publication cannot suppress expiry withdrawal");return 0;
 }
 if(test==27){
  publish_own(grant.until_us);unsigned initial=lora_withdraws,shortened=0;uint64_t prior=grant.until_us;
  for(unsigned i=0;i<200;i++){
   uint64_t hf=1000000+i*2000;
   uint64_t kernel=(hf*32768/1000000)*1000000/32768;
   struct gm7_bridge b={.hf=hf,.kernel_lo=kernel,.kernel_hi=kernel,.epoch=1};
   assert(gm7_bridge_to_kernel(b,2000000,&grant.until_us));
   if(grant.until_us<prior)++shortened;prior=grant.until_us;
   fake_time=kernel;publish_own(grant.until_us);
   assert(pt_input.grant.until_us==grant.until_us&&!pt_input.stopped);
  }
  assert(shortened>20);
#ifdef R2_LORA_PASSIVE_FAST
  assert(lora_withdraws==initial);
#else
  assert(lora_withdraws==initial+shortened);
#endif
  ++grant.boot;publish_own(grant.until_us);assert(lora_withdraws>initial);initial=lora_withdraws;
  ++grant.local_source_epoch;publish_own(grant.until_us);assert(lora_withdraws==initial+1);initial=lora_withdraws;
  pt_input.source_ready=false;--grant.until_us;publish_own(grant.until_us);assert(lora_withdraws==initial+1);initial=lora_withdraws;
  pt_input.source_ready=true;grant.local_source_epoch=pt_input.source_epoch;grant.until_us=fake_time;publish_own(fake_time);assert(lora_withdraws==initial+1);
  puts("PASS actual HF/kernel bridge jitter retains passive own source; PT deadline still clips; root/source/expiry changes still withdraw");return 0;
 }
 if(test==0){
  follower();precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  assert(!public_client&&pt_output.scope.follower&&native_owned&&sync_object);
  handoff.root=99;handoff.valid=true;r2_radio_service_withdraw();precision_poll(fake_time,&running);
  grant=(struct r2_radio_scope){.root=0,.root_identity=17,.boot=88,.source_epoch=4,.local_source_epoch=3,.publication_epoch=5,.until_us=fake_time+1000000,.gm=true};
  publish_own(grant.until_us);assert(!pt_input.stopped&&handoff.valid&&handoff.root==99);
  r2_radio_service_gm7_tick(fake_time,NULL);assert(r2_radio_service_closed());
  uint64_t completed_retry=retry_at;
  for(unsigned n=1;n<=4;n++){fake_time=capture_us=1000000+n*50000;capture_tick=fake_time*16;
   if(!r2_radio_service_closed())r2_radio_service_stop(fake_time);
   assert(retry_at==completed_retry);
  }
  fake_time=capture_us=1300000;capture_tick=fake_time*16;precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  assert(publication==1&&pt_output.length&&fake_adv_creates==1&&native_gm_started&&r2_radio_service_serving(fake_time)&&pa_started&&ext_started&&!error);
  puts("PASS actual follower withdrawal/drain -> bounded ownGM resume -> payload and PA/EXT start; handoff not cleared");
 }else if(test==1){
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);follower();precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  assert(!public_client&&pt_output.scope.follower&&sync_object&&!pt_input.source_ready);
  drive_stream(10);
  assert(precision.accepted>=4&&!precision.rejected);puts("PASS bounded authenticated follower resumes actual captured G1 association without local GNSS/ownGM authority");
 }else if(test>=2&&test<=8){
  uint64_t until=grant.until_us;
  if(test==2)pt_input.source_ready=false;
  if(test==3)pt_input.source_epoch=10;
  if(test==4)until=fake_time;
  if(test==5)grant.until_us=fake_time;
  if(test==6)grant.root_identity=777;
  if(test==7)error=-EIO;
  if(test==8)until=0;
  publish_own(until);precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  assert(pt_input.stopped&&!publication&&!native_gm_started&&!fake_adv_creates);
  printf("PASS invalid resume gate%u remains stopped without positive output\n",test);
 }else if(test==9){
  publish_own(grant.until_us);precision_poll(fake_time,&running);struct pt_output old=pt_output;
  ++grant.boot;++grant.source_epoch;++grant.publication_epoch;publish_own(grant.until_us);
  pt_output=old;struct pt_output out;output_read(&out,fake_time);
  assert(!out.scope.gm&&!out.scope.follower&&!out.view.authorized&&!out.utc_valid&&!out.context.source&&!native_live(&old,fake_time));
  puts("PASS old in-flight different-scope publication cannot revive mailbox or native HCI authority");
 }else if(test==10){
  publish_own(grant.until_us);precision_poll(fake_time,&running);
  pt_output.valid_until=pt_output.context.until=pt_output.context.map_until=grant.until_us;
  pt_output.capture_until=capture_tick+16000000;pt_output.view.authorized=pt_output.view.map_valid=pt_output.view.locked=pt_output.utc_valid=pt_output.context.source=true;
  uint64_t original=pt_output.sampled;grant.until_us=fake_time+100000;publish_own(grant.until_us);
  struct pt_output out;output_read(&out,fake_time);assert(out.scope.until_us==grant.until_us&&out.valid_until==grant.until_us&&out.context.until==grant.until_us&&out.context.map_until==grant.until_us&&out.sampled==original);
  fake_time=capture_us=grant.until_us;capture_tick=fake_time*16;output_read(&out,fake_time);
  assert(!out.scope.gm&&!out.view.authorized&&!out.view.map_valid&&!out.utc_valid&&!out.context.source&&pt_output.sampled==original);
  puts("PASS same-scope cached output keeps original timestamp/map and obeys tightened exact expiry");
 }else if(test==11){
  publish_own(grant.until_us);precision_poll(fake_time,&running);r2_radio_service_withdraw();
  r2_radio_service_gm7_tick(fake_time,NULL);assert(pt_input.stopped&&!r2_radio_service_serving(fake_time)&&!fake_adv_creates);
  publish_own(0);r2_radio_service_gm7_tick(fake_time,NULL);assert(pt_input.stopped&&!fake_adv_creates);
  puts("PASS stop after publication and consume cannot clear stop or rewrite/resume grant");
 }else if(test==12){
  publish_own(grant.until_us);precision_poll(fake_time,&running);
  fake_capture_overflow=1;precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  assert(!pt_input.stopped&&!error&&pt_output.scope.gm&&last_overflow==1);
  puts("PASS GM precision record loss resets association and resumes existing capture path without retiring GNSS/PPS or latching error");
 }else if(test==13){
  publish_own(grant.until_us);precision_poll(fake_time,&running);unsigned before=lora_publishes;
  fake_after_stats=replace_grant;precision_step(fake_time);assert(!fake_after_stats&&!pt_output.scope.gm&&!pt_output.scope.follower&&lora_publishes==before);
  puts("PASS grant replaced while PT in flight fences LoRa export and mailbox at final commit");
 }else if(test==14){
  publish_own(grant.until_us);precision_poll(fake_time,&running);unsigned before=lora_publishes;
  fake_time=grant.until_us;precision_step(1000000);assert(!pt_output.scope.gm&&!pt_output.scope.follower&&lora_publishes==before);
  puts("PASS advanced validation-now at original expiry rejects stale in-flight LoRa/positive scope");
 }else if(test==15){
  r2_radio_service_activate();r2_radio_service_tick_reference(grant,&local,1000000000000LL,fake_time,NULL);
  fake_mutex_unlocked=first_optin_probe;precision_step(fake_time);
  assert(optin_unlocks==2&&!fake_mutex_unlocked&&!pt_input.gm7_busy&&!pt_input.gm7_managed);
  publish_own(grant.until_us);assert(pt_input.gm7_managed);precision_step(fake_time);
  assert(pt_output.scope.boot==grant.boot&&!pt_input.gm7_busy);
  puts("PASS first GM opt-in cannot overtake ordinary final-precheck/unlocked side export; unchanged grant retries after real quiescence");
 }else if(test==16){
  follower();precision_poll(fake_time,&running);assert(pt_output.scope.follower&&!pt_output.utc_valid);
  fake_mutex_unlocked=expire_after_copy;struct pt_output out;output_read(&out,fake_time);
  assert(!out.scope.gm&&out.scope.follower&&!out.acquisition&&!out.utc_valid&&!out.context.source);
  puts("PASS old control deadline cannot close broadcast listening or fabricate data/map authority");
 }else if(test>=17&&test<=20){
  follower();pt_input.stopped=true;
  if(test==17)grant.root_identity=777;
  if(test==18)grant.boot=0;
  if(test==19)grant.root=0;
  if(test==20)error=-EIO;
  r2_radio_service_gm7_publish(grant,&anchor,&path,-1,fake_time,0);
  precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  assert(pt_input.stopped&&!pt_output.scope.follower&&!native_owned&&!fake_adv_creates);
  printf("PASS invalid broadcast identity/boot/ownership/error%u retains stop\n",test);
 }else if(test==21){
  follower();precision_poll(fake_time,&running);assert(pt_output.scope.follower);
  r2_radio_service_withdraw();precision_poll(fake_time,&running);assert(pt_input.stopped);
  follower();assert(!pt_input.stopped);precision_poll(fake_time,&running);
  assert(pt_output.scope.follower&&!pt_output.scope.gm&&!error);
  puts("PASS stopped GM7 receipt-origin follower resumes only on fresh exact source grant; unknown UTC bound preserved");
 }else if(test==22){
  follower();precision_step(fake_time);
  precision.servo.count=12;precision.servo.span=160000000;precision.servo.saturated=7;
  precision_step(fake_time);struct r2_gm7_follow_diagnostic d;
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert(d.stats.count==12&&d.stats.span_us==10000000&&d.stats.saturated==7);
  struct r2_gm7_timing t;r2_radio_service_gm7_timing(&t,fake_time);
  assert(!t.first_tx_lo&&!t.first_tx_hi&&!t.first_tx_derived_end_hi&&!t.last_tx&&!t.tx_phase_us&&!t.first_tx_observed_complete);
  grant=(struct r2_radio_scope){.root=0,.root_identity=17,.boot=88,.source_epoch=4,.local_source_epoch=3,.publication_epoch=5,.until_us=fake_time+1000000,.gm=true};
  publish_own(grant.until_us);precision_step(fake_time);
  assert(!pt_output.gm7.first_tx_lo&&!pt_output.gm7.first_tx_observed_complete);
  pt_output.gm7.first_tx_lo=123;pt_output.gm7.first_tx_observed_complete=true;
  follower();precision_step(fake_time);r2_radio_service_gm7_timing(&t,fake_time);
  assert(!t.first_tx_lo&&!t.first_tx_observed_complete);
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);assert(!d.stats.count&&!d.stats.saturated);
  puts("PASS coherent follower diagnostics/GM firstTX40B union clears follower->GM->follower; no diagnostic value becomes TX proof");
 }else if(test>=23&&test<=26){
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);
  follower();precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  drive_stream(test==25?200:10);assert(precision.accepted>=4&&!pt_input.source_ready&&!pt_output.scope.gm);
  if(test==23){
   uint64_t contact=gm7_direct.tick,until=precision.data_until;
   fake_time=capture_us=grant.until_us+100000;capture_tick=fake_time*16;
   precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
   assert(pt_output.scope.follower&&!pt_input.stopped&&!pt_output.utc_valid&&
       !pt_output.view.map_valid&&gm7_direct.tick==contact&&precision.data_until==until);
   puts("PASS no localGNSS/control refresh: G1 following stays listening, actual matched packet expiry stops time and never renews contact");
  }else if(test==24){
   uint64_t contact=gm7_direct.tick;uint32_t accepted=precision.accepted;
   struct r2_enrollment receive_roster=enrollment;
   struct r2_precision_packet p={.root=0,.boot=77,.source_epoch=8,.train=9,.publication=11,
       .capture_epoch=5,.flags=1,.created_ns=1600000000,.expires_ns=2000000000,
       .utc_offset_ns=INT64_C(1792800000000000000)};
   enrollment.identity[0]=99;enrollment.identity[1]=17;memset(&generic_origin,0,sizeof generic_origin);
   packet.raw+=1000000;packet.event++;assert(!generic_encode(packet.ad,&p));enrollment=receive_roster;
   fake_time=capture_us=1700000;capture_tick=(uint64_t)packet.raw+16000;queued=true;precision_step(fake_time);
   assert(gm7_direct.tick>contact&&precision.accepted==accepted&&precision.remote_capture==4);
   contact=gm7_direct.tick;packet.raw+=10000;capture_tick+=10000;queued=true;precision_step(++fake_time);
   assert(gm7_direct.tick==contact); /* Same publication is retained replay. */
   p.source_epoch=9;p.train=10;p.publication=1;
   enrollment.identity[0]=99;enrollment.identity[1]=17;memset(&generic_origin,0,sizeof generic_origin);assert(!generic_encode(packet.ad,&p));enrollment=receive_roster;
   packet.raw+=10000;capture_tick+=10000;queued=true;precision_step(++fake_time);
   assert(gm7_direct.source==9&&gm7_direct.train==10&&gm7_direct.publication==1&&
       precision.scope.source_epoch==9&&precision.train==10&&!precision.servo.fit_valid);
   puts("PASS authenticated raw G1 liveness before bad capture/fit, no duplicate renewal, new source/train resets publication floor and retires timing fit");
  }else if(test==25){
   r2_radio_service_handoff_request(99,77,8);precision_poll(fake_time,&running);
   assert(handoff.valid&&handoff.pending&&!r2_radio_service_handoff_ready(99,77,8));
   uint64_t saved=handoff.last_accepted;precision_poll(fake_time,&running);
   assert(handoff.last_accepted==saved&&handoff.pending);
   r2_radio_service_stop(fake_time);precision_poll(fake_time,&running);
   assert(!handoff.pending&&handoff.valid&&r2_radio_service_handoff_ready(99,77,8));
   r2_radio_service_handoff_request(99,78,8);precision_poll(fake_time,&running);
   assert(!handoff.valid); /* A different boot never inherits the fit. */
   puts("PASS same real fit snapshot while listening, repeat immutability, ACK only after actual stop/drain and wrong-boot invalidation");
  }else{
   uint64_t contact=gm7_direct.tick;
   packet.ad[20]^=1;packet.raw+=10000;capture_tick+=10000;queued=true;precision_step(++fake_time);
   assert(gm7_direct.tick==contact);
   struct r2_group_client empty={0};++grant.boot;
   r2_radio_service_gm7_publish(grant,&empty,&path,-1,fake_time,0);precision_step(fake_time);
   assert(!pt_output.utc_valid&&!pt_output.view.map_valid&&pt_output.scope.boot==grant.boot);
   puts("PASS bad MAC cannot renew liveness, different selected boot cannot inherit delivered timing");
  }
 }else assert(false);
 assert(!pt_lock.held);return 0;
}
