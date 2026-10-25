/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual PT/mailbox/precision/codec/native-start bodies; fake HAL/HCI only. */
#include "gm7_pt_radio_fixture.inc"
static struct r2_enrollment path;
static struct r2_source_clock local;
static struct r2_radio_scope grant;
static struct r2_group_client anchor;
static bool running;
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
int main(int argc,char **argv){
 assert(argc==2);unsigned test=(unsigned)strtoul(argv[1],NULL,10);
 if(test>=100){char mode_text[16];snprintf(mode_text,sizeof mode_text,"%u",test-100);char *historical[]={argv[0],mode_text};return historical_radio_main(2,historical);}
 setup_pt();
 if(test==0){
  follower();precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  assert(public_client&&pt_output.scope.follower&&native_owned&&sync_object);
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
  assert(public_client&&pt_output.scope.follower&&sync_object&&!pt_input.source_ready);
  uint64_t base=1000000000;struct r2_enrollment receive_roster=enrollment;
  for(unsigned i=0;i<10;i++){
   uint64_t ns=base+(uint64_t)i*62500000;
   struct r2_precision_packet p={.root=0,.boot=77,.source_epoch=8,.train=9,.publication=i+1,.capture_epoch=4,.flags=1,
    .created_ns=ns-1000000,.expires_ns=base+1000000000,.utc_offset_ns=INT64_C(1792800000000000000)};
   if(i){p.count=1;p.correction[0]=(struct r2_precise_correction){.publication=i,.event=(uint16_t)i,.channel=3,.ticks=(base+(uint64_t)(i-1)*62500000)*2/125,.generation=9,.expires_ns=base+1000000000};}
   enrollment=receive_roster;enrollment.identity[0]=99;enrollment.identity[1]=17;if(!i)generic_frame=ns-2000000;
   packet=(struct r2_radio_record){.raw=16000000+i*1000000+144,.epoch=1,.length=R2_RELAY_AD_BYTES,.event=(uint16_t)(i+1),.channel=3};
   assert(!generic_encode(packet.ad,&p));enrollment=receive_roster;
   fake_time=capture_us=1000000+(uint64_t)i*62500+1000;capture_tick=(uint64_t)packet.raw+16000;queued=true;precision_step(fake_time);
   assert(!queued&&!error&&!pt_input.source_ready&&!pt_output.scope.gm);
  }
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
  publish_own(grant.until_us);fake_capture_overflow=1;fake_mutex_unlocked=overflow_resume_probe;precision_poll(fake_time,&running);
  assert(!fake_mutex_unlocked&&pt_input.stopped&&error==-ENOBUFS&&!publication&&!fake_adv_creates);
  puts("PASS PT overflow publishes harderror+stop atomically before attempted resume");
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
  follower();precision_poll(fake_time,&running);assert(pt_output.acquisition);
  fake_mutex_unlocked=expire_after_copy;struct pt_output out;output_read(&out,fake_time);
  assert(!out.scope.gm&&!out.scope.follower&&!out.acquisition&&!out.view.authorized&&!out.utc_valid&&!out.context.source);
  puts("PASS fresh postcopy clock expiry fences managed follower acquisition too");
 }else if(test>=17&&test<=20){
  follower();pt_input.stopped=true;
  if(test==17)++anchor.boot;if(test==18)++anchor.source_epoch;if(test==19)++anchor.publication;
  r2_radio_service_gm7_publish(grant,&anchor,&path,-1,fake_time,test==20?fake_time:grant.until_us);
  precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  assert(pt_input.stopped&&!pt_output.scope.follower&&!native_owned&&!fake_adv_creates);
  printf("PASS invalid authenticated follower resume gate%u retains stop\n",test);
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
 }else assert(false);
 assert(!pt_lock.held);return 0;
}
