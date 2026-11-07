/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ble_service.h"
#include "clock.h"
static int64_t now_us;
static int64_t k_uptime_get(void){return now_us/1000;}
int64_t r2_time_now_us(void){return now_us;}
static struct {char unit[8];}identity={"TC-01"};
static int r2_watchdog_reset_kind(void){return 0;}
struct k_spinlock{int unused;};typedef int k_spinlock_key_t;
static struct k_spinlock lock;static uint32_t drops;static enum r2_clock_fault pending;
static int input_error;int r2_gnss_input_error(void){return input_error;}
static int k_spin_lock(struct k_spinlock *l){(void)l;return 0;}
static void k_spin_unlock(struct k_spinlock *l,int k){(void)l;(void)k;}
#include "time_ui_guard_under_test.h"
static struct r2_ui_model published;static unsigned publications;
void r2_ui_publish_owned(const struct r2_ui_model *m,uint64_t emitter,const struct r2_ui_label_ids *labels){
 (void)labels;assert(!emitter);published=*m;++publications;}
void r2_time_precision_snapshot(struct r2_pps_precision_view *out){*out=(struct r2_pps_precision_view){.epoch=9,.age_us=123};}
void r2_service_ui_logic(struct r2_ui_model *m,const struct r2_service_status *s){
 /* Test boundary: actual backend projection has independent native/legacy
  * proof. Here verify the main binding consumes this exact owned status. */
 assert(s->profile_incarnation==123);
 m->role=R2_UI_FOLLOWER;m->ble_time_selected=true;m->clock.valid=s->follower_clock_valid;
 m->ble_valid_until_us=s->service_until;
}
bool r2_time_source_utc_now(uint32_t epoch,int64_t *utc){(void)epoch;(void)utc;assert(0);return false;}
#include "main_logic_under_test.h"
int main(void){
 struct r2_clock local;r2_clock_init(&local,0,R2_ASSOC_UNKNOWN);
 local.last_rmc=local.last_pps=0;local.run_rmc=3;local.view.fix_active=local.view.pps_present=true;
 local.view.gps_age_us=local.view.pps_age_us=0;
 struct r2_service_status s={.profile_incarnation=123,.follower_clock_valid=true,.service_until=7000000};
 now_us=3000000;drops=19;input_error=-5;pending=R2_INPUT_OVERFLOW;
 gm7_ui_logic(0,true,true,&s,&local);
 assert(publications==1&&published.sampled_ms==3000&&published.queue_drops==19);
 assert(published.pps_precision.epoch==9&&published.pps_precision.age_us==123);
 assert(published.clock.input_error==-5&&published.clock.reason==R2_INPUT_OVERFLOW);
 assert(!published.clock.fix_active&&!published.clock.source_available);
 assert(!published.clock.pps_present&&published.clock.pps_age_us==3000000);
 assert(published.ble_valid_until_us==s.service_until); /* no lease extension */
 pending=R2_INPUT_OK;now_us=3020000;gm7_ui_logic(3000,true,true,&s,&local);
 assert(published.clock.pps_age_us==3020000&&published.clock.mono_us==3020000);
 assert(published.ble_valid_until_us==7000000);
 puts("PASS actual main binding: old local ages advance; pending/input_error/drops retained; owner deadline unchanged");
}
