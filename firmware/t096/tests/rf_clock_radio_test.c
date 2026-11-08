/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual PT/source/MAC/delivered-map owners; physical HAL/HCI are synthetic. */
#define main historical_pt_cases
#include "gm7_pt_recovery_test.c"
#undef main
#include "rf_clock.h"
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);setup_pt();
 struct r2_rf_clock_sample s;
 if(mode==1){
  publish_own(grant.until_us);struct r2_capture at={capture_tick,1,R2_CAPTURE_NOW};
  int64_t expected;assert(r2_source_utc_read(&local,at,3,&expected));
  struct pt_input before=pt_input;struct pt_output old=pt_output;
  r2_radio_service_rf_clock(&s,at,fake_time);
  assert(s.fresh&&s.utc&&s.rate_valid&&!s.provisional&&s.time_ns==(uint64_t)expected&&s.root==17&&s.boot==88);
  assert(s.rate_ppb==0&&s.fresh_until_us<=grant.until_us&&s.fresh_until_us>fake_time);
  assert(!memcmp(&before,&pt_input,sizeof before)&&!memcmp(&old,&pt_output,sizeof old));
  pt_input.source.maps[0].rate_q16=16000160*UINT64_C(65536);pt_input.source.maps[0].phase_ppb=200;
  r2_radio_service_rf_clock(&s,at,fake_time);assert(s.fresh&&s.rate_ppb==-10199);
  pt_input.source_ready=false;r2_radio_service_rf_clock(&s,at,fake_time);assert(!s.available);
  pt_input.source_ready=true;pt_input.source_epoch++;r2_radio_service_rf_clock(&s,at,fake_time);assert(!s.available);
  pt_input.source_epoch--;pt_input.source.utc_bound=false;r2_radio_service_rf_clock(&s,at,fake_time);assert(!s.available);
  pt_input.source.utc_bound=true;at.epoch++;r2_radio_service_rf_clock(&s,at,fake_time);assert(!s.available);
  at.epoch--;at.tick=local.maps[0].until;r2_radio_service_rf_clock(&s,at,fake_time);assert(!s.available);
  puts("PASS ownGM original UTC/sourcegen/counter/map expiry; actual rate sign+phase; no owner mutation");return 0;
 }
 r2_radio_service_source(&(struct r2_source_clock){0},false,0);follower();precision_poll(fake_time,&running);drive_stream(500);
 assert(pt_output.utc_valid&&pt_output.gm7.retained);
 if(mode>=3){
  assert(lora_publishes&&source_gate==R2_LORA_SOURCE_READY&&precision.data_until<pt_output.capture_until);
  unsigned published=lora_publishes,withdrawn=lora_withdraws;
  uint64_t delta=precision.data_until-capture_tick;
  capture_tick+=delta;fake_time=capture_us+=delta/16+1;
  lora_hold_available=true;precision_step(fake_time);
  uint64_t utc;
  assert(!pt_output.utc_valid&&!r2_radio_service_client_clock(&utc,fake_time));
  assert(source_gate==R2_LORA_SOURCE_BLE_HOLDOVER&&lora_hold_queries==1);
  assert(lora_publishes==published&&lora_withdraws==withdrawn);
  if(mode==5){
   unsigned first=(unsigned)((fake_time-1001000)/62500+1);
   drive_stream_range(first,first+12);
   assert(pt_output.utc_valid&&source_gate==R2_LORA_SOURCE_READY&&lora_publishes>published&&lora_withdraws==withdrawn);
   assert(same(pt_output.scope,active_scope)&&precision.remote_capture==4);
   puts("PASS first fresh authenticated replacement restores the same LoRa train after bounded holdover");return 0;
  }
  if(mode==3)lora_hold_available=false;
  else if(mode==4)++capture_test_epoch;
  else r2_radio_service_withdraw();
  capture_tick+=16000;fake_time=capture_us+=1000;precision_step(fake_time);
  assert(lora_hold_queries==(mode==3?2:1)&&lora_publishes==published&&lora_withdraws>withdrawn);
  assert(source_gate!=R2_LORA_SOURCE_BLE_HOLDOVER);
  puts("PASS signed data expiry retains only existing LoRa map; expiry, capture change and explicit stop withdraw");return 0;
 }
 struct r2_capture at={capture_tick,1,R2_CAPTURE_NOW};int64_t expected;
 assert(r2_servo_mapped(&pt_output.gm7.map,at.tick,&expected));expected+=pt_output.gm7.utc_offset_ns;
 struct pt_input before=pt_input;struct pt_output old=pt_output;
 r2_radio_service_rf_clock(&s,at,fake_time);
 assert(s.available&&s.fresh&&s.utc&&s.time_ns==(uint64_t)expected&&s.root==99&&s.boot==77&&s.source_epoch==8&&s.train==9);
 assert(!memcmp(&before,&pt_input,sizeof before)&&!memcmp(&old,&pt_output,sizeof old));
 uint64_t expiry=s.fresh_until_us;r2_radio_service_rf_clock(&s,at,expiry);assert(!s.fresh&&!s.available);
 r2_radio_service_rf_clock(&s,(struct r2_capture){at.tick,2,R2_CAPTURE_NOW},fake_time);assert(!s.available);
 pt_input.stopped=true;r2_radio_service_rf_clock(&s,at,fake_time);assert(!s.available);pt_input.stopped=false;
 ++pt_input.grant.boot;r2_radio_service_rf_clock(&s,at,fake_time);assert(!s.available);--pt_input.grant.boot;
 pt_output.gm7.data_until=at.tick;r2_radio_service_rf_clock(&s,at,fake_time);assert(!s.available);pt_output=old;
 pt_output.gm7.map.phase=5000;pt_output.gm7.map.delivery=at.tick;
 r2_radio_service_rf_clock(&s,at,fake_time);assert(s.available&&s.rate_ppb==pt_output.gm7.map.rate+1000);
 pt_output.gm7.map.phase=-5000;r2_radio_service_rf_clock(&s,at,fake_time);assert(s.rate_ppb==pt_output.gm7.map.rate-1000);
 pt_output.gm7.map.anchor=INT64_MAX;r2_radio_service_rf_clock(&s,at,fake_time);assert(!s.available);
 puts("PASS realMAC500pair BLE map exactns, bound/counter/stop/parent/overflow gates and delivered phase rate; no owner mutation");return 0;
}
