/* Real time-service loop, synthetic driver/capture boundaries only. */
#include "time_service.h"
#include "rc_shadow.h"
#include <assert.h>
#include <stdio.h>
static int64_t ticks=1000000,skew;
static uint64_t capture=16000000,capture_epoch=1;
static bool rc_profile;
struct k_spinlock {int unused;};typedef int k_spinlock_key_t;
static k_spinlock_key_t k_spin_lock(struct k_spinlock *l){(void)l;return 0;}
static void k_spin_unlock(struct k_spinlock *l,k_spinlock_key_t k){(void)l;(void)k;}
static int64_t k_uptime_ticks(void){return ticks;}
static int64_t k_ticks_to_us_floor64(int64_t n){return n;}
bool r2_switch_rc_active(void){return rc_profile;}
bool r2_gnss_capture_now(struct r2_capture *out){
 if(!capture)return false;
 *out=(struct r2_capture){capture,capture_epoch,R2_CAPTURE_NOW};ticks+=skew;return true;
}
void r2_gnss_observe(const uint8_t *b,size_t n,int64_t at){(void)b;(void)n;(void)at;}
int r2_gnss_start(void){return 0;}
void r2_gnss_stop(void){}
int r2_gnss_close_result(void){return 0;}
int r2_gnss_input_error(void){return 0;}
enum r2_association r2_gnss_association(void){return R2_ASSOC_UNKNOWN;}
void r2_gnss_poll(int64_t now){(void)now;}
bool r2_shadow_enabled(void){return false;}
void r2_shadow_reference(struct r2_capture p,const struct r2_clock *c){(void)p;(void)c;}
void r2_shadow_poll_normal(void){}
#include "../src/time_service.c"
/* Parser/servo itself is covered by clock_test; this fixture supplies a fresh
 * UNKNOWN raw source so the real owner loop can exercise sampling and fencing. */
static void fresh(void){
 clock_state.mapped=true;clock_state.wall=INT64_C(1791504000000000)+ticks;
 clock_state.last_tick=clock_state.last_good=clock_state.last_rmc=clock_state.last_pps=ticks;
 clock_state.last_rmc_utc=clock_state.wall;clock_state.run_rmc=3;
}
static void edge(void){fresh();r2_time_input_precise_pps((struct r2_capture){capture,capture_epoch,R2_CAPTURE_PPS},ticks);r2_time_service();}
int main(void){
 r2_time_init();r2_time_start();edge();assert(!source.utc_bound);
 ticks+=1000000;capture+=16000000;edge();assert(source.utc_bound);
 uint32_t epoch=clock_state.view.source_epoch;int64_t offset=source.utc_offset_ns;
 assert(clock_state.translated_epoch==epoch&&clock_state.view.association==R2_ASSOC_UNKNOWN);
 int64_t snapshot=clock_state.translated_utc_us;
 skew=1000;fresh();r2_time_service();assert(source.utc_offset_ns==offset&&clock_state.translated_utc_us==snapshot);
 assert(clock_state.view.source_epoch==epoch);skew=0;
 uint64_t saved=capture;capture=0;fresh();r2_time_service();assert(source.utc_offset_ns==offset&&clock_state.translated_utc_us==snapshot);capture=saved;
 /* Coordinate replacement must revoke old translated snapshots even if the
  * first coherent rebind is delayed by scheduling. */
 ticks+=1000000;capture+=16000000;capture_epoch=2;edge();
 assert(!source.utc_bound&&clock_state.translated_epoch==0);
 int64_t utc;assert(!r2_time_source_utc_now(epoch,&utc));
 ticks+=1000000;capture+=16000000;skew=1000;edge();
 assert(source.count&&!source.utc_bound&&clock_state.translated_epoch==0);
 assert(!r2_source_utc_read(&source,(struct r2_capture){capture,2,R2_CAPTURE_NOW},epoch,&utc));
 skew=0;fresh();r2_time_service();assert(source.utc_bound&&clock_state.view.source_epoch==epoch+1);
 assert(clock_state.translated_epoch==epoch+1&&!r2_source_utc_read(&source,(struct r2_capture){capture,2,R2_CAPTURE_NOW},epoch,&utc));
 assert(r2_time_source_utc_now(epoch+1,&utc));
 /* Expiry remains exactly the original physical last-PPS +1.5s. */
 capture=source.last_edge+24000000;assert(!r2_time_source_utc_now(epoch+1,&utc));
 /* RC profile never binds a fresh local UTC interpretation. */
 rc_profile=true;r2_time_init();r2_time_start();ticks+=1000000;capture+=16000000;edge();
 ticks+=1000000;capture+=16000000;edge();assert(source.count&&!source.utc_bound&&clock_state.translated_epoch==0);
 puts("PASS actual source owner: bounded/skewed/missing observations, delayed coordinate restart, epoch fence before new translation, exact expiry, UNKNOWN and RC isolation");
}
