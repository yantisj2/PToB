/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual owner and authenticated wire; no hardware or elected-source claim. */
#define main historical_fixed500_main
#include "lora_fixed500_test.c"
#undef main
static struct lp_ble_scope ble={.parent=3,.parent_boot=4,.root=3,.root_boot=4,
 .source_epoch=5,.train=6,.parent_capture_epoch=1,.capture_epoch=7};
static struct lp_packet meta;
static void pair(unsigned cursor,uint64_t event){
 struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=cursor};rx(&sync,event,event+200000);
 struct lp_packet follow={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=cursor,.utc_ns=UINT64_C(1000000000000)+event*1000};
 rx(&follow,event+2800000,event+3000000);report(now_us);
}
static void seed(bool rc,enum rc8_selected normal){
 now_us=1000000;witness_enabled=true;witness_published=witness_withdrawn=0;
 init(false);r2_lora_control(rc,true,normal);step();
 meta=(struct lp_packet){.kind=LP_META,.schedule=LP_SCHEDULE,.serial=1,
  .time_valid=true,.has_pair=true,.delay_10ms=600,.utc_ns=UINT64_C(1000000000000),.tz={2,-360},
  .scope={.emitter=1,.boot=2,.root=1,.root_boot=2,.epoch=1,.stream=1,.seed=17,.kind=1}};
 rx(&meta,10000000,10700000);
 for(unsigned i=0;i<10;i++)pair(2*i,UINT64_C(17000000)+(uint64_t)i*LP_BEAT_US);
 assert(role.rx.trained&&role.rx.display_valid&&role.watch.rx.display_valid&&rf_observer.valid);
 r2_lora_ble_observe(&ble,now_us+2000000,0,now_us);
}
static void hidden(void){
 struct r2_lora_view v;r2_lora_view(&v);
 assert(!v.utc_us&&!v.time_valid&&!v.quality_known&&!v.observer_available&&!v.display_held&&!v.display_acquiring&&v.choice==RC8_SELECT_NONE);
 struct r2_rf_clock_sample rf;r2_lora_rf_clock(&rf,(struct r2_capture){.kind=R2_CAPTURE_NOW,.tick=now_us*16,.epoch=7},now_us);assert(!rf.available);
 r2_lora_shadow_publish();assert(!shadow_tick);
}
int main(int argc,char **argv){
 assert(argc==2);unsigned scenario=(unsigned)atoi(argv[1]);
 if(scenario==1){
  seed(false,RC8_SELECT_BLE);uint32_t serial=replay[0].serial,cursor=replay[0].cursor;
  r2_lora_control(false,true,RC8_SELECT_NONE);hidden();assert(atomic_get(&flags)&GM_BLE_LOST);
  step();hidden();assert(owner.active&&owner.listening&&(atomic_get(&flags)&REQUESTED));
  assert(!role.rx.trained&&!role.rx.have_pair&&!role.rx.display_valid&&!role.watch.admitted&&!rf_observer.valid);
  assert(role.rx.cached&&role.watch.rx.cached&&role.watch.rx.tz.id==2&&role.watch.rx.tz.offset_min==-360);
  assert(replay[0].serial==serial&&replay[0].cursor==cursor);
  meta.serial=2;meta.cursor=22;meta.utc_ns+=UINT64_C(300000000000);
  uint32_t received=owner.received[LP_META-1];rx(&meta,now_us+1000000,now_us+1700000);
  assert(owner.received[LP_META-1]>received&&replay[0].serial==2&&!role.watch.admitted);
  uint32_t accepted=role.rx.accepted;pair(22,now_us+2000000);assert(role.rx.accepted==accepted);hidden();
  /* Fresh core BLE resumes immediately; no old map survives its return. */
  r2_lora_control(false,true,RC8_SELECT_BLE);assert(!(atomic_get(&flags)&GM_BLE_LOST)&&(atomic_get(&flags)&NORMAL));step();
  hidden();puts("PASS GM BLE loss fences LoRa immediately, keeps RX/META/TZ/replay, and cannot readmit the retired train while waiting for BLE");
 }else if(scenario==2){
  seed(false,RC8_SELECT_BLE);r2_lora_control(false,true,RC8_SELECT_LOCAL_GM);hidden();
  assert(!(atomic_get(&flags)&GM_BLE_LOST)&&(atomic_get(&flags)&GM_TIME_CLEAR));step();hidden();
  assert(!role.rx.have_pair&&!role.rx.display_valid&&!role.watch.admitted&&(atomic_get(&flags)&NORMAL));
  puts("PASS qualified own-GM takeover clears old LoRa timing even without a NONE interval");
 }else if(scenario==3){
  seed(false,RC8_SELECT_BLE);uint64_t reference=role.watch.rx.display_us;
  /* Repeated same-context publication / quality-refusal count is not a source loss. */
  for(unsigned i=0;i<3;i++){
   r2_lora_ble_observe(&ble,now_us+2000000,i,now_us);r2_lora_control(false,true,RC8_SELECT_BLE);step();
   assert(!(atomic_get(&flags)&GM_TIME_BLOCKED)&&role.watch.rx.display_us==reference&&role.rx.display_valid);
  }
  struct lp_ble_scope replacement=ble;++replacement.train;
  r2_lora_ble_observe(&replacement,now_us+2000000,2,now_us);hidden();
  assert(atomic_get(&flags)&GM_TIME_CLEAR);step();hidden();
  assert(!(atomic_get(&flags)&GM_BLE_LOST)&&!role.watch.admitted);
  puts("PASS same-commit same-scope refresh leaves maps intact; authenticated full-scope replacement cannot inherit them");
 }else if(scenario==4){
  seed(true,RC8_SELECT_BLE);r2_lora_control(true,true,RC8_SELECT_NONE);step();
  assert(!(atomic_get(&flags)&GM_TIME_BLOCKED)&&role.rx.display_valid&&published.utc_us&&published.choice==RC8_SELECT_LORA);
  now_us=role.watch.rx.last_us+90000000;report(now_us);
  assert(published.utc_us&&published.display_held&&published.observer_available);
  puts("PASS RC real BLE loss retains existing LoRa fallback and 90s HOLD");
 }else if(scenario==5){
  seed(false,RC8_SELECT_NONE);r2_lora_control(false,true,RC8_SELECT_NONE);step();
  assert(!(atomic_get(&flags)&GM_TIME_BLOCKED)&&role.rx.display_valid&&published.utc_us);
  puts("PASS no invented GM startup latch before any qualified normal clock");
 }else if(scenario==6){
  seed(false,RC8_SELECT_BLE);r2_lora_control(false,true,RC8_SELECT_NONE);step();hidden();
  r2_lora_control(true,true,RC8_SELECT_NONE);step();assert(!(atomic_get(&flags)&GM_TIME_BLOCKED));
  meta.serial=2;meta.cursor=22;meta.utc_ns+=UINT64_C(300000000000);rx(&meta,now_us+1000000,now_us+1700000);
  pair(22,now_us+2000000);assert(role.watch.admitted&&published.observer_available&&published.utc_us);
  puts("PASS explicit RC switch restores ordinary fresh authenticated first-pair LoRa observation");
 }else if(scenario==7){
  seed(false,RC8_SELECT_BLE);
  r2_lora_control(false,true,RC8_SELECT_LOCAL_GM);step();hidden();
  r2_lora_control(false,true,RC8_SELECT_NONE);hidden();step();
  assert(atomic_get(&flags)&GM_BLE_LOST);
  for(unsigned i=0;i<3;i++){
   meta.serial++;meta.cursor=22+2*i;meta.utc_ns+=UINT64_C(30000000000);
   uint64_t start=now_us+1000000;rx(&meta,start,start+700000);pair(meta.cursor,now_us+2000000);hidden();
   assert(owner.active&&owner.listening&&!role.watch.admitted&&!role.rx.have_pair);
  }
  puts("PASS BLE to brief qualified LOCAL_GM to NONE cannot revive continuous old LoRa packets; RX observations continue");
 }else if(scenario==8){
  char *args[]={"fixed500","8"};return historical_fixed500_main(2,args);
 }else assert(false);
 return 0;
}
