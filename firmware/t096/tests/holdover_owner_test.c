/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Execute the actual production RC adapter with deterministic physical pairs.
 */
#include "holdover.h"
#include "timing_events.h"
#include "precision.h"
#include "rc_shadow.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define K_FOREVER 0
static unsigned state_lock;
typedef int atomic_t;
static int atomic_get(const int *p){return *p;}
static int atomic_set(int *p,int n){int old=*p;*p=n;return old;}
static bool atomic_cas(int *p,int old,int n){if(*p!=old)return false;*p=n;return true;}
static void k_mutex_lock(unsigned *p,int n){(void)p;(void)n;}
static void k_mutex_unlock(unsigned *p){(void)p;}
static struct r2_capture now;
bool r2_capture_now(struct r2_capture *p){*p=now;return true;}
static struct rc5_rate_fit fit;
static struct rc5_clock local_clock;
static struct rc5_origin current_origin;
static uint32_t capture_epoch=1,fit_rms;
static bool fit_rms_valid;
static int32_t direct_phase_bias;
static unsigned starts,ends,unknown;
static struct rc5_routes routes={.count=2,.node={{.id=55},{.id=66}}};
static unsigned parent_index;
static struct rc5_state timing;
static struct rc_shadow_map shadow_map;
bool r2_shadow_enabled(void){return true;}
void r2_shadow_normal_map(const struct rc_shadow_map *m){shadow_map=*m;}
void r2_timing_event(unsigned k,int r,uint64_t p,uint32_t d,uint32_t a,uint32_t b){
 (void)p;(void)d;(void)a;(void)b;
 if(k==R2_TIMING_HOLD_START)starts++;
 if(k==R2_TIMING_HOLD_END){ends++;if(r)unknown++;}
}
#include "../src/holdover_service.inc"
int main(void){
 current_origin.utc_offset=1000000000000LL;
 struct rc5_state state={.bound=true,.context_until=UINT64_MAX,.root_boot=11,.root_epoch=1,.parent_boot=12,.parent_epoch=1};
 hold_reset(2);
 unsigned selected=0,continuous=0;
 for(unsigned i=1;i<=6400;i++){
  uint64_t tick=R2_CAPTURE_HZ+(uint64_t)i*1000000;
  int64_t bias=((int)(i%3)-1)*25;
  struct rc5_reference r={.valid=true,.rx=tick,.parent_tx_ns=(uint64_t)((int64_t)(tick*125/2)+bias),.completed_at=tick+16000,.channel=i%3};
  now=(struct r2_capture){.kind=R2_CAPTURE_NOW,.epoch=1,.tick=r.completed_at};
  state.reference=r;timing=state;
  hold_witness(&r);(void)rc5_rate_add(&fit,&r);fit_rms_valid=rc5_rate_rms(&fit,&fit_rms);
  struct rc5_rate rate={.ppb=fit.ppb};
  uint64_t before=0,e=0;bool had=hold_estimate.generation&&rc5_map_project(&hold_estimate,r.completed_at,&before,&e);
  if(rc5_clock_discipline(&local_clock,&state,&fit,&rate)){
   hold_prepare(&r);
   assert(hold_shadow_publish(now)&&shadow_map.local_hold&&shadow_map.normal_until==hold_estimate.effective+R2_HOLD_MAX_TICKS);
   if(had){uint64_t after;assert(rc5_map_project(&hold_estimate,r.completed_at,&after,&e)&&before==after);continuous++;}
   selected+=hold_view.selected;
  }
 }
 printf("owner selection=%u continuity=%u starts=%u ends=%u rates=%d/%d guards=%u/%u\n",selected,continuous,starts,ends,hold_rate.short_fit.rate,hold_rate.long_fit.rate,hold_rate.short_fit.guard,hold_rate.long_fit.guard);fflush(stdout);
 assert(selected&&continuous&&starts>100&&ends>100&&hold_rate.count<=65);
 assert(sizeof hold_rate+sizeof hold_estimate+sizeof hold_trial+sizeof hold_view+sizeof hold_serial+sizeof hold_resets<=1280);
 uint64_t utc;uint32_t age;assert(hold_read(&utc,&age)&&age<=2);
 assert(r2_dynamic_service_holdover_test(999)==-EINVAL);
 assert(!r2_dynamic_service_holdover_test(12000));hold_tick(now.tick);
 assert(hold_test_until&&hold_view.test[0]);
 struct rc5_map saved=hold_estimate;
 for(unsigned i=1;i<=193;i++){
  uint64_t tick=now.tick+(uint64_t)i*1000000;
  struct rc5_reference r={.valid=true,.rx=tick,.parent_tx_ns=tick*125/2,.completed_at=tick+16000,.channel=i%3};
  hold_witness(&r);
  if(i<192)assert(!memcmp(&saved,&hold_estimate,sizeof saved));
 }
 assert(!hold_test_until&&hold_test_count==192&&!hold_test_result&&hold_test_max<1000);
 char json[1600];assert(r2_dynamic_service_holdover_json(json,sizeof json)>0&&strstr(json,"BLE_RATE_ONLY_T096_V1"));
 uint64_t frozen_tick=now.tick;
 hold_trial[0]=(struct r2_hold_trial){.baseline=hold_estimate,.start=frozen_tick,.probe_ms=2000,.active=true};
 now.tick+=121*R2_CAPTURE_HZ;hold_tick(now.tick);
 assert(!hold_read(&utc,&age));assert(unknown&&hold_view.trial[0][2]==0&&hold_view.trial[1][2]==0);
 now.tick=frozen_tick;now.epoch=2;assert(!hold_read(&utc,&age));
 hold_reset(1);assert(!hold_read(&utc,&age)&&!hold_view.valid&&!hold_rate.count);
 printf("HOLD actual owner: selected=%u continuous=%u starts=%u ends=%u unknown=%u static=%zu PASS\n",selected,continuous,starts,ends,unknown,sizeof hold_rate+sizeof hold_estimate+sizeof hold_trial+sizeof hold_view+sizeof hold_serial+sizeof hold_resets);
}
