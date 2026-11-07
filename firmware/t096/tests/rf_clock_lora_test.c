/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual wire3 encoder/MAC receiver/watch and service publication. */
#define main historical_fixed500_cases
#include "lora_fixed500_rf_fixture.inc"
#undef main
static void sample(struct r2_rf_clock_sample *s){r2_lora_rf_clock(s,(struct r2_capture){now_us*16,fake_capture_epoch,R2_CAPTURE_NOW},now_us);}
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);
 init(false);step();struct r2_rf_clock_sample s;sample(&s);assert(!s.available);
 struct lp_packet m={.kind=LP_META,.schedule=LP_SCHEDULE,.scope={.emitter=1,.boot=22,.root=3,.root_boot=44,.parent=3,.parent_boot=44,.epoch=5,.stream=6,.seed=17,.kind=RC8_SELECTED_BLE},.cursor=0,.serial=1,.utc_ns=UINT64_C(1000000000000),.time_valid=true,.has_pair=true,.delay_10ms=600,.tz={0,INT16_MIN}};
 rx(&m,10000000,10700000);
 for(unsigned i=0;i<2;i++){
  uint64_t event=UINT64_C(17000000)+(uint64_t)i*LP_BEAT_US;
  struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=2*i};rx(&sync,event,event+200000);
  struct lp_packet follow={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=2*i,.utc_ns=UINT64_C(1000000000000)+event*1000};
  rx(&follow,event+2800000,event+3000000);report(now_us);
  struct lp_receiver_watch before=role.watch;struct lp_rx live=role.rx;struct rf_observer_map frozen=rf_observer;
  sample(&s);assert(s.available&&s.fresh&&s.utc&&s.provisional&&s.rate_valid==(i!=0));
  uint64_t expected;assert(r2_hold_lora_project(now_us*16,before.rx.display_tick,before.rx.display_ns,before.rx.display_rate_ppb,before.rx.display_phase_ns,&expected));
  assert(s.time_ns==expected&&s.root==3&&s.boot==44&&s.source_epoch==5&&s.train==6);
  assert(!memcmp(&before,&role.watch,sizeof before)&&!memcmp(&live,&role.rx,sizeof live)&&!memcmp(&frozen,&rf_observer,sizeof frozen));
  if(mode==2&&!i){
   uint64_t deadline=s.fresh_until_us;
   struct lp_packet bad={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=2};rx(&bad,47000000,47200000);
   bad=(struct lp_packet){.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=2,.utc_ns=UINT64_C(1048000000000)};
   rx(&bad,49800000,50000000);report(now_us);sample(&s);
   assert(s.available&&s.fresh&&s.provisional&&!s.rate_valid&&s.fresh_until_us==deadline);
   assert(role.watch.rx.fit_count==2&&role.watch.rx.model_accepted!=role.watch.rx.accepted);
   puts("PASS rejected rawsecondpair cannot promote firstpair nominalrate or renew its delivered-map deadline");return 0;
  }

 }
 uint64_t original_deadline=s.fresh_until_us;assert(original_deadline==role.watch.rx.last_us+35000000);
 /* A progressing physical pair whose candidate rate is rejected cannot renew
  * the old delivered map's freshness or advertise its nominal rate as learned. */
 uint64_t delivered_us=role.watch.rx.display_us;
 struct lp_packet bad_sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=4};rx(&bad_sync,77000000,77200000);
 struct lp_packet bad_follow={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=4,.utc_ns=UINT64_C(1078000000000)};
 rx(&bad_follow,79800000,80000000);report(now_us);sample(&s);
 assert(role.watch.rx.last_us==77000000&&role.watch.rx.display_us==delivered_us&&role.watch.rx.model_accepted!=role.watch.rx.accepted);
 assert(s.fresh_until_us==original_deadline&&s.provisional&&s.rate_valid);

 now_us=original_deadline;sample(&s);assert(s.available&&!s.fresh&&!s.rate_valid); /* real HOLD may continue independently */
 now_us=original_deadline-1;fake_capture_epoch++;sample(&s);assert(!s.available);fake_capture_epoch--;
 rf_observer.fresh_until_us=now_us+1000000;rf_observer.tick=now_us*16;rf_observer.phase=123;sample(&s);assert(s.rate_ppb==rf_observer.rate+123);
 rf_observer.ns=UINT64_MAX;sample(&s);assert(!s.available);
 r2_lora_request(false);sample(&s);assert(!s.available);
 puts("PASS actual wire3 firstpair ns/unknownPPB then measured provisional rate; sameHF map, exclusive35s/META/physical bounds, HOLD/counter/OFF/overflow no mutation");return 0;
}
