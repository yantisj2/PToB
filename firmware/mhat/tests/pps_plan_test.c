/* Copyright (c) 2026 Jonathan Yantis. MIT. Synthetic policy, not hardware proof. */
#include "pps_plan.h"
#include "pps_offer.h"
#include "source_monitor.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 /* Wrapper recovery cannot cut short native search + the longest trial. */
 assert(MHAT_CONTEXT_RECOVERY_MS*1000>RC_SEARCH_LIMIT_US+RC_STREAM_TRIAL_US);
 struct mhat_role_monitor monitor={0};
 struct r2_role_record report={.root=1,.root_boot=2,.sequence=10,.created_us=100,.valid_us=2000000,.source_epoch=1};
 assert(mhat_role_observe(&monitor,3,&report,1000,1001));
 assert(!mhat_role_observe(&monitor,3,&report,2000,2001));
 assert(monitor.row[0].received_us==1000);
 report.source_epoch++;report.sequence--;assert(!mhat_role_observe(&monitor,3,&report,2000,2001));
 report.sequence=11;assert(mhat_role_observe(&monitor,3,&report,2000,2001));
 report.root_boot++;report.sequence=1;assert(mhat_role_observe(&monitor,3,&report,3000,3001));
 report.sequence++;assert(!mhat_role_observe(&monitor,3,&report,3000,2003000));
 report.root_boot=2;report.sequence=10;assert(!mhat_role_observe(&monitor,3,&report,4000,4001));
 for(unsigned i=2;i<MHAT_ROLE_ROWS;i++){
  report.root_boot=10+i;report.sequence=1;assert(mhat_role_observe(&monitor,3,&report,5000+i,5001+i));
 }
 report.root_boot=99;assert(!mhat_role_observe(&monitor,3,&report,6000,6001));
 assert(monitor.capacity_refused==1);
 report.root_boot=2;report.sequence=10;assert(!mhat_role_observe(&monitor,3,&report,7000,7001));
 assert(monitor.row[0].received_us==2000);
 assert(mhat_pps_revision_current(1,1));
 assert(!mhat_pps_revision_current(1,2));
 /* A saturated snapshot cannot re-arm after a withdrawal at the same value. */
 assert(!mhat_pps_revision_current(UINT32_MAX,UINT32_MAX));
 assert(!mhat_pps_revision_current(UINT32_MAX-1,UINT32_MAX));
 assert(!mhat_pps_commit_ready(1,16000001));
 assert(mhat_pps_commit_ready(1,320001));
 assert(mhat_pps_commit_ready(1,48001));
 assert(!mhat_pps_commit_ready(1,48000));
 assert(!mhat_pps_commit_ready(UINT64_MAX,1));
 struct r2_uc_clock o={.valid=true,.capture_epoch=1,.until_tick=24000000,.session_until=32000000,
 .origin={.issued=1,.expires=3000000000,.utc_offset=1000000000000},
 .map={.effective=1,.anchor_tick=1,.anchor_ns=1,.generation=1}};
 uint64_t edge,second,utc;
 struct r2_uc_clock copy=o;
 assert(mhat_pps_offer_same(&o,&copy));
 copy.session_until--;assert(!mhat_pps_offer_same(&o,&copy));copy=o;
 copy.origin.utc_offset++;assert(!mhat_pps_offer_same(&o,&copy));copy=o;
 copy.origin.boot++;assert(!mhat_pps_offer_same(&o,&copy));copy=o;
 copy.scope[15]++;assert(!mhat_pps_offer_same(&o,&copy));copy=o;
 copy.map.phase_ns++;assert(!mhat_pps_offer_same(&o,&copy));copy=o;
 copy.map.generation++;assert(!mhat_pps_offer_same(&o,&copy));
 assert(mhat_pps_plan(&o,12000001,0,&edge,&second));assert(edge==16000001&&second==1001);
 assert(mhat_utc_at(&o,edge,&utc)&&utc>=1000999999968ULL&&utc<=1001000000032ULL);
 assert(!mhat_pps_plan(&o,12000001,second,&edge,&second));
 o.valid=false;assert(!mhat_pps_plan(&o,12000001,0,&edge,&second));o.valid=true;
 o.until_tick=15000000;assert(!mhat_pps_plan(&o,12000001,0,&edge,&second));o.until_tick=24000000;
 o.session_until=15000000;assert(!mhat_pps_plan(&o,12000001,0,&edge,&second));o.session_until=32000000;
 o.origin.expires=900000000;assert(!mhat_pps_plan(&o,12000001,0,&edge,&second));o.origin.expires=3000000000;
 /* The edge and its fall are valid even if the one-second endpoint is not.
  * A source-domain expiry must not discard the earlier valid crossing. */
 o.origin.expires=1100000000;
 assert(mhat_utc_at(&o,16000001,&utc));
 assert(mhat_utc_at(&o,16160001,&utc));
 assert(mhat_pps_plan(&o,15760001,0,&edge,&second));
 assert(edge==16000001&&second==1001);
 /* Never extend permission to fit the target or its ten-millisecond fall. */
 o.origin.expires=1005000000;
 assert(mhat_utc_at(&o,16000001,&utc));
 assert(!mhat_utc_at(&o,16160001,&utc));
 assert(!mhat_pps_plan(&o,15760001,0,&edge,&second));
 o.origin.expires=1000000000;
 assert(!mhat_pps_plan(&o,15760001,0,&edge,&second));
 o.origin.expires=3000000000;
 o.map.effective=13000000;assert(!mhat_pps_plan(&o,12000001,0,&edge,&second));o.map.effective=1;
 assert(!mhat_pps_plan(&o,15960001,0,&edge,&second));
 assert(!mhat_pps_plan(&o,UINT64_MAX-10,0,&edge,&second));
 o.origin.utc_offset=INT64_MIN;assert(!mhat_pps_plan(&o,12000001,0,&edge,&second));
 o.origin.utc_offset=1000000000000; o.map.ppb=150000;
 assert(mhat_pps_plan(&o,12000001,0,&edge,&second));
 assert(edge>16000001);assert(mhat_utc_at(&o,edge,&utc));assert(utc>=1000999999968ULL&&utc<=1001000000032ULL);
 o.map.ppb=-150000;assert(mhat_pps_plan(&o,12000001,0,&edge,&second));assert(edge<16000001);
 int64_t sum=0;unsigned early=0,late=0;
 o.map.ppb=0;
 for(unsigned fraction=0;fraction<125;fraction++){
  o.map.anchor_ns=1+fraction;
  assert(mhat_pps_plan(&o,12000001,0,&edge,&second));
  assert(mhat_utc_at(&o,edge,&utc));
  int64_t residual=(int64_t)utc-(int64_t)(second*UINT64_C(1000000000));
  assert(residual>=-32&&residual<=32);sum+=residual;
  early+=residual<0;late+=residual>0;
 }
 assert(early&&late&&sum>=-125&&sum<=125);
 puts("PPS phase, rate, deadline, stale-map and overflow checks passed (synthetic)");
}
