/* Actual owner: an IRQ arrives after polling sampled time. */
#include <assert.h>
#include <stdio.h>
#include "time_service.h"
/* Diagnostic shadow is disabled at this owner boundary. */
bool r2_shadow_enabled(void){return false;}
void r2_shadow_poll_normal(void){}
void r2_shadow_reference(struct r2_capture c,const struct r2_clock *s){(void)c;(void)s;}
static int64_t ticks=1000000;
static bool inject;
static uint64_t current_capture;
bool r2_gnss_capture_now(struct r2_capture *out){
 if(!current_capture)return false;
 *out=(struct r2_capture){current_capture,1,R2_CAPTURE_NOW};return true;
}
int64_t k_uptime_get(void){return ticks/1000;}
static int64_t k_uptime_ticks(void){return ticks;}
static int64_t k_ticks_to_us_floor64(int64_t n){return n;}
void r2_gnss_observe(const uint8_t *b,size_t n,int64_t at){}
int r2_gnss_start(void){return 0;}
void r2_gnss_stop(void){}
int r2_gnss_close_result(void){return 0;}
int r2_gnss_input_error(void){return 0;}
enum r2_association r2_gnss_association(void){return R2_ASSOC_UNKNOWN;}
void r2_gnss_poll(int64_t now){if(inject){inject=false;ticks+=31;r2_time_input_pps(ticks);}}
#include "../src/time_service.c"
int main(void){
 r2_time_init();r2_time_start();inject=true;r2_time_service();
 struct r2_clock c;r2_time_copy(&c);assert(c.view.pps_edges==1&&!c.view.faults);
 ticks+=R2_INPUT_MAX_AGE_US;r2_time_input_pps(ticks-R2_INPUT_MAX_AGE_US);r2_time_service();
 r2_time_copy(&c);assert(c.view.reason==R2_INPUT_STALE&&c.view.faults==1);
 r2_time_init();r2_time_start();uint64_t capture=100000000;
 for(unsigned i=0;i<241;i++){
  ticks=10000000+(int64_t)i*1000000+(i%3)*100;
  r2_time_input_precise_pps((struct r2_capture){capture,1,R2_CAPTURE_PPS},ticks);
  ticks+=i%7*1000;current_capture=capture+160000;r2_time_service();capture+=16000160;
 }
 struct r2_pps_precision_view p;r2_time_precision_snapshot(&p);
 assert(p.fit_valid&&p.drift_ppb==10000&&p.fit_rms_ns==0);
 uint64_t ns,generation;
 assert(r2_time_source_read((struct r2_capture){current_capture,1,R2_CAPTURE_RADIO},&ns,&generation));
 assert(generation==240);
 struct r2_source_pps_view causal;r2_time_source_pps_snapshot(&causal);
 assert(causal.valid&&causal.epoch==1&&causal.observations==241&&causal.generation==239&&causal.map_from<causal.tick);
 assert(causal.phase_ns>=-5&&causal.phase_ns<=5); /* Raw source phase, no demeaning. */
 assert(!r2_time_source_read((struct r2_capture){current_capture,2,R2_CAPTURE_RADIO},&ns,&generation));
 ticks+=1500000;r2_time_precision_snapshot(&p);assert(!p.fit_valid&&!p.rate_valid);r2_time_source_pps_snapshot(&causal);assert(!causal.valid);
 puts("time owner IRQ-after-poll accepted; genuinely stale edge still refused");
}
