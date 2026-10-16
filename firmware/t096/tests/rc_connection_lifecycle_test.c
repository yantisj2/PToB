/* Production close/reset bodies with controlled asynchronous BLE boundaries. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define CONFIG_BT_MAX_CONN 6
#define K_NO_WAIT 0
#define K_MSEC(x) (x)
#define BT_HCI_ERR_REMOTE_USER_TERM_CONN 0x13
struct bt_conn {unsigned refs;bool active,initiating,ucs,scheduled;unsigned at;};
struct rc5_gm_context {int v;};struct r2_join_exchange {int v;};struct rc5_demand {int v;};
static struct rc5_gm_context gm_context;static struct r2_join_exchange gm_exchange;static struct rc5_demand lt_demand;
static uint32_t gm_admission_diagnostic[7];
static uint64_t lt_attempt_started,gm_sequence,lt_challenge,lt_deadline,lt_renew_at,lt_request_at,lt_next,lt_budget_until,lt_attempt_ready,lt_adv_check_at,last_context_progress;
static unsigned lt_sequence,lt_requests,lt_responses,lt_bindings,lt_rejects,lt_attempts,lt_initiations,lt_disconnects,lt_adv_recoveries,lt_read_size,lt_handle,lt_phase;
static bool parent_relay,lt_paused,lt_acquiring,lt_advertising,lt_dead,lt_server,lt_server_pending,lt_reply_ready,gm_write_busy;
static void *lt_adv;static struct bt_conn *lt_conn;enum {LT_IDLE};
enum {LT_CONNECTED,LT_DISCONNECTED,LT_MTU_DONE};
struct lt_event {unsigned type;struct bt_conn *conn;unsigned error;};
static struct {struct lt_event event[4];unsigned count;} lt_events;
static struct bt_conn pool[CONFIG_BT_MAX_CONN];static struct bt_conn *arrival;
static unsigned now,delay_ms,disconnect_calls,records;static bool delete_fails,advertising_live;
static void put(unsigned type,struct bt_conn *c,unsigned error){assert(lt_events.count<4);lt_events.event[lt_events.count++]=(struct lt_event){type,c,error};}
static int k_msgq_get(void *q,struct lt_event *e,int wait){(void)q;(void)wait;if(!lt_events.count)return -EAGAIN;*e=lt_events.event[0];memmove(lt_events.event,lt_events.event+1,--lt_events.count*sizeof *e);return 0;}
static unsigned k_msgq_num_used_get(void *q){(void)q;return lt_events.count;}
static uint64_t k_uptime_get(void){return now;}
static void k_sleep(unsigned ms){now+=ms;for(unsigned i=0;i<CONFIG_BT_MAX_CONN;++i){struct bt_conn *c=&pool[i];if(c->scheduled&&now>=c->at){c->scheduled=false;if(c->initiating){c->initiating=false;++c->refs;put(LT_CONNECTED,c,2);}else{c->active=false;put(LT_DISCONNECTED,c,0);}}}}
static void bt_conn_unref(struct bt_conn *c){assert(c->refs);--c->refs;}
static bool r2_uc_owns(struct bt_conn *c){return c->ucs;}
static int bt_conn_disconnect(struct bt_conn *c,unsigned reason){assert(reason==0x13&&!advertising_live);assert(!c->ucs);++disconnect_calls;c->scheduled=true;c->at=now+delay_ms;return 0;}
static void lt_record(unsigned event,int result,unsigned phase){assert(event==4&&result==0&&phase==5);++records;}
static int bt_le_ext_adv_stop(void *adv){assert(adv);advertising_live=false;if(arrival){++arrival->refs;arrival->active=true;put(LT_CONNECTED,arrival,0);arrival=NULL;}return 0;}
static int bt_le_ext_adv_delete(void *adv){assert(adv&&!advertising_live);return delete_fails?-EIO:0;}
#include "rc_connection_lifecycle_under_test.h"
static void seed(void){
 memset(pool,0,sizeof pool);memset(lt_retired,0,sizeof lt_retired);memset(&lt_events,0,sizeof lt_events);
 lt_conn=NULL;lt_adv=(void*)1;lt_dead=false;lt_advertising=advertising_live=true;lt_retirement_error=false;
 arrival=NULL;now=0;delay_ms=2;disconnect_calls=records=0;delete_fails=false;
}
static void queue_connected(unsigned slot){pool[slot].refs=1;pool[slot].active=true;put(LT_CONNECTED,&pool[slot],0);}
static void balanced(void){assert(!lt_events.count&&!lt_adv);for(unsigned i=0;i<CONFIG_BT_MAX_CONN;++i)assert(!pool[i].refs&&!pool[i].active&&!pool[i].initiating&&!lt_retired[i].conn);assert(lt_reset());}
int main(void){
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
 puts("PASS queued/during-stop arrivals, central cancel, multiple ACLs, deadline499/500/501ms, failed retirement, UCS preservation and12 capacity cycles");
 return 0;
}
