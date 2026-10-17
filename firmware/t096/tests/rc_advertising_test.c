/* MIT. Copyright (c) 2026 Jonathan Yantis. Actual precision and discovery gates. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "rc_unicast.h"
#include "rc_admission_policy.h"
#define RC_FIELD_HOLD_TICKS UINT64_C(8000000)
#define UC1_CLIENTS 2
#define BT_LE_EXT_ADV_START_DEFAULT 0
#define BT_HCI_ERR_REMOTE_USER_TERM_CONN 19
static bool parent_known,parent_synced,holdover_expired,capture_ok,tracking;
static struct {bool valid;} fit;
static uint64_t discipline_at,fake_tick;
static struct rc5_clock local_clock;
static struct rc5_state timing;
static struct rc5_origin current_origin;
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
static int stop_error;
static void check(bool ok,const char *why){(void)why;assert(ok);}
static int bt_le_ext_adv_start(void *p,int parameters){assert(p&&parameters==0);++starts;return 0;}
static int bt_le_ext_adv_stop(void *p){assert(p);++stops;return stop_error;}
static void slot_hint_update(void){++hints;}
struct r2_capture {uint64_t tick;};
static bool r2_capture_now(struct r2_capture *c){c->tick=fake_tick;return true;}
static void *pa=(void*)3;
static bool running,start_prepared,awaiting_first_tx,slot_move_pending,phase_admitted,lt_acquiring;
static uint64_t requested,slot_move_until,restart_at;
static unsigned train=1,sent_count,periodic_starts,periodic_stops;
static int bt_le_per_adv_start(void *p){assert(p);++periodic_starts;return 0;}
static int bt_le_per_adv_stop(void *p){assert(p);++periodic_stops;return 0;}
#include "rc_advertising_under_test.h"
static void good(void){parent_known=parent_synced=fit.valid=capture_ok=tracking=true;holdover_expired=false;discipline_at=100;fake_tick=200;current_origin.expires=101;timing.bound=true;timing.context_until=UINT64_MAX;}
static void refused(void){assert(!precision_synced(fake_tick));unsigned n=starts;lt_advertise(fake_tick);assert(!lt_advertising&&starts==n);assert(!r2_uc_poll(&(struct r2_uc_clock){0},false)&&!u.advertising&&starts==n);}
int main(void){
 u.enabled=true;u.adv=(void*)2;good();assert(precision_synced(fake_tick));
 /* RX can complete after the loop's capture: a stale caller tick must not
  * withdraw good timing, and an actually expired fresh capture must refuse. */
 discipline_at=150;assert(precision_synced(100));
 fake_tick=discipline_at+RC_FIELD_HOLD_TICKS;assert(!precision_synced(100));good();
 parent_synced=false;start();assert(!running&&!periodic_starts&&!starts);
 good();start();assert(running&&periodic_starts==1&&starts==1);
 assert(advertising_withdraw(fake_tick)&&running);
 parent_synced=false;assert(advertising_withdraw(fake_tick)&&!running&&periodic_stops==1&&train==2);
 assert(advertising_withdraw(fake_tick)&&train==2);starts=stops=hints=0;good();
 /* No downstream demand/publication is required to discover a synced relay. */
 lt_advertise(fake_tick);assert(lt_advertising&&starts==1&&hints==1);
 assert(!r2_uc_poll(&(struct r2_uc_clock){0},true)&&u.advertising&&starts==2);
 parent_synced=false;refused();assert(stops==2);refused();assert(stops==2);
 good();parent_known=false;refused();good();fit.valid=false;refused();good();capture_ok=false;refused();
 good();tracking=false;refused();good();holdover_expired=true;refused();good();discipline_at=0;refused();
 good();timing.context_until=fake_tick;refused();good();timing.bound=false;refused();
 good();fake_tick=99;refused();good();fake_tick=discipline_at+RC_FIELD_HOLD_TICKS;refused();
 good();current_origin.expires=100;refused();
 good();lt_advertise(fake_tick);assert(lt_advertising&&starts==3);
 struct bt_conn client={.refs=1};u.clients[0].conn=&client;u.clients[0].session.control_seq=1;
 assert(!r2_uc_poll(&(struct r2_uc_clock){0},true)&&u.advertising&&starts==4);
 /* Withdrawing discovery preserves an existing authenticated client's state. */
 assert(!r2_uc_poll(&(struct r2_uc_clock){0},false)&&client.refs==1&&!u.advertising);
 assert(!r2_uc_poll(&(struct r2_uc_clock){0},true)&&u.advertising);
 stop_error=-EIO;assert(r2_uc_poll(&(struct r2_uc_clock){0},false)==-EIO&&u.advertising);
 stop_error=-EALREADY;assert(!r2_uc_poll(&(struct r2_uc_clock){0},false)&&!u.advertising);
 for(unsigned attempt=0;attempt<8;++attempt){
  uint64_t lo=rc_admission_retry_us(attempt,0),hi=rc_admission_retry_us(attempt,UINT64_MAX);
  assert(lo>=2000000&&hi>=lo&&hi<=lo+lo/2);
 }
 uint64_t starts[2]={1,UINT64_C(320000001)};
 assert(!rc_admission_start_allowed(starts,UINT64_C(640000001)));
 assert(rc_admission_start_allowed(starts,UINT64_C(960000001)));
 assert(!rc_admission_start_allowed(starts,UINT64_C(320000000)));
 starts[0]=0;assert(!rc_admission_start_allowed(starts,UINT64_C(640000000)));
 assert(rc_admission_start_allowed(starts,UINT64_C(640000001)));
 assert(rc_admission_drain_us(false,3200)==500000&&rc_admission_drain_us(true,100)==1500000&&rc_admission_drain_us(true,3200)==32500000);
 puts("PASS production RC/UCS discovery: cold refusal, all precision gates, withdrawal/recovery, idle-relay discovery, existing client preservation and failed stop");
}
