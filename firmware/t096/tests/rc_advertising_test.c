/* MIT. Copyright (c) 2026 Jonathan Yantis. Actual precision and discovery gates. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "rc_unicast.h"
#include "rc_admission_policy.h"
#include "rc_transport_hold.h"
#include "timing_events.h"
#define RC_FIELD_HOLD_TICKS UINT64_C(8000000)
#define UC1_CLIENTS 2
#define BT_LE_EXT_ADV_START_DEFAULT 0
#define BT_HCI_ERR_REMOTE_USER_TERM_CONN 19
static bool parent_known,parent_synced,holdover_expired,capture_ok,tracking,observation_ok=true;
static struct {bool valid;} fit;
static uint64_t discipline_at,fake_tick,phase_rx;
static struct rc5_clock local_clock;
static struct rc5_state timing;
static struct rc5_origin current_origin;
static struct {struct {bool transmitter;} node[1];} routes;
static unsigned node;
bool rc5_clock_capture(const struct rc5_clock *c,const struct rc5_state *s,uint64_t t,struct rc5_stamp *st){
 (void)c;(void)s;(void)t;*st=(struct rc5_stamp){.ns=100,.pll_tracking=tracking};return capture_ok;
}
struct bt_conn {unsigned refs;};
static struct bt_conn *bt_conn_ref(struct bt_conn *c){++c->refs;return c;}
static void bt_conn_unref(struct bt_conn *c){assert(c->refs);--c->refs;}
static int bt_conn_disconnect(struct bt_conn *c,unsigned reason){(void)c;(void)reason;assert(false);return 0;}
struct session {unsigned control_seq,hz;};
struct uc_client {struct bt_conn *conn;bool dead,submitting,tx_busy,state_pending,command_pending,probe_pending;struct session session;uint64_t unauth_until;};
static struct {struct r2_uc_clock clock;struct uc_client clients[UC1_CLIENTS];void *adv;bool enabled,advertising;uint64_t adv_check_at;} u;
typedef int k_spinlock_key_t;
static int lock,job;
static int k_spin_lock(int *p){(void)p;return 0;}
static void k_spin_unlock(int *p,int k){(void)p;(void)k;}
static int k_work_busy_get(int *p){(void)p;return 0;}
static int k_work_submit(int *p){(void)p;return 0;}
static bool rc_owner(void){return true;}
static uint64_t now_us(void){return fake_tick/16;}
static bool uc1_live(struct session *s,uint64_t now){(void)s;(void)now;return true;}
static void *lt_adv=(void*)1;
static bool lt_paused,lt_advertising;
static uint64_t lt_adv_check_at;
static unsigned lt_adv_recoveries,starts,stops,hints;
static int stop_error,fault;
#define atomic_get(p) (*(p))
static void check(bool ok,const char *why){(void)why;if(!ok)fault=-EIO;}
static int bt_le_ext_adv_start(void *p,int parameters){assert(p&&parameters==0);++starts;return 0;}
static int bt_le_ext_adv_stop(void *p){assert(p);++stops;return stop_error;}
static void slot_hint_update(void){++hints;}
struct r2_capture {uint64_t tick;};
static bool r2_capture_now(struct r2_capture *c){c->tick=fake_tick;return observation_ok;}
static void *pa=(void*)3;
static bool running,start_prepared,awaiting_first_tx,slot_move_pending,phase_admitted,lt_acquiring;
static uint64_t requested,slot_move_until,restart_at,usable_tx_from;
static unsigned train=1,sent_count,periodic_starts,periodic_stops,tx=1;
static struct rc_transport_hold downstream_hold;
static struct {unsigned count;} published;
static unsigned count0_published;
static void publish(void){published.count=0;++count0_published;}
static struct rc5_demand lt_demand;
bool rc5_demand_live(const struct rc5_demand *d,uint64_t now){return d->deadline&&now<d->deadline;}
static int bt_le_per_adv_start(void *p){assert(p);++periodic_starts;return 0;}
static int bt_le_per_adv_stop(void *p){assert(p);++periodic_stops;return 0;}
#include "rc_advertising_under_test.h"
static void good(void){routes.node[0].transmitter=true;parent_known=parent_synced=fit.valid=capture_ok=tracking=true;holdover_expired=false;discipline_at=100;fake_tick=200;current_origin.root=11;current_origin.boot=timing.root_boot=12;current_origin.epoch=timing.root_epoch=3;current_origin.expires=101;timing.bound=true;timing.reference.valid=true;timing.context_until=UINT64_MAX;lt_demand.deadline=UINT64_MAX;}
static void transport_hold_cases(void){
 const uint64_t first_tick=fake_tick;
 phase_admitted=true;sent_count=2;published.count=2;
 assert(downstream_ready(fake_tick));
 parent_synced=false;
 assert(advertising_withdraw(fake_tick)&&running&&train==1);
 assert(!sent_count&&!published.count&&count0_published==1&&!downstream_ready(fake_tick));
 assert(usable_tx_from==fake_tick);
 const uint64_t deadline=first_tick+RC_TRANSPORT_HOLD_TICKS;
 assert(downstream_hold.deadline==deadline);
 // Repeated owner polls used to stop/start the train on each loss. Ten
 // thousand1ms good/bad pairs retain the actual established owner, train and
 // original child lease; sub2s lock flicker never rearms the hold deadline.
 const unsigned old_starts=periodic_starts,old_stops=periodic_stops;
 const struct rc5_demand original_demand=lt_demand;
 for(unsigned i=1;i<=10000;i++){
  fake_tick=first_tick+(uint64_t)(i*2-1)*16000;discipline_at=fake_tick;parent_synced=true;
  assert(publication_demand_poll(fake_tick)&&advertising_withdraw(fake_tick));
  parent_synced=false;fake_tick+=16000;
  assert(publication_demand_poll(fake_tick)&&advertising_withdraw(fake_tick));
  // This is the actual worker's start eligibility, including its !running.
  if(!running&&precision_synced(fake_tick))start();
  assert(running&&train==1&&periodic_starts==old_starts&&periodic_stops==old_stops);
  assert(downstream_hold.deadline==deadline&&!downstream_ready(fake_tick));
  assert(lt_demand.deadline==original_demand.deadline&&lt_demand.highwater==original_demand.highwater);
 }
 for(unsigned i=21;i<30;i++){
  fake_tick=first_tick+(uint64_t)i*16000000;discipline_at=fake_tick;parent_synced=true;
  assert(advertising_withdraw(fake_tick)&&running&&downstream_hold.deadline==deadline);
  parent_synced=false;assert(advertising_withdraw(++fake_tick)&&downstream_hold.deadline==deadline);
 }
 fake_tick=deadline;assert(advertising_withdraw(fake_tick)&&!running&&train==2&&periodic_stops==1);
 assert(advertising_withdraw(fake_tick)&&train==2);
 // Startup remains precision-only, even when asked inside a former hold.
 start();assert(!running&&periodic_starts==1);
 good();start();assert(running);parent_synced=false;assert(advertising_withdraw(fake_tick));
 for(unsigned i=0;i<=8;i++){
  fake_tick=1000+(uint64_t)i*RC_TRANSPORT_OBSERVATION_TICKS;discipline_at=fake_tick;parent_synced=true;
  assert(advertising_withdraw(fake_tick)&&running);
 }
 assert(!downstream_hold.deadline); // Actual2s continuously observed recovery.
 parent_synced=false;assert(advertising_withdraw(++fake_tick));
 assert(downstream_hold.deadline==fake_tick+RC_TRANSPORT_HOLD_TICKS);
 // Original demand, scope and failed owner drains override transport hold.
 lt_demand.deadline=fake_tick/16;
 assert(publication_demand_poll(fake_tick)&&!running&&train==3&&!downstream_hold.armed);
 good();start();timing.reference.valid=false;
 assert(advertising_withdraw(fake_tick)&&!running&&train==4);
 good();start();current_origin.expires=100;published.count=2;sent_count=2;
 assert(advertising_withdraw(fake_tick)&&running&&!published.count&&!downstream_ready(fake_tick));
 stop_error=-EIO;assert(!stop()&&running&&fault==-EIO);
 stop_error=0;fault=0;assert(stop()&&!running&&!downstream_hold.armed);
 good();start();++timing.root_epoch;
 unsigned before_train=train;assert(advertising_withdraw(fake_tick)&&!running&&train==before_train+1);
 good();start();observation_ok=false;before_train=train;
 assert(advertising_withdraw(fake_tick)&&!running&&train==before_train+1);observation_ok=true;
 // Checked counter reversal/overflow refuses retention, never wraps a lease.
 struct rc_transport_hold h={0};assert(!rc_transport_hold_update(&h,1,false,false));
 assert(rc_transport_hold_update(&h,2,true,true));
 assert(!rc_transport_hold_update(&h,1,true,false)&&!h.armed);
 assert(rc_transport_hold_update(&h,UINT64_MAX-1,true,true));
 assert(!rc_transport_hold_update(&h,UINT64_MAX,true,false)&&!h.armed);
 // A>250ms observation gap restarts stable recovery, not the loss deadline.
 assert(rc_transport_hold_update(&h,1,true,true));assert(rc_transport_hold_update(&h,2,true,false));
 const uint64_t original=h.deadline;
 assert(rc_transport_hold_update(&h,3,true,true));
 assert(rc_transport_hold_update(&h,RC_TRANSPORT_RECOVERY_TICKS+4,true,true));
 assert(h.deadline==original&&h.recovered==RC_TRANSPORT_RECOVERY_TICKS+4);
 assert(stop());running=false;train=2;periodic_starts=1;periodic_stops=1;count0_published=0;
}
static void refused(void){assert(!precision_synced(fake_tick));lt_advertise(fake_tick);assert(lt_advertising);unsigned n=starts;assert(!r2_uc_poll(&(struct r2_uc_clock){0},false)&&!u.advertising&&starts==n);}
int main(void){
 (void)publish; // Remains referenced when running the old-withdraw negative.
 u.enabled=true;u.adv=(void*)2;good();assert(precision_synced(fake_tick));
 /* RX can complete after the loop's capture: a stale caller tick must not
  * withdraw good timing, and an actually expired fresh capture must refuse. */
 discipline_at=150;assert(precision_synced(100));
 fake_tick=discipline_at+RC_FIELD_HOLD_TICKS;assert(!precision_synced(100));good();
 parent_synced=false;start();assert(!running&&!periodic_starts&&!starts);
 good();start();assert(running&&periodic_starts==1&&starts==1);
 assert(advertising_withdraw(fake_tick)&&running);
 transport_hold_cases();starts=stops=hints=0;good();
 /* No downstream demand/publication is required to discover a synced relay. */
 lt_advertise(fake_tick);assert(lt_advertising&&starts==1&&hints==1);
 assert(!r2_uc_poll(&(struct r2_uc_clock){0},true)&&u.advertising&&starts==2);
 parent_synced=false;refused();assert(stops==1);refused();assert(stops==1);
 good();parent_known=false;refused();good();fit.valid=false;refused();good();capture_ok=false;refused();
 good();tracking=false;refused();good();holdover_expired=true;refused();good();discipline_at=0;refused();
 good();timing.context_until=fake_tick;refused();good();timing.bound=false;refused();
 good();fake_tick=99;refused();good();fake_tick=discipline_at+RC_FIELD_HOLD_TICKS;refused();
 good();current_origin.expires=100;refused();
 good();lt_advertise(fake_tick);assert(lt_advertising&&starts==2);
 struct bt_conn client={.refs=1};u.clients[0].conn=&client;u.clients[0].session.control_seq=1;
 assert(!r2_uc_poll(&(struct r2_uc_clock){0},true)&&u.advertising&&starts==3);
 /* Withdrawing discovery preserves an existing authenticated client's state. */
 assert(!r2_uc_poll(&(struct r2_uc_clock){0},false)&&client.refs==1&&!u.advertising);
 assert(!r2_uc_poll(&(struct r2_uc_clock){0},true)&&u.advertising);
 stop_error=-EIO;assert(r2_uc_poll(&(struct r2_uc_clock){0},false)==-EIO&&u.advertising);
 stop_error=-EALREADY;assert(!r2_uc_poll(&(struct r2_uc_clock){0},false)&&!u.advertising);
 for(unsigned attempt=0;attempt<8;++attempt){
  uint64_t lo=rc_admission_retry_us(attempt,0),hi=rc_admission_retry_us(attempt,UINT64_MAX);
  assert(lo>=2000000&&hi>=lo&&hi<=lo+lo/2);
 }
 uint64_t start_history[2]={1,UINT64_C(320000001)};
 assert(!rc_admission_start_allowed(start_history,UINT64_C(640000001)));
 assert(rc_admission_start_allowed(start_history,UINT64_C(960000001)));
 assert(!rc_admission_start_allowed(start_history,UINT64_C(320000000)));
 start_history[0]=0;assert(!rc_admission_start_allowed(start_history,UINT64_C(640000000)));
 assert(rc_admission_start_allowed(start_history,UINT64_C(640000001)));
 /* Mesh admission is unavailable to a leaf, independently of a good clock.
  * Ordinary receiver timing/UCS clients remain valid and are not withdrawn. */
 good();routes.node[0].transmitter=true;lt_advertising=false;lt_paused=false;
 lt_advertise(fake_tick);assert(lt_advertising);
 routes.node[0].transmitter=false;lt_advertise(fake_tick);
 assert(!lt_advertising&&precision_synced(fake_tick));
 unsigned before=starts;lt_advertise(fake_tick);assert(starts==before);
 routes.node[0].transmitter=true;lt_advertise(fake_tick);assert(lt_advertising);
 assert(rc_admission_drain_us(false,3200)==500000&&rc_admission_drain_us(true,100)==1500000&&rc_admission_drain_us(true,3200)==32500000);
 puts("PASS production RC/UCS discovery: management discovery during timing loss, all output precision gates, withdrawal/recovery, idle-relay discovery, existing client preservation and failed stop");
}
