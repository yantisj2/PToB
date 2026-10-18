/* Production close/reset bodies with controlled asynchronous BLE boundaries. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define R2_TIMING_EVENTS_TEST 1
#include "timing_events.h"
#include "rc_admission_policy.h"
#define CONFIG_BT_MAX_CONN 6
#define K_NO_WAIT 0
#define K_MSEC(x) (x)
#define BT_HCI_ERR_REMOTE_USER_TERM_CONN 0x13
#define BT_HCI_ERR_CONN_LIMIT_EXCEEDED 9
#define BT_ATT_ERR_UNLIKELY 14
#define BT_ATT_ERR_INSUFFICIENT_RESOURCES 17
struct bt_conn {unsigned refs;bool active,initiating,ucs,scheduled;unsigned at;};
enum {BT_CONN_ROLE_CENTRAL,BT_CONN_ROLE_PERIPHERAL};
enum {BT_CONN_STATE_DISCONNECTED,BT_CONN_STATE_CONNECTING,BT_CONN_STATE_CONNECTED,BT_CONN_STATE_DISCONNECTING};
struct bt_conn_info {unsigned role,state;struct {unsigned timeout;} le;};
static int bt_conn_get_info(struct bt_conn *c,struct bt_conn_info *i){*i=(struct bt_conn_info){.role=BT_CONN_ROLE_CENTRAL,.state=c->initiating?BT_CONN_STATE_CONNECTING:c->active?BT_CONN_STATE_CONNECTED:BT_CONN_STATE_DISCONNECTED,.le={.timeout=100}};return 0;}
struct rc5_gm_context {int v;};struct r2_join_exchange {int v;};struct rc5_demand {int v;};
static struct rc5_gm_context gm_context;static struct r2_join_exchange gm_exchange;static struct rc5_demand lt_demand;
static uint32_t gm_admission_diagnostic[7];
static uint64_t lt_attempt_started,lt_close_until,gm_sequence,lt_challenge,lt_deadline,lt_renew_at,lt_request_at,lt_next,lt_budget_until,lt_attempt_ready,lt_adv_check_at,last_context_progress;
static unsigned lt_sequence,lt_requests,lt_responses,lt_bindings,lt_rejects,lt_attempts,lt_initiations,lt_disconnects,lt_adv_recoveries,lt_read_size,lt_handle,lt_phase,lt_cap_retries;
static bool startup_admitted;
static bool parent_relay,lt_paused,lt_acquiring,lt_advertising,lt_dead,lt_server,lt_server_pending,lt_reply_ready,gm_write_busy;
static void *lt_adv;static struct bt_conn *lt_conn;enum {LT_IDLE,LT_CONNECT,LT_MTU,LT_CLOSE,LT_CAP,LT_REPLY};
static bool parent_known;static unsigned parent_index;
static struct {struct {uint64_t id;} node[2];} routes;
struct rc7_control {int unused;};
static struct rc7_control capability(void){return (struct rc7_control){0};}
static void lt_publish_reply(const struct rc7_control *c){(void)c;assert(false);}
enum {LT_CONNECTED,LT_DISCONNECTED,LT_MTU_DONE,LT_READ};
struct lt_event {unsigned type;struct bt_conn *conn;unsigned error;};
static struct {struct lt_event event[4];unsigned count;} lt_events;
static struct bt_conn pool[CONFIG_BT_MAX_CONN];static struct bt_conn *arrival;
static unsigned now,delay_ms,disconnect_calls,records;static bool delete_fails,advertising_live,cancel_race,late_success,fault;
static unsigned drain_events;static int drain_result;
void r2_timing_event(unsigned k,int r,uint64_t p,uint32_t d,uint32_t a,uint32_t b){(void)p;(void)d;(void)a;(void)b;if(k==R2_TIMING_RC_DRAIN){++drain_events;drain_result=r;}}
static void check(bool ok,const char *why){(void)why;if(!ok)fault=true;}
static uint64_t mono_us(void){return (uint64_t)now*1000;}
static uint64_t lt_now(void){return (uint64_t)now*16000;}
static uint64_t lt_random(void){return 0;}
static bool live_precision;
static bool precision_synced(uint64_t tick){(void)tick;return live_precision;}
static struct {bool bound;uint64_t context_until;} timing;
static void put(unsigned type,struct bt_conn *c,unsigned error){assert(lt_events.count<4);lt_events.event[lt_events.count++]=(struct lt_event){type,c,error};}
static int k_msgq_get(void *q,struct lt_event *e,int wait){(void)q;(void)wait;if(!lt_events.count)return -EAGAIN;*e=lt_events.event[0];memmove(lt_events.event,lt_events.event+1,--lt_events.count*sizeof *e);return 0;}
static unsigned k_msgq_num_used_get(void *q){(void)q;return lt_events.count;}
static uint64_t k_uptime_get(void){return now;}
static void k_sleep(unsigned ms){now+=ms;for(unsigned i=0;i<CONFIG_BT_MAX_CONN;++i){struct bt_conn *c=&pool[i];if(c->scheduled&&now>=c->at){c->scheduled=false;if(c->initiating){c->initiating=false;c->active=late_success;++c->refs;put(LT_CONNECTED,c,late_success?0:2);late_success=false;}else{c->active=false;put(LT_DISCONNECTED,c,0);}}}}
static void bt_conn_unref(struct bt_conn *c){assert(c->refs);--c->refs;}
static struct bt_conn *bt_conn_ref(struct bt_conn *c){++c->refs;return c;}
static bool r2_uc_owns(struct bt_conn *c){return c->ucs;}
static int bt_conn_disconnect(struct bt_conn *c,unsigned reason){assert(reason==0x13&&!advertising_live);assert(!c->ucs);++disconnect_calls;c->scheduled=true;c->at=now+delay_ms;if(c->initiating&&cancel_race){cancel_race=false;return -EACCES;}return 0;}
static void lt_record(unsigned event,int result,unsigned phase){(void)phase;assert((event==2||event==4)&&(result==0||result==2||result==-EACCES));++records;}
static int bt_le_ext_adv_stop(void *adv){assert(adv);advertising_live=false;if(arrival){++arrival->refs;arrival->active=true;put(LT_CONNECTED,arrival,0);arrival=NULL;}return 0;}
static int bt_le_ext_adv_delete(void *adv){assert(adv&&!advertising_live);return delete_fails?-EIO:0;}
#include "rc_connection_lifecycle_under_test.h"
static void seed(void){
 memset(pool,0,sizeof pool);memset(lt_retired,0,sizeof lt_retired);memset(&lt_events,0,sizeof lt_events);
 lt_conn=NULL;lt_adv=(void*)1;lt_dead=false;lt_advertising=advertising_live=true;lt_retirement_error=false;
 arrival=NULL;now=0;delay_ms=2;disconnect_calls=records=0;delete_fails=false;
 lt_close_until=0;lt_phase=LT_IDLE;cancel_race=late_success=fault=false;drain_events=0;drain_result=0;
 lt_attempts=lt_cap_retries=0;parent_relay=lt_server=false;
 startup_admitted=false;live_precision=false;timing.bound=false;timing.context_until=0;
}
static void queue_connected(unsigned slot){pool[slot].refs=1;pool[slot].active=true;put(LT_CONNECTED,&pool[slot],0);}
static void balanced(void){assert(!lt_events.count&&!lt_adv);for(unsigned i=0;i<CONFIG_BT_MAX_CONN;++i)assert(!pool[i].refs&&!pool[i].active&&!pool[i].initiating&&!lt_retired[i].conn);assert(lt_reset());}
int main(void){
 /* A busy initial relay capability is retried on the same live ACL, with a
  * finite count and the ORIGINAL transaction deadline. All other errors close. */
 seed();pool[0].active=true;lt_conn=&pool[0];parent_relay=true;lt_phase=LT_CAP;
 lt_deadline=32000000;uint64_t cap_deadline=lt_deadline;
 struct lt_event cap={.type=LT_READ,.conn=lt_conn,.error=BT_ATT_ERR_UNLIKELY};
 for(unsigned n=0;n<3;n++){
  assert(lt_retry_cap_error(&cap)&&lt_cap_retries==n+1&&lt_next==lt_now()+800000&&lt_deadline==cap_deadline);
  now+=50;
 }
 assert(!lt_retry_cap_error(&cap)&&lt_deadline==cap_deadline);
 lt_cap_retries=0;cap.error=BT_ATT_ERR_INSUFFICIENT_RESOURCES;assert(lt_retry_cap_error(&cap));
 cap.error=5;assert(!lt_retry_cap_error(&cap));cap.error=BT_ATT_ERR_UNLIKELY;
 lt_phase=LT_REPLY;assert(!lt_retry_cap_error(&cap));lt_phase=LT_CAP;
 parent_relay=false;assert(!lt_retry_cap_error(&cap));parent_relay=true;
 lt_server=true;assert(!lt_retry_cap_error(&cap));lt_server=false;
 cap.type=LT_MTU_DONE;assert(!lt_retry_cap_error(&cap));cap.type=LT_READ;
 cap.conn=&pool[1];assert(!lt_retry_cap_error(&cap));cap.conn=lt_conn;
 pool[0].active=false;assert(!lt_retry_cap_error(&cap));pool[0].active=true;
 lt_deadline=lt_now()+800000;assert(!lt_retry_cap_error(&cap));
 /* Exactly one wait after the third failure. Cold recovery does not stack
  * per-attempt and batch delays; healthy renewal reserves original expiry. */
 seed();lt_attempts=3;lt_retry_after(0,0,0);
 assert(!lt_attempts&&lt_budget_until==160000000&&lt_renew_at==lt_budget_until);
 uint64_t scheduled=lt_renew_at;assert(!lt_closed_poll(160000000)&&lt_renew_at==scheduled);
 seed();lt_attempts=3;startup_admitted=true;live_precision=true;timing.bound=true;timing.context_until=480000000;
 lt_retry_after(0,0,0);assert(!lt_attempts&&lt_budget_until==320000000);
 seed();lt_attempts=3;startup_admitted=true;live_precision=true;timing.bound=true;timing.context_until=80000000;
 lt_retry_after(0,0,0);assert(!lt_attempts&&lt_budget_until==320000000&&timing.context_until==80000000);
 /* A connected peer may disappear before replying to termination. Its1s
  * supervision is inside the1.5s normal drain, without relaxing shutdown. */
 seed();lt_adv=NULL;advertising_live=false;pool[0]=(struct bt_conn){.refs=1,.active=true};lt_conn=&pool[0];lt_attempts=2;
 lt_disconnect(1);assert(!fault&&lt_close_until==1500000);
 now=1000;assert(lt_closed_poll(lt_now())&&!fault&&lt_conn);
 now=1499;pool[0].active=false;lt_dead=true;
 assert(!lt_closed_poll(lt_now())&&!fault&&!lt_conn&&!pool[0].refs&&lt_renew_at==lt_now()+80000000);
 seed();lt_adv=NULL;advertising_live=false;pool[0]=(struct bt_conn){.refs=1,.active=true};lt_conn=&pool[0];
 lt_disconnect(1);now=1500;assert(lt_closed_poll(lt_now())&&fault&&lt_conn&&drain_result==-ETIMEDOUT);
 /* A duplicate native timeout cancel is completion-pending, not permission
  * to discard ownership or declare a permanent radio-owner fault. */
 seed();lt_adv=NULL;advertising_live=false;pool[0]=(struct bt_conn){.refs=1,.initiating=true};lt_conn=&pool[0];lt_phase=LT_CONNECT;cancel_race=true;
 lt_disconnect(6);assert(!fault&&lt_phase==LT_CLOSE&&lt_close_until==500000&&pool[0].refs==1);
 now=499;pool[0].initiating=false;lt_apply_connected(&pool[0],2);
 assert(!lt_closed_poll(lt_now())&&!lt_conn&&!pool[0].refs&&!fault&&drain_events==1&&!drain_result);
 /* A success after cancellation is drained; it never becomes MTU/JOIN. */
 seed();lt_adv=NULL;advertising_live=false;pool[0]=(struct bt_conn){.refs=1,.initiating=true};lt_conn=&pool[0];lt_phase=LT_CONNECT;cancel_race=true;
 lt_disconnect(6);uint64_t original=lt_close_until;now=2;pool[0].initiating=false;pool[0].active=true;
 lt_apply_connected(&pool[0],0);assert(lt_phase==LT_CLOSE&&lt_close_until==original&&disconnect_calls==2&&!fault);
 pool[0].active=false;lt_dead=true;assert(!lt_closed_poll(lt_now())&&!lt_conn&&!pool[0].refs);
 /* Missing and late completion remain explicit failures with owned refs. */
 seed();lt_adv=NULL;advertising_live=false;pool[0]=(struct bt_conn){.refs=1,.initiating=true};lt_conn=&pool[0];lt_phase=LT_CONNECT;cancel_race=true;
 lt_disconnect(6);now=500;assert(lt_closed_poll(lt_now())&&fault&&lt_conn&&pool[0].refs==1&&drain_result==-ETIMEDOUT);
 pool[0].initiating=false;lt_apply_connected(&pool[0],2);assert(lt_closed_poll(lt_now())&&!lt_conn&&!pool[0].refs&&fault);
 seed();pool[0]=(struct bt_conn){.refs=1,.initiating=true};lt_conn=&pool[0];cancel_race=true;assert(lt_close()&&!fault);balanced();
 seed();pool[0]=(struct bt_conn){.refs=1,.initiating=true};lt_conn=&pool[0];cancel_race=late_success=true;assert(lt_close()&&disconnect_calls==2&&now==4);balanced();
 seed();pool[0]=(struct bt_conn){.refs=1,.initiating=true};lt_conn=&pool[0];cancel_race=true;delay_ms=501;assert(!lt_close()&&now==500&&pool[0].refs==1);
 /* Entering shutdown cannot replace an already running drain deadline. */
 seed();pool[0]=(struct bt_conn){.refs=1,.initiating=true};lt_conn=&pool[0];lt_close_until=3000;delay_ms=4;assert(!lt_close()&&now==3&&pool[0].refs==1);
 seed();queue_connected(0);assert(lt_close());assert(disconnect_calls==1&&records==1);balanced();
 seed();arrival=&pool[0];assert(lt_close());assert(disconnect_calls==1);balanced();
 seed();pool[0]=(struct bt_conn){.refs=1,.initiating=true};lt_conn=&pool[0];assert(lt_close());balanced();
 seed();pool[0]=(struct bt_conn){.refs=1,.active=true};lt_conn=&pool[0];queue_connected(1);assert(lt_close());assert(disconnect_calls==2);balanced();
 seed();queue_connected(0);delay_ms=499;assert(lt_close()&&now==499);balanced();
 seed();queue_connected(0);delay_ms=500;assert(!lt_close()&&now==500);assert(!pool[0].active&&!pool[0].refs);
 seed();queue_connected(0);delay_ms=501;assert(!lt_close()&&now==500);assert(pool[0].refs==1&&lt_retired[0].conn==&pool[0]);assert(!lt_reset());
 seed();delete_fails=true;assert(!lt_close()&&lt_adv);assert(!lt_reset());
 seed();lt_adv=NULL;queue_connected(0);assert(!lt_reset()&&!pool[0].refs&&pool[0].active); /* reject unknown live scope */
 seed();pool[0]=(struct bt_conn){.refs=2,.active=true,.ucs=true};put(LT_CONNECTED,&pool[0],0);assert(lt_close());assert(pool[0].refs==1&&pool[0].active&&!disconnect_calls);assert(lt_reset());
 seed();
 for(unsigned i=0;i<CONFIG_BT_MAX_CONN*2;++i){
  unsigned slot=i%CONFIG_BT_MAX_CONN;assert(!pool[slot].refs&&!pool[slot].active);
  lt_adv=(void*)1;lt_advertising=advertising_live=true;
  queue_connected(slot);assert(lt_close());balanced();
 }
 assert(disconnect_calls==CONFIG_BT_MAX_CONN*2);
 puts("PASS cancel EACCES races, late-success cleanup without MTU/JOIN, original callback/shutdown deadlines, unknown ownership, UCS preservation and12 capacity cycles");
 return 0;
}
