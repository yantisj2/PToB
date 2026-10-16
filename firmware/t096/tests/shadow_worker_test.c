#include "rc_shadow.h"
#include "capture.h"
#include "radio_precision.h"
#include "rc_direct_calibration.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct k_spinlock {int held;};typedef int k_spinlock_key_t;
static int k_spin_lock(struct k_spinlock *s){assert(!s->held);s->held=1;return 0;}
static void k_spin_unlock(struct k_spinlock *s,int key){assert(s->held);s->held=0;}
static uint64_t now_tick,now_us;
static uint64_t k_uptime_ticks(void){return now_us;}
static uint64_t k_ticks_to_us_floor64(uint64_t ticks){return ticks;}
bool r2_capture_now(struct r2_capture *c){*c=(struct r2_capture){now_tick,1,R2_CAPTURE_NOW};return true;}
#define R2_SHADOW_TEST_BOTH 1
typedef int atomic_t;
static int atomic_get(atomic_t *v){return *v;}
static int atomic_set(atomic_t *v,int n){int old=*v;*v=n;return old;}
#include "../src/rc_shadow.c"
int main(void){
 struct r2_radio_precision owner={.scope={.boot=9,.source_epoch=3,.root_identity=1,.until_us=3000000,.follower=true},.servo={.output={16000000,1000000100-RC_DIRECT_PPS_CORRECTION_NS,0,0,true}}};
 r2_servo_map_t delivered;assert(r2_radio_precision_map(&owner,&delivered)&&delivered.anchor-owner.servo.output.anchor==RC_DIRECT_PPS_CORRECTION_NS);
 struct rc_shadow_map m={.normal=true,.normal_map=delivered,.normal_until=40000000,
  .map={.effective=16000000,.generation=1},.lo=16001000,.epoch=1,.root=1,.root_boot=9,.parent=1,.parent_boot=9,.root_epoch=3,.utc_offset=1000000000000LL};
 now_tick=16002000;now_us=1000;r2_shadow_normal_map(&m);
 struct r2_capture e={32000000,1,R2_CAPTURE_PPS};r2_shadow_edge(e,1000);r2_shadow_poll();
 struct r2_clock c={.view={.fix_active=true,.pps_present=true,.association=R2_ASSOC_UNKNOWN}};r2_shadow_reference(e,&c);
 struct rc_shadow_record r;struct rc_shadow_status st;now_us=800999;assert(!r2_shadow_read(0,&r,&st));
 now_us=801000;assert(r2_shadow_read(0,&r,&st)&&r.valid&&r.phase_ns==100&&r.reference_seen&&r.reference_fix&&r.reference_pps&&r.map.normal);
 assert(r.utc_ns==1002000000100LL);
 /* A later map cannot improve an already frozen physical edge. */
 m.normal_map.anchor+=500;m.lo=32001000;now_tick=32002000;r2_shadow_normal_map(&m);assert(r2_shadow_read(0,&r,&st)&&r.phase_ns==100);
 e.tick=40000000;r2_shadow_edge(e,1000);r2_shadow_poll();assert(r2_shadow_read(1,&r,&st)&&!r.valid&&r.reason==RC_SHADOW_EXPIRED);
 r2_shadow_normal_withdraw();assert(history.count==0);
 owner.scope.follower=false;owner.scope.gm=true;assert(!r2_radio_precision_map(&owner,&delivered));
 struct rc5_clock rc={.head=1,.count=1,.map={{.effective=24001000,.anchor_tick=24000000,.anchor_ns=1500000123,.generation=2,.continuous=true}}};
 struct rc5_state path={.routes={.count=2,.node={{.id=1},{.id=2,.parent=0}}},.self=1,.parent_boot=9,.root_boot=9,.root_epoch=3,.parent_epoch=1,.reference={.origin={.root=1,.utc_offset=1000000000000LL}}};
 now_tick=24002000;r2_shadow_map(&rc,&path,24001000,1);e.tick=32000001;r2_shadow_edge(e,1000);r2_shadow_poll();assert(r2_shadow_read(2,&r,&st)&&r.valid&&r.phase_ns>=185&&r.phase_ns<=186&&!r.map.normal);
 e.tick=24001000;r2_shadow_edge(e,1000);r2_shadow_poll();assert(r2_shadow_read(3,&r,&st)&&r.reason==RC_SHADOW_ACTIVATION_RACE);
 r2_shadow_serving(true);assert(!r2_shadow_enabled());r2_shadow_enable(true);assert(!r2_shadow_enabled());
 unsigned old_edges=stats.edges;e.tick+=16000000;r2_shadow_edge(e,1000);r2_shadow_poll();assert(stats.edges==old_edges&&!qcount&&!history.count);
 r2_shadow_serving(false);assert(r2_shadow_enabled());r2_shadow_enable(false);assert(!r2_shadow_enabled());
 puts("PASS actual normal/RC witness worker: immutable causal maps, metadata deferral, normal1.5s/RC500ms expiry, withdraw and raw PPS association; no witness feedback");
}
