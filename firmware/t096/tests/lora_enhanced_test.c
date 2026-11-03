/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Real passive core/codec, synthetic credentials and physical observations. */
#include "lora_passive_core.h"
#include "holdover.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void budget(void){
 assert((LP_SCHEDULE==3||LP_SCHEDULE==4)&&LP_BEAT_US==30000000&&LP_META_BEATS==10&&LP_CAP_US==108000000);
 const uint64_t phase[]={0,1,49999,50000,14999999,15000000,29999999,30000000,44999999,45000000,59999999};
 for(unsigned p=0;p<sizeof phase/sizeof phase[0];p++){
  uint64_t first=UINT64_C(3600000000)+phase[p],maximum=0;unsigned disc=0,meta=0,pairs=0;
  struct lp_tx tx;assert(lp_tx_start(&tx,17,first,first,(uint32_t)(first/1000)));
  for(unsigned i=0;i<48*120;i++){
   uint64_t at=first+(uint64_t)i*LP_BEAT_US;struct lp_reservation r;
   assert(lp_tx_reserve(&tx,at,&r)&&r.has_pair);
   unsigned wanted=i%10==0?LP_META:i%4==1?LP_DISC:0;
   assert(r.control==wanted);disc+=wanted==LP_DISC;meta+=wanted==LP_META;++pairs;
   uint64_t charge=lp_charged(&tx.durable,at);assert(charge<=LP_CAP_US);
   if(charge>maximum)maximum=charge;
   if(r.control)lp_tx_control_complete(&tx);
   uint64_t sync=at+LP_SYNC_OFFSET_US+lp_duration(LP_SYNC)-10000;
   assert(lp_tx_capture_sync(&tx,sync));
   assert(lp_tx_capture_follow(&tx,sync+LP_FOLLOW_OFFSET_US+lp_duration(LP_FOLLOW)-10000));
   assert(!tx.durable.pending&&!tx.durable.owed);
   if(i==119)assert(disc==30&&meta==12&&lp_charged(&tx.durable,at)==
#ifdef R2_LORA_PASSIVE_WIDE
    73887648
#else
    87434808
#endif
   );
  }
  assert(pairs==5760&&disc==1440&&meta==576&&maximum<=90534063);
  printf("BUDGET %llu %llu %u %u %u\n",(unsigned long long)phase[p],(unsigned long long)maximum,pairs,disc,meta);
 }
 /* Full-charge admission and synchronous no-RF refund include pair-only work. */
 struct lp_tx tx;struct lp_reservation r;assert(lp_tx_start(&tx,17,1000000,1000000,1000));
 tx.durable.slot=2;
 assert(lp_tx_reserve(&tx,1000000,&r)&&!r.control&&r.has_pair);
 assert(lp_charged(&tx.durable,1000000)==LP_SYNC_BUDGET_US+LP_FOLLOW_BUDGET_US);
 assert(lp_tx_refund_no_rf(&tx,&r)&&!lp_charged(&tx.durable,1000000));
 assert(!lp_tx_refund_no_rf(&tx,&r));
 tx.durable.buckets[0]=LP_CAP_US-(LP_SYNC_BUDGET_US+LP_FOLLOW_BUDGET_US-1);
 assert(!lp_tx_reserve(&tx,1000000,&r));
 puts("PASS enhanced48h full-charge, phases, independent DISC/META, pair-only and no-RF boundaries");
}

static const struct r2_group_config group={.id=9,.epoch=1,.psk={1,2,3}};
static void receiver(void){
 struct lp_rx r={0};struct lp_scope scope={.emitter=1,.boot=2,.root=1,.root_boot=2,.epoch=1,.stream=1,.seed=17,.kind=1};
 struct lp_packet m={.kind=LP_META,.schedule=LP_SCHEDULE,.scope=scope,.serial=1,.utc_ns=UINT64_C(1000000000000),.time_valid=true,.has_pair=true,.delay_10ms=587,.tz={0,INT16_MIN}};
 uint8_t wire[LP_META_BYTES],context[16];struct lp_packet decoded;
 assert(!lp_context(context,&scope,LP_SCHEDULE,&group));
 assert(!lp_encode(wire,sizeof wire,&m,context,&group));wire[3]=2;
 assert(lp_decode(&decoded,wire,sizeof wire,context,&group));
 assert(lp_rx_meta(&r,&m,&group,NULL,1000000,1000000));
 unsigned selected=0,scored=0;uint64_t squares=0,maximum=0;
 for(unsigned i=0;i<100;i++){
  uint64_t event=UINT64_C(8000000)+(uint64_t)i*LP_BEAT_US;
  if(i&&i%10==0){m.serial++;m.cursor=2*i;m.utc_ns+=UINT64_C(300000000000);assert(lp_rx_meta(&r,&m,&group,NULL,event-7000000,event-7000000));}
  if(!i){r.hint=true;r.hint_us=event;r.hint_cursor=0;}
  assert(lp_rx_sync(&r,2*i,event*16,7,event,event));
  /* Fixed causal rate plus periodic capture disturbances. Retain every point. */
  int64_t noise=i%17==8?5000:i%5==1?150:i%5==2?-150:0;
  uint64_t ns=UINT64_C(1000000000000)+event*1000+(event*123)/1000000+noise;
  struct lp_packet f={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=2*i,.utc_ns=ns};
  assert(lp_rx_follow(&r,&f,NULL,7,event+3000000));
  if(r.trained){
   uint64_t at=(event+3000000)*16,previous=0;bool continuous=r.display_valid&&!r.display_acquiring;
   if(continuous)assert(lp_rx_display_estimate(&r,at,7,event+3000000,&previous));
   assert(lp_rx_discipline(&r,at,7,event+3000000));
   if(continuous)assert(r.display_ns==previous);
   printf("MODEL %u %llu %llu %d %d %d\n",i,(unsigned long long)r.display_tick,(unsigned long long)r.display_ns,r.display_rate_ppb,r.display_phase_ns,r.phase_offset_ns);
   if(i>=10)for(unsigned second=0;second<30;second++){
    uint64_t us=event+3000000+(uint64_t)second*1000000,value;
    assert(lp_rx_display_estimate(&r,us*16,7,us,&value));
    uint64_t truth=UINT64_C(1000000000000)+us*1000+us*123/1000000;
    uint64_t error=value>=truth?value-truth:truth-value;squares+=error*error;++scored;if(error>maximum)maximum=error;
   }
  }
  assert(r.fit_count<=16&&r.hint_us==event+LP_BEAT_US&&r.hint_cursor==2*i+2);
  if(i<6)assert(!r.trained);
  else assert(r.trained);
  if(r.hold_selected)++selected;
  printf("PAIR %u %llu %llu %d %d %d %u %d %u %u %u\n",i,(unsigned long long)(event*16),(unsigned long long)ns,
   r.baseline_rate_ppb,r.rate_ppb,r.hold_short_ppb,r.hold_short_guard,r.hold_long_ppb,r.hold_long_guard,r.hold_selected,r.rms_ns);
 }
 assert(selected>0);
 assert(scored==2700&&squares/(uint64_t)scored<1000000);
 printf("HELD_GRID %u %llu %llu\n",scored,(unsigned long long)squares,(unsigned long long)maximum);
 uint64_t physical=r.physical_deadline_us,reference=r.display_tick;int32_t rate=r.display_rate_ppb;
 lp_rx_expire(&r,physical);assert(!r.trained&&r.display_valid&&r.display_tick==reference&&r.display_rate_ppb==rate);
 m.serial++;m.scope.stream++;m.utc_ns+=UINT64_C(300000000000);m.cursor=200;
 assert(lp_rx_meta(&r,&m,&group,NULL,physical+1,physical+1));
 assert(!r.trained&&!r.fit_count&&r.display_tick==reference);
 puts("PASS sparse HOLD causal sample span, rate convention, loss, context and schedule rejection");
}
int main(int argc,char **argv){assert(argc==2);if(atoi(argv[1])==1)budget();else receiver();}
