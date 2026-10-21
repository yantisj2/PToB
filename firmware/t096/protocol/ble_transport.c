#include "cleanup_budget.h"
/* SPDX-License-Identifier: Apache-2.0
 * Fixed storage, public GATT APIs, single polling owner. Callback work copies
 * one complete record or records completion. No election work in callbacks.
 */
#include "ble_transport.h"
#include "gm_standby.h"
#include "timing_events.h"
#include "initiator_diag.h"
#include "disconnect_diag.h"
#if defined(R2_SWITCHABLE_FIELD)
#include "ble_service.h"
#endif
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/atomic.h>
#if defined(R2_SERVICE_CLIENTS)
#include <zephyr/sys/printk.h>
#endif
#if defined(R2_FIELD_FIXTURE) || defined(R2_GROUP_SIMULATION)
#include "bs_tracing.h"
#endif
#define R2_LOCK_UNIT 1u
#include "health_lock.h"
#define PEERS 5
#define CONTROL_PEERS 4
#if defined(R2_FIELD_FIXTURE)
static unsigned fixture_control_interval,fixture_admission_interval;
void r2_ble_fixture_admission_interval(unsigned units){fixture_admission_interval=units;}
void r2_ble_fixture_control_interval(unsigned units){fixture_control_interval=units;}
#endif
static struct bt_uuid_128 service_uuid=BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x850d0400,0xa6ef,0x4775,0x974b,0x188a7d4ed45c));
static struct bt_uuid_128 data_uuid=BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x850d0401,0xa6ef,0x4775,0x974b,0x188a7d4ed45c));
struct peer;
struct tx_context {struct peer *peer;unsigned slot;};
struct peer {
  struct bt_conn *conn;bt_addr_le_t address;
  /* connect_until is initiation expiry, then original retirement expiry once closing. */
  uint64_t identity,provisional_until,connect_until,first_seen;bool authenticated,admission_server,release,disconnect_requested;
  struct bt_gatt_exchange_params mtu;
  struct bt_gatt_discover_params discovery;
  struct bt_gatt_read_params lookup;atomic_t lookup_done;
  atomic_t connected,dead,tx_busy,mtu_done,discovered,att_mtu;
  uint16_t handle;uint32_t incarnation;
  uint64_t retry,advertised_at;
  uint8_t tx[2][R2_GROUP_CONTROL_MAX];
  struct tx_context completion[2];
};
static bool field_admission_allowed=true,field_admission_renewal,bootstrap_urgent=true;
static bool relay_server;
static bool group_client,client_window=true,bootstrap_needed=true;
static bool gm5_mode,gm5_inventory_due;static uint64_t gm5_selected;
static uint64_t now_us(void);
#if defined(R2_CONTROLLER_DIAGNOSTICS)
static void gm5_inventory_snapshot(bool consumed,uint64_t now);
#else
#define gm5_inventory_snapshot(consumed,now) ((void)0)
#endif
void r2_ble_gm5_policy(bool enabled,uint64_t selected,bool due){
 if(gm5_mode&&gm5_inventory_due&&!due)gm5_inventory_snapshot(false,now_us());
 gm5_mode=enabled;gm5_selected=selected;gm5_inventory_due=due;
}
static uint32_t client_phase;static uint64_t client_resume;static bool client_aligned,client_search_seen;
static uint8_t admission_peer=R2_BLE_ADMISSION_PEER;static bool field_profile;
static uint64_t group_id,local_identity;static uint32_t group_epoch;
static struct peer peers[PEERS];static uint8_t self,nodes;
static bool scanning,advertising,stopping,precision_discovery,precision_serving,precision_draining;
/* The public start API returns EALREADY without replacing live AD data.
 * Keep the existing G1 hint current across local admission/profile changes. */
static uint8_t advertised_discovery[23];
static bool advertised_discovery_valid;
static struct k_thread *transport_owner;
static bool owner_bound,transport_started;
int r2_ble_owner_bind(struct k_thread *owner){
 if(!owner)return -EINVAL;
 if(owner_bound||transport_started)return -EALREADY;
 transport_owner=owner;owner_bound=true;return 0;
}
static uint16_t scan_window,scan_interval;
static uint64_t scan_stop_until;
static uint32_t scan_stop_next_ms;
static uint8_t coordinator=255;static unsigned control_members;
static int stop_error;
static uint8_t public_client=PEERS;
static bool sync_acquiring;
static uint64_t recent_admission,recent_admission_until;
static struct k_spinlock lock;
static uint64_t now_us(void);
/* Four saturating lifecycle counts and first/recent MTU/disconnect records.
 * Each record is [at_lo,at_hi,peer|error<<8|local_request<<16,incarnation,id_lo,id_hi].
 * A local request observation is not proof that it won a disconnect race. */
static uint32_t gm_link_events[28];
static uint32_t gm_adv_calls,gm_adv_errors,gm_mtu_errors;static int gm_adv_result;
/* The main thread never reads owner-mutated connection/controller fields.
 * A query requests one bounded owner-boundary publication, returned by the
 * next query with its original timestamp. Callback records stay separate. */
static uint32_t gm_diag_state[56];static bool gm_diag_requested;
static void gm_diagnostic_publish(void);
static void diag_count(unsigned word){if(gm_link_events[word]!=UINT32_MAX)++gm_link_events[word];}
static void diag_link_event(unsigned counter,unsigned first,unsigned recent,unsigned peer,unsigned error){
 uint64_t at=now_us(),id=peer<nodes?peers[peer].identity:0;
 uint32_t record[6]={(uint32_t)at,(uint32_t)(at>>32),peer|(error<<8)|
  ((peer<nodes&&peers[peer].disconnect_requested)?1u<<16:0),
  peer<nodes?peers[peer].incarnation:0,(uint32_t)id,(uint32_t)(id>>32)};
 if(!gm_link_events[first]&&!gm_link_events[first+1])memcpy(gm_link_events+first,record,sizeof record);
 memcpy(gm_link_events+recent,record,sizeof record);diag_count(counter);
 r2_timing_event(counter==1?R2_TIMING_GM_DISCONNECTED:R2_TIMING_GM_MTU_ERROR,
                 (int)error,id,record[2],record[3],0);
}
/* A discovery hint never authenticates a role. Keep a bounded candidate set
 * separate from the single owned JOIN slot so a weak first advertiser cannot
 * hide a stronger relay or monopolize retries. No callback starts controllers. */
#define DISCOVERY_CANDIDATES 8
struct discovery_candidate {bt_addr_le_t address;uint64_t identity,seen,consumed,cooldown;int16_t rssi;bool passive;};
static struct discovery_candidate candidates[DISCOVERY_CANDIDATES];
static uint64_t admission_parent,candidate_since;static bool admission_seek_relay;
void r2_ble_admission_policy(uint64_t parent,bool seek_relay){
 k_spinlock_key_t k=k_spin_lock(&lock);admission_parent=parent;admission_seek_relay=seek_relay;k_spin_unlock(&lock,k);
}
/* Legacy fixtures retain their bounded control star. Normal G5 uses the
 * selected peer only; candidate discovery remains separate from these slots. */
void r2_ble_topology(unsigned value,unsigned members){coordinator=value;control_members=members;}
static bool control_ready(void){
 /* Source candidacy is not link membership. Keep provisioned/admitted control
  * peers required through source loss; transient admission is not control. */
 unsigned required=coordinator<nodes&&coordinator!=self?1u<<coordinator:0;
 for(unsigned p=0;p<nodes&&p<CONTROL_PEERS;p++){
  if(coordinator!=self||p==self||p==public_client||(field_profile&&p==2))continue;
  if(!group_id||peers[p].identity)required|=1u<<p;
 }
 /* Preserve discovery for an isolated core, including partition merging. */
 if(coordinator>=nodes||!required)return false;
 for(unsigned p=0;p<nodes;p++)if((required&(1u<<p))&&(!peers[p].authenticated||!r2_ble_ready(p)))return false;
 return true;
}
static bool desired(unsigned peer){
 if(gm5_mode){if(peer<CONTROL_PEERS)return peer!=self&&peers[peer].identity&&peers[peer].identity==gm5_selected;
  /* A started transient keeps its original owner through STATUS/promotion;
   * provisional expiry/release still bound it. Poll cadence gates new work. */
  return peer==CONTROL_PEERS&&(gm5_inventory_due||peers[peer].conn);}
 return (!group_client||bootstrap_needed)&&peer<nodes&&peer!=self&&peer!=public_client&&(!group_id||peers[peer].identity||(peer==CONTROL_PEERS&&peers[peer].conn));}
/* Either endpoint may negotiate MTU. Only the current connection's public
 * negotiated-value callback, above the unchanged wire bound, is usable. */
static bool usable_mtu(const struct peer *p){return (uint16_t)atomic_get(&p->att_mtu)>=((group_id||field_profile)?R2_GROUP_CONTROL_MAX:R2_AUTH_BYTES)+3;}
void r2_ble_public_client(unsigned peer){public_client=peer<nodes?peer:PEERS;}
static atomic_t drop_count,rx_count,error_count;
static struct r2_transport_fault first_fault;
static uint64_t now_us(void);
static bool transport_error(int error,const char *operation,unsigned peer){
 atomic_inc(&error_count);
 bool first=!first_fault.operation;
 if(first)first_fault=(struct r2_transport_fault){.at=now_us(),.error=error,.peer=peer,.operation=!strcmp(operation,"MTU")?1:!strcmp(operation,"DISCOVER")?2:!strcmp(operation,"SCAN_START")?4:!strcmp(operation,"ADV_UPDATE")?5:!strcmp(operation,"SCAN_STOP")?6:3};
#if defined(R2_SERVICE_CLIENTS)
 printk("R2_CLIENT_TRANSPORT_ERROR operation=%s error=%d peer=%u\n",operation,error,peer);
#endif
 return first;
}
/* A transient refusal retains scan ownership. G5 core/client use the existing
 * callback-retirement budget from the FIRST refusal; legacy core keeps500ms.
 * The20ms retry cadence never renews that original deadline. */
static bool stop_scan(uint64_t now){
 if(scan_stop_until&&now>=scan_stop_until){
  scan_stop_until=0;transport_error(-ETIMEDOUT,"SCAN_STOP",self);stop_error=-ETIMEDOUT;return false;
 }
 if(scan_stop_until&&(int32_t)((uint32_t)(now/1000)-scan_stop_next_ms)<0)return false;
 int rc=bt_le_scan_stop();
 if(!rc||rc==-EALREADY){
  uint64_t until=scan_stop_until;scanning=false;scan_stop_until=0;
  if(until&&now_us()>=until){transport_error(-ETIMEDOUT,"SCAN_STOP",self);stop_error=-ETIMEDOUT;return false;}
  return true;
 }
 if(!stop_error&&group_id&&(!group_client||gm5_mode)&&(rc==-EACCES||rc==-EBUSY||rc==-EAGAIN)){
  if(!scan_stop_until){scan_stop_until=now+(gm5_mode?R2_CALLBACK_RETIRE_US:500000);r2_timing_event(R2_TIMING_GM_CONTROL,rc,local_identity,17,(uint32_t)((scan_stop_until-now)/1000),0);}
  scan_stop_next_ms=(uint32_t)(now/1000)+20;return false;
 }
 transport_error(rc,"SCAN_STOP",self);stop_error=rc;return false;
}
/* The pinned ATT allocator waits forever outside the system workqueue/ATT RX
 * thread. Keep expensive service work on the preemptible owner; dispatch just
 * one public GATT call on the original cooperative system queue. The owner
 * waits for its actual return, so send acceptance/ACK timestamps do not change.
 * Queueing and waiting consume the ORIGINAL submitted-age deadline. */
enum gatt_operation {GATT_WRITE,GATT_MTU,GATT_READ,GATT_DISCOVER};
struct gatt_request {
 struct bt_conn *conn;unsigned peer,slot;uint32_t incarnation;
 uint64_t submitted,until;enum gatt_operation operation;
 bool pending,done,abandoned;int result;
};
static struct gatt_request gatt_request;
static uint64_t owner_deadline;
static uint32_t bridge_calls,bridge_queue_max_us,bridge_call_max_us,bridge_timeouts;
K_SEM_DEFINE(gatt_done,0,1);
static void tx_done(struct bt_conn *conn,void *user_data);
static void gatt_dispatch(struct k_work *work);
K_WORK_DEFINE(gatt_work,gatt_dispatch);
void r2_ble_owner_deadline(uint64_t until){owner_deadline=until;}
static bool gatt_pending(unsigned peer)
{
 k_spinlock_key_t key=k_spin_lock(&lock);
 bool pending=gatt_request.pending&&gatt_request.peer==peer;
 k_spin_unlock(&lock,key);return pending;
}
static int gatt_public_call(const struct gatt_request *request)
{
 /* Enforce the native allocator contract even if future routing changes. */
 if(k_current_get()!=k_work_queue_thread_get(&k_sys_work_q))return -EWOULDBLOCK;
#if defined(R2_FIELD_FIXTURE) || defined(R2_GROUP_SIMULATION)
 /* The fixture uses the actual pinned ATT allocator, not a fake thread token.
  * One bounded trace per operation proves its K_NO_WAIT context branch. */
 static unsigned contexts;
 if(k_current_get()!=k_work_queue_thread_get(&k_sys_work_q))
  bs_trace_error_time_line("R2_NATIVE_ATT_BAD_CONTEXT operation=%u\n",request->operation);
 unsigned bit=1u<<request->operation;
 if(!(contexts&bit)){contexts|=bit;bs_trace_raw(0,"R2_NATIVE_ATT_CONTEXT operation=%u syswork=1 allocation=K_NO_WAIT\n",request->operation);}
#endif
 struct peer *p=&peers[request->peer];
 switch(request->operation){
 case GATT_MTU:return bt_gatt_exchange_mtu(request->conn,&p->mtu);
 case GATT_READ:return bt_gatt_read(request->conn,&p->lookup);
 case GATT_DISCOVER:return bt_gatt_discover(request->conn,&p->discovery);
 default:return bt_gatt_write_without_response_cb(request->conn,p->handle,
  p->tx[request->slot],r2_control_wire_size(p->tx[request->slot]),false,
  tx_done,&p->completion[request->slot]);
 }
}
static void gatt_dispatch(struct k_work *work)
{
 k_spinlock_key_t key=k_spin_lock(&lock);
 struct gatt_request request=gatt_request;struct peer *p=&peers[request.peer];
 /* Retain the exact connection incarnation and immutable TX/params until this
  * job actually completes, including owner timeout and disconnect. */
 bool valid=request.pending&&!request.abandoned&&!stopping&&
  p->conn==request.conn&&p->incarnation==request.incarnation&&
  atomic_get(&p->connected)&&!atomic_get(&p->dead);
 uint64_t now=now_us(),queued=now>=request.submitted?now-request.submitted:0;
 uint32_t bounded=queued>UINT32_MAX?UINT32_MAX:(uint32_t)queued;
 if(bounded>bridge_queue_max_us)bridge_queue_max_us=bounded;
 k_spin_unlock(&lock,key);
 int rc=!valid?-ENOTCONN:now>=request.until?-ETIMEDOUT:gatt_public_call(&request);
 if(rc&&request.operation==GATT_WRITE)atomic_clear_bit(&p->tx_busy,request.slot);
 bt_conn_unref(request.conn);
 key=k_spin_lock(&lock);
 uint64_t elapsed=now_us()-now;bounded=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
 if(bounded>bridge_call_max_us)bridge_call_max_us=bounded;
 if(valid&&now<request.until&&bridge_calls<UINT32_MAX)++bridge_calls;
 gatt_request.result=rc;gatt_request.done=true;
 bool wake=!gatt_request.abandoned;
 if(!wake)gatt_request.pending=false;
 k_spin_unlock(&lock,key);
 if(wake)k_sem_give(&gatt_done);
}
static int gatt_call(unsigned peer,enum gatt_operation operation,unsigned slot,bool *owned)
{
 if(owned)*owned=false;
 if(!transport_owner){
  if(k_current_get()!=k_work_queue_thread_get(&k_sys_work_q))return -EWOULDBLOCK;
  struct gatt_request request={.conn=peers[peer].conn,.peer=peer,.slot=slot,.operation=operation};
  return gatt_public_call(&request);
 }
 if(k_current_get()!=transport_owner)return -EWOULDBLOCK;
 k_spinlock_key_t key=k_spin_lock(&lock);uint64_t now=now_us();
 struct peer *p=&peers[peer];
 if(gatt_request.pending){k_spin_unlock(&lock,key);return -EBUSY;}
 if(stopping||!p->conn||!atomic_get(&p->connected)||atomic_get(&p->dead)){
  k_spin_unlock(&lock,key);return -ENOTCONN;
 }
 uint64_t request_deadline=now+(gm5_mode?R2_GM5_IO_US:100000);
 if(owner_deadline&&owner_deadline<request_deadline)request_deadline=owner_deadline;
 if(p->provisional_until&&!p->authenticated&&p->provisional_until<request_deadline)request_deadline=p->provisional_until;
 if(now>=request_deadline){k_spin_unlock(&lock,key);return -ETIMEDOUT;}
 gatt_request=(struct gatt_request){.conn=bt_conn_ref(p->conn),.peer=peer,.slot=slot,
  .incarnation=p->incarnation,.submitted=now,.until=request_deadline,.operation=operation,.pending=true};
 k_spin_unlock(&lock,key);k_sem_reset(&gatt_done);
 int rc=k_work_submit(&gatt_work);
 if(rc<0){
  key=k_spin_lock(&lock);struct bt_conn *conn=gatt_request.conn;gatt_request.pending=false;
  k_spin_unlock(&lock,key);bt_conn_unref(conn);return rc;
 }
 if(owned)*owned=true;
 /* Completion publication and semaphore wake are separate. A previous wake
  * arriving after timeout must not make a new request fail early. Recheck the
  * actual request predicate after every wake, using the SAME deadline. */
 for(;;){
  now=now_us();if(now>=request_deadline)break;
  (void)k_sem_take(&gatt_done,K_USEC(request_deadline-now));
  key=k_spin_lock(&lock);bool done=gatt_request.done;k_spin_unlock(&lock,key);
  if(done)break;
 }
 key=k_spin_lock(&lock);
 if(gatt_request.done){rc=gatt_request.result;gatt_request.pending=false;}
 else{gatt_request.abandoned=true;rc=-ETIMEDOUT;}
 /* Even a completed call cannot extend the original owner deadline. An
  * accepted TX retains its slot until tx_done, regardless of this result. */
 if(now_us()>=request_deadline)rc=-ETIMEDOUT;
 if(rc==-ETIMEDOUT&&bridge_timeouts<UINT32_MAX)++bridge_timeouts;
 k_spin_unlock(&lock,key);return rc;
}
/* At most four controller ACL objects can require duplicate teardown. A
 * callback only retains the object; the polling owner issues and drains close. */
static uint64_t close_allowance(struct bt_conn *conn,int *result,uint32_t *parameters,uint32_t *supervision){
 /* Pinned get_info reads host state and negotiated LE parameters without
  * waiting. Incoming links choose their own supervision timeout; the LL
  * termination procedure may legally consume it before callback retirement.
  * Keep the first deadline in the existing field, never refresh on retries. */
 struct bt_conn_info info;int info_rc=bt_conn_get_info(conn,&info);
 uint64_t allowance=R2_CALLBACK_RETIRE_US;*parameters=*supervision=0;
 if(!info_rc){
  if(info.type!=BT_CONN_TYPE_LE)info_rc=-EINVAL;
  else {
   *supervision=info.le.timeout;*parameters=info.le.interval|((uint32_t)info.le.latency<<16);
   if(info.state==BT_CONN_STATE_CONNECTED||info.state==BT_CONN_STATE_DISCONNECTING){
    if(!info.le.timeout||info.le.timeout>3200)info_rc=-EINVAL;
    else allowance+=(uint64_t)*supervision*10000;
   }
   /* Preserve public state and raw parameters even after host disconnect;
    * the application callback may still be pending on deferred work. */
   *supervision|=(uint32_t)info.state<<16;
  }
 }
 *result=info_rc;return allowance;
}
struct duplicate_owner {struct bt_conn *conn;uint64_t until;bool requested,dead;};
static struct duplicate_owner duplicates[4];
static void duplicate_track(struct bt_conn *conn,uint64_t now){
 k_spinlock_key_t k=k_spin_lock(&lock);unsigned free=4;
 for(unsigned i=0;i<4;i++){
  if(duplicates[i].conn==conn){k_spin_unlock(&lock,k);return;}
  if(!duplicates[i].conn)free=i;
 }
 if(free<4){
  int rc;uint32_t parameters,supervision;uint64_t allowance=close_allowance(conn,&rc,&parameters,&supervision);
  duplicates[free]=(struct duplicate_owner){.conn=bt_conn_ref(conn),.until=now+allowance};
  if(rc)stop_error=rc;
  r2_timing_event(R2_TIMING_GM_CONTROL,rc?rc:(int)supervision,0,23,parameters,0);
 }
 else stop_error=-ENOSPC;
 k_spin_unlock(&lock,k);
}
static void duplicate_poll(uint64_t now){
 for(unsigned i=0;i<4;i++){
  k_spinlock_key_t k=k_spin_lock(&lock);struct duplicate_owner *d=&duplicates[i];
  struct bt_conn *conn=d->conn;bool dead=d->dead,issue=conn&&!dead&&!d->requested;
  if(dead)*d=(struct duplicate_owner){0};
  else if(conn){if(now>=d->until)stop_error=-ETIMEDOUT;if(issue)d->requested=true;}
  k_spin_unlock(&lock,k);
  if(dead&&conn)bt_conn_unref(conn);
  else if(issue){
   int rc=bt_conn_disconnect(conn,BT_HCI_ERR_REMOTE_USER_TERM_CONN);
   /* A natural disconnect can win while this synchronous HCI call sleeps.
    * These responses do not release our reference: only its callback does,
    * and a missing callback still faults at the original negotiated cleanup limit. */
   if(rc&&rc!=-EACCES&&rc!=-ENOTCONN)stop_error=rc;
   if(rc)r2_timing_event(R2_TIMING_GM_CONTROL,rc,0,18,i,(uint32_t)(d->until>now?d->until-now:0));
  }
 }
}

K_MSGQ_DEFINE(incoming,sizeof(struct r2_ble_event),32,8);
static uint64_t now_us(void){return k_ticks_to_us_floor64(k_uptime_ticks());}
/* Eight fixed telemetry lamps. RX means complete transport receipt, TX means
 * public GATT completion; neither is a physical RF timestamp/precision claim. */
static uint64_t lamp_times[8][2];
static unsigned lamp_kind(const uint8_t *w)
{
 if(w[0]!=12&&w[0]!=13&&w[0]!=15&&w[0]!=17&&w[0]!=19)return 7;
 switch(w[1]){
 case R2_AUTH_HELLO:return w[44]==255&&w[101]==0?0:1;
 case R2_AUTH_PREPARE:return 2;
 case R2_AUTH_ACK:return 3;
 case R2_AUTH_COMMIT:return 4;
 case R2_AUTH_SERVICE:return 5;
 case R2_AUTH_READY:return 6;
 default:return 7;
 }
}
void r2_ble_timing_lamps(bool transmit,bool corrections,uint64_t now)
{
 /* Owners call only after observing a real packet, never on PA submission.
  * One fixed spinlock copy; no controller calls, allocations or formatting. */
 k_spinlock_key_t key=k_spin_lock(&lock);
 lamp_times[1][transmit?1:0]=now;
 if(corrections)lamp_times[5][transmit?1:0]=now;
 k_spin_unlock(&lock,key);
}
void r2_ble_lamps(uint8_t out[8],uint64_t now)
{
 k_spinlock_key_t key=k_spin_lock(&lock);
 for(unsigned i=0;i<8;i++){
  uint64_t r=lamp_times[i][0],t=lamp_times[i][1];
  /* Cover the 125ms frame and asynchronous owner/model sampling. */
  bool rx=r&&now>=r&&now-r<R2_BLE_LAMP_PULSE_US,tx=t&&now>=t&&now-t<R2_BLE_LAMP_PULSE_US;
  out[i]=tx&&(!rx||t>=r)?2:rx?1:0;
 }
 k_spin_unlock(&lock,key);
}
static int index_conn(struct bt_conn *conn)
{for(unsigned i=0;i<nodes;i++)if(i!=self&&peers[i].conn==conn)return (int)i;return -1;}
static int index_addr(const bt_addr_le_t *addr)
{for(unsigned i=0;i<nodes;i++)if(i!=self&&(!group_id||peers[i].identity)&&!bt_addr_le_cmp(addr,&peers[i].address))return (int)i;return -1;}
static ssize_t write_record(struct bt_conn *conn,const struct bt_gatt_attr *attr,
 const void *buf,uint16_t len,uint16_t offset,uint8_t flags)
{
#if defined(R2_SWITCHABLE_FIELD)
  if(r2_switch_rc_active()){
#if defined(R2_RC_PROFILE)
    return r2_rc7_gm_receive(conn,buf,len,offset);
#else
    return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
#endif
  }
#endif
  if(offset||len<2||len!=r2_control_wire_size(buf))return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  k_spinlock_key_t key=k_spin_lock(&lock);int i=index_conn(conn);
  struct r2_ble_event event={.at=now_us()};
  if(i<0||stopping){k_spin_unlock(&lock,key);return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);}
  lamp_times[lamp_kind(buf)][0]=event.at;
  event.peer=i;event.length=len;event.incarnation=peers[i].incarnation;memcpy(event.data,buf,len);
  k_spin_unlock(&lock,key);
  if(k_msgq_put(&incoming,&event,K_NO_WAIT)){atomic_inc(&drop_count);return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);}
  atomic_inc(&rx_count);return len;
}
static const uint8_t discovery_marker[5]={'R','2','G','1',1};
static ssize_t read_marker(struct bt_conn *conn,const struct bt_gatt_attr *attr,
 void *buf,uint16_t len,uint16_t offset){
 return bt_gatt_attr_read(conn,attr,buf,len,offset,discovery_marker,sizeof discovery_marker);
}
BT_GATT_SERVICE_DEFINE(r2_control_service,
 BT_GATT_PRIMARY_SERVICE(&service_uuid),
 BT_GATT_CHARACTERISTIC(&data_uuid.uuid,BT_GATT_CHRC_WRITE_WITHOUT_RESP|BT_GATT_CHRC_READ,BT_GATT_PERM_WRITE|BT_GATT_PERM_READ,read_marker,write_record,NULL));
static void native_init_observation(uint64_t peer,int result)
{
#if defined(CONFIG_ARM)
  struct r2_init_diag d;r2_init_diag_snapshot(&d);
  /* Cumulative boot-scoped counters need no additional static baseline.
   * This reads the pinned controller's1M scan flags; it performs no operation. */
  extern uint32_t ull_scan_is_enabled(uint8_t handle);
  r2_timing_event(R2_TIMING_GM_CONTROL,result,peer,13,d.count[R2_INIT_PREPARE],d.count[R2_INIT_RX]);
  r2_timing_event(R2_TIMING_GM_CONTROL,result,peer,14,d.count[R2_INIT_MATCH],d.count[R2_INIT_REQUEST]);
  r2_timing_event(R2_TIMING_GM_CONTROL,result,peer,15,d.count[R2_INIT_WINDOW],d.count[R2_INIT_BUFFER]);
  r2_timing_event(R2_TIMING_GM_CONTROL,result,peer,16,d.count[R2_INIT_ABORT],ull_scan_is_enabled(0));
#else
  (void)peer;(void)result;
#endif
}
static void connected(struct bt_conn *conn,uint8_t error)
{
#if defined(R2_SWITCHABLE_FIELD)
  if(r2_switch_rc_active())return;
#endif
  k_spinlock_key_t key=k_spin_lock(&lock);
  diag_count(0);
  for(unsigned d=0;d<4;d++)if(duplicates[d].conn==conn){if(error)duplicates[d].dead=true;k_spin_unlock(&lock,key);return;}
  int i=index_addr(bt_conn_get_dst(conn));
  if(i<0&&group_id&&!error&&!peers[CONTROL_PEERS].conn){
    /* A passive admission hint is not a reservation against a real incoming
     * connection. Its stable ID is bound later from a verified JOIN request
     * and the canonical group-derived address, before any authority. */
    i=CONTROL_PEERS;struct peer *p=&peers[i];p->identity=0;p->address=*bt_conn_get_dst(conn);
    p->admission_server=true;p->release=false;p->provisional_until=now_us()+5000000;
  }
  if(i>=0){struct peer *p=&peers[i];
    if(!p->conn){p->conn=bt_conn_ref(conn);p->disconnect_requested=false;}
    if(p->conn!=conn)i=-1; /* reject duplicate without replacing the owned link */
    else if(group_id){struct bt_conn_info info;if(!bt_conn_get_info(conn,&info))p->admission_server=info.role==BT_CONN_ROLE_PERIPHERAL;}
    if(p->conn==conn){
     if(gm5_mode&&i==CONTROL_PEERS&&!error&&
        (p->disconnect_requested||(p->connect_until&&now_us()>=p->connect_until)))p->release=true;
     if(!gm5_mode)p->disconnect_requested=false;
     else if(!error&&!p->release&&p->connect_until&&now_us()<p->connect_until)p->connect_until=0;
     atomic_clear(&p->att_mtu);atomic_set(&p->connected,!error);atomic_set(&p->dead,error?1:0);if(i==CONTROL_PEERS&&!error)p->provisional_until=p->first_seen?p->first_seen+5000000:now_us()+5000000;++p->incarnation;}
  }
  r2_timing_event(R2_TIMING_GM_CONNECTED,error,i>=0?peers[i].identity:0,
                  i<0?255u:(unsigned)i,i>=0?peers[i].incarnation:0,0);
  bool own_init=group_id&&i>=0&&!peers[i].admission_server;
  uint64_t init_peer=own_init?peers[i].identity:0;
  k_spin_unlock(&lock,key);
  if(own_init)native_init_observation(init_peer,error);
  if(i<0&&!error)duplicate_track(conn,now_us());
}
static void disconnected(struct bt_conn *conn,uint8_t reason)
{
#if defined(R2_SWITCHABLE_FIELD)
  if(r2_switch_rc_active())return;
#endif
  k_spinlock_key_t key=k_spin_lock(&lock);int i=index_conn(conn);
  diag_link_event(1,16,22,i<0?255u:(unsigned)i,reason);
  if(i>=0){struct peer *p=&peers[i];
   if(!p->disconnect_requested){p->disconnect_requested=true;p->connect_until=now_us()+R2_CALLBACK_RETIRE_US;}
   r2_timing_event(R2_TIMING_GM_CONTROL,reason,p->identity,20,p->incarnation,(uint32_t)atomic_get(&p->tx_busy));
   atomic_clear(&p->att_mtu);atomic_clear(&p->connected);atomic_set(&p->dead,1);}
  else for(unsigned d=0;d<4;d++)if(duplicates[d].conn==conn)duplicates[d].dead=true;
  k_spin_unlock(&lock,key);
}
BT_CONN_CB_DEFINE(callbacks)={.connected=connected,.disconnected=disconnected};
static void att_mtu_updated(struct bt_conn *conn,uint16_t tx,uint16_t rx)
{
#if defined(R2_SWITCHABLE_FIELD)
  if(r2_switch_rc_active())return;
#endif
  /* The public host callback supplies negotiated values in its own connection
   * lifecycle. Never traverse ATT channel lists from the preemptible owner:
   * a disconnect can tear them down before our disconnected callback runs. */
  k_spinlock_key_t key=k_spin_lock(&lock);int i=index_conn(conn);
  if(i>=0&&atomic_get(&peers[i].connected)&&!atomic_get(&peers[i].dead))
    atomic_set(&peers[i].att_mtu,MIN(tx,rx));
  k_spin_unlock(&lock,key);
}
static struct bt_gatt_cb mtu_callbacks={.att_mtu_updated=att_mtu_updated};
static bool mtu_callbacks_registered;
static void mtu_done(struct bt_conn *conn,uint8_t error,struct bt_gatt_exchange_params *params)
{
  struct peer *p=CONTAINER_OF(params,struct peer,mtu);
  k_spinlock_key_t key=k_spin_lock(&lock);
  if(p->conn==conn){
   r2_timing_event(R2_TIMING_GM_CONTROL,error,p->identity,8,
                   (uint32_t)atomic_get(&p->att_mtu),p->incarnation);
   if(error){diag_link_event(2,4,10,(unsigned)(p-peers),error);if(gm_mtu_errors!=UINT32_MAX)++gm_mtu_errors;}
   atomic_set(&p->mtu_done,error?-1:1);
  }
  k_spin_unlock(&lock,key);
}
static uint8_t discovered(struct bt_conn *conn,const struct bt_gatt_attr *attr,struct bt_gatt_discover_params *params)
{
  struct peer *p=CONTAINER_OF(params,struct peer,discovery);
  if(p->conn!=conn)return BT_GATT_ITER_STOP;
  r2_timing_event(R2_TIMING_GM_CONTROL,attr?0:-ENOENT,p->identity,9,
                  attr?((const struct bt_gatt_chrc *)attr->user_data)->value_handle:0,p->incarnation);
  if(!attr){atomic_set(&p->discovered,-1);return BT_GATT_ITER_STOP;}
  const struct bt_gatt_chrc *chrc=attr->user_data;
  p->handle=chrc->value_handle;atomic_set(&p->discovered,1);return BT_GATT_ITER_STOP;
}
static uint8_t located(struct bt_conn *conn,uint8_t error,struct bt_gatt_read_params *params,
 const void *data,uint16_t length){
 struct peer *p=CONTAINER_OF(params,struct peer,lookup);
 if(p->conn!=conn||atomic_get(&p->dead))return BT_GATT_ITER_STOP;
 r2_timing_event(R2_TIMING_GM_CONTROL,error,p->identity,10,length,p->incarnation);
 if(!error&&data&&length==sizeof discovery_marker&&params->by_uuid.start_handle&&
    !memcmp(data,discovery_marker,sizeof discovery_marker)){
  p->handle=params->by_uuid.start_handle;atomic_set(&p->discovered,1);
 }
 /* One failed/unsupported marker read falls back to ordinary discovery.
  * This locator is never authentication or permission to trust peer data. */
 atomic_set(&p->lookup_done,1);return BT_GATT_ITER_STOP;
}
static void tx_done(struct bt_conn *conn,void *user_data){struct tx_context *c=user_data;
 k_spinlock_key_t key=k_spin_lock(&lock);lamp_times[lamp_kind(c->peer->tx[c->slot])][1]=now_us();
 atomic_clear_bit(&c->peer->tx_busy,c->slot);
 if(c->peer->disconnect_requested)r2_timing_event(R2_TIMING_GM_CONTROL,0,c->peer->identity,21,c->peer->incarnation,(uint32_t)atomic_get(&c->peer->tx_busy));
 k_spin_unlock(&lock,key);}
/* Caller holds lock. RSSI orders discovery attempts only, never time quality. */
static void candidate_seen(const bt_addr_le_t *address,uint64_t identity,bool passive,int8_t rssi,uint64_t now){
 unsigned slot=DISCOVERY_CANDIDATES;
 for(unsigned i=0;i<DISCOVERY_CANDIDATES;i++)if(candidates[i].identity==identity){slot=i;break;}
 if(slot==DISCOVERY_CANDIDATES){
  slot=0;for(unsigned i=0;i<DISCOVERY_CANDIDATES;i++)if(!candidates[i].identity){slot=i;break;}else if(candidates[i].seen<candidates[slot].seen)slot=i;
  candidates[slot]=(struct discovery_candidate){.identity=identity,.rssi=-127};
 }
 struct discovery_candidate *c=&candidates[slot];
 if(rssi<=0&&rssi>=-127)c->rssi=c->seen?(3*c->rssi+rssi)/4:rssi;
 c->address=*address;c->seen=now;c->passive=passive;
 if(!candidate_since)candidate_since=now;
}
static void candidate_cooldown(uint64_t identity,uint64_t now,uint64_t duration){
 for(unsigned i=0;i<DISCOVERY_CANDIDATES;i++)if(candidates[i].identity==identity){
  candidates[i].cooldown=now+duration;candidates[i].consumed=candidates[i].seen;break;
 }
}
void r2_ble_admission_anchor_deadline(uint64_t identity,uint64_t until){
 /* A successful, authenticated coarse bootstrap may expire before the
  * fairness cooldown. Do not prohibit its necessary reacquisition past that
  * original deadline. This changes retry scheduling, never source lifetime. */
 k_spinlock_key_t k=k_spin_lock(&lock);
 for(unsigned i=0;i<DISCOVERY_CANDIDATES;i++)if(candidates[i].identity==identity&&candidates[i].cooldown>until)candidates[i].cooldown=until;
 k_spin_unlock(&lock,k);
}
static void candidate_select(uint64_t now){
 if(!group_id||(!group_client&&!gm5_mode)||field_profile||(gm5_mode&&!gm5_inventory_due))return;
 k_spinlock_key_t k=k_spin_lock(&lock);struct peer *p=&peers[CONTROL_PEERS];
 /* Never rebind a controller/callback/TX owner. Collect a short bounded set
  * before initiating; the first arriving G1 is not a route decision. */
 if(p->conn||atomic_get(&p->tx_busy)||p->release||gatt_request.pending||
    !candidate_since||now<candidate_since||now-candidate_since<40000){k_spin_unlock(&lock,k);return;}
 int best=-1;
 for(unsigned i=0;i<DISCOVERY_CANDIDATES;i++){
  struct discovery_candidate *c=&candidates[i];
  if(!c->identity||now<c->seen||now-c->seen>=200000||now<c->cooldown||c->seen<=c->consumed||
     (admission_parent&&c->identity!=admission_parent))continue;
  if(!gm5_mode&&c->passive&&!admission_seek_relay&&!admission_parent)continue;
  bool member=false;for(unsigned j=0;j<CONTROL_PEERS;j++)member|=peers[j].identity==c->identity;
  if(member)continue;
  bool preferred=c->passive==admission_seek_relay;
  bool best_preferred=best>=0&&candidates[best].passive==admission_seek_relay;
  bool forced=gm5_mode&&c->identity==gm5_selected;
  bool best_forced=best>=0&&gm5_mode&&candidates[best].identity==gm5_selected;
  bool older=best>=0&&gm5_mode&&c->consumed<candidates[best].consumed;
  bool same_age=best>=0&&(!gm5_mode||c->consumed==candidates[best].consumed);
  /* G5 inventory visits least-recently-consumed identities before RSSI or
   * passive hints. A stronger HOT must not hide another partition's ACTIVE.
   * Explicit selected-peer repair/parent remains first; ordinary RC is intact. */
  if(best<0||(forced&&!best_forced)||(forced==best_forced&&(older||(same_age&&
     ((preferred&&!best_preferred)||(preferred==best_preferred&&
      (c->rssi>candidates[best].rssi||(c->rssi==candidates[best].rssi&&c->identity<candidates[best].identity))))))))best=(int)i;
 }
 if(best>=0){struct discovery_candidate *c=&candidates[best];
  if(p->identity!=c->identity){p->identity=c->identity;p->address=c->address;p->first_seen=now;p->provisional_until=now+5000000;p->retry=1;p->admission_server=false;++p->incarnation;}
  p->advertised_at=c->seen;
 }else p->advertised_at=0;
 k_spin_unlock(&lock,k);
}
#if defined(R2_CONTROLLER_DIAGNOSTICS)
/* One owner-boundary record per closed inventory opportunity. This is a
 * current gate snapshot, not a reconstruction of its earlier missed polls. */
static void gm5_inventory_snapshot(bool consumed,uint64_t now)
{
 unsigned present=0,fresh=0,unconsumed=0,eligible=0,selected_flags=0;uint32_t age=UINT32_MAX;
 k_spinlock_key_t key=k_spin_lock(&lock);struct peer *p=&peers[CONTROL_PEERS];
 for(unsigned i=0;i<DISCOVERY_CANDIDATES;i++){
  const struct discovery_candidate *c=&candidates[i];
  if(!c->identity)continue;
  ++present;
  bool member=false;for(unsigned j=0;j<CONTROL_PEERS;j++)member|=peers[j].identity==c->identity;
  bool selected=gm5_selected&&c->identity==gm5_selected;
  if(selected){selected_flags|=(1u<<15)|((unsigned)(now<c->cooldown)<<18)|((unsigned)member<<19);
   if(c->seen>c->consumed)selected_flags|=1u<<17;}
  if(now<c->seen)continue;
  uint64_t elapsed=now-c->seen;
  if((selected||!gm5_selected)&&elapsed<age)age=(uint32_t)elapsed;
  if(selected&&elapsed<200000)selected_flags|=1u<<16;
  if(elapsed>=200000)continue;
  ++fresh;
  if(c->seen<=c->consumed)continue;
  ++unconsumed;
  if(now<c->cooldown||(admission_parent&&c->identity!=admission_parent))continue;
  if(!member)++eligible;
 }
 unsigned flags=selected_flags|((unsigned)(gm5_selected&&local_identity<gm5_selected)<<20)|(unsigned)consumed|((unsigned)group_client<<1)|((unsigned)client_window<<2)|
  ((unsigned)scanning<<3)|((unsigned)advertising<<4)|((unsigned)precision_draining<<5)|
  ((unsigned)sync_acquiring<<6)|((unsigned)(p->conn!=NULL)<<7)|((unsigned)p->release<<8)|
  ((unsigned)(atomic_get(&p->tx_busy)!=0)<<9)|((unsigned)gatt_request.pending<<10)|
  ((unsigned)field_admission_allowed<<11)|((unsigned)(admission_parent!=0)<<12)|
  ((unsigned)(gm5_selected&&admission_parent&&admission_parent!=gm5_selected)<<13)|
  ((unsigned)(candidate_since&&now>=candidate_since&&now-candidate_since>=40000)<<14);
 uint64_t selected=gm5_selected;k_spin_unlock(&lock,key);
 r2_timing_event(R2_TIMING_GM_CONTROL,(int)flags,selected,24,
                 present|(fresh<<8)|(unconsumed<<16)|(eligible<<24),age);
}
#endif
static void scan_report(const bt_addr_le_t *address,int8_t rssi,uint8_t type,struct net_buf_simple *ad)
{
#if defined(R2_SWITCHABLE_FIELD)
  if(r2_switch_rc_active())return;
#endif
  /* Discovery creates no authority. Main owner alone initiates connections. */
  if(type!=BT_GAP_ADV_TYPE_ADV_IND)return;
  uint64_t candidate=0;bool passive_hint=false;
  if(group_id){
   /* Bounded legacy AD inspection only. A discovery claim creates no authority. */
   for(unsigned at=0;at<ad->len;){unsigned n=ad->data[at];if(!n||at+1+n>ad->len)break;
    const uint8_t *w=ad->data+at+2;
    if(n==24&&ad->data[at+1]==BT_DATA_MANUFACTURER_DATA&&w[0]=='G'&&w[1]=='1'&&(w[2]&63)==1&&
      r2_group_get(w+3,8)==group_id&&r2_group_get(w+11,4)==group_epoch){candidate=r2_group_get(w+15,8);passive_hint=(w[2]&64)!=0;}
    at+=n+1;
   }
   if(!candidate||candidate==local_identity)return;
   /* A passive relay advertises to clients, not to solicit GM control. Once
    * independently eligible it clears this hint and ordinary discovery resumes.
    * This filters futile initiation only; authenticated admission is unchanged. */
   if(!gm5_mode&&!group_client&&passive_hint)return;
   if(!gm5_mode&&!group_client&&candidate==recent_admission&&now_us()<recent_admission_until)return;
  }
  k_spinlock_key_t key=k_spin_lock(&lock);int i=index_addr(address);
  if(group_id){
   if(i>=0&&peers[i].identity!=candidate){k_spin_unlock(&lock,key);return;}
   if((group_client||gm5_mode)&&!field_profile){
    candidate_seen(address,candidate,passive_hint,rssi,now_us());
    if(i<0||i==CONTROL_PEERS){k_spin_unlock(&lock,key);return;}
   }
   if(i<0){
    for(unsigned p=1;p<PEERS;p++)if(peers[p].identity==candidate){k_spin_unlock(&lock,key);return;}
    /* Discovery never decides admission class. All new peers prove PSK/name
     * on the same transient path, then negotiate bounded control capacity. */
    unsigned p=CONTROL_PEERS;
    if(!peers[p].identity&&!peers[p].conn){i=(int)p;peers[p].identity=candidate;peers[p].address=*address;peers[p].first_seen=now_us();peers[p].provisional_until=now_us()+5000000;peers[p].admission_server=local_identity>candidate;peers[p].release=false;
     r2_timing_event(R2_TIMING_GM_CONTROL,0,candidate,11,(uint32_t)(int32_t)rssi,passive_hint);}

   }
  }
  if(field_profile&&i>=0&&peers[i].identity==recent_admission&&now_us()<recent_admission_until){k_spin_unlock(&lock,key);return;}
  if(i>=0&&!peers[i].conn){if(!peers[i].first_seen)peers[i].first_seen=now_us();peers[i].advertised_at=now_us();if(!peers[i].retry)peers[i].retry=1;}
  k_spin_unlock(&lock,key);
}
int r2_ble_start(unsigned unit,unsigned count,const uint8_t addresses[][6])
{
  if(count>CONTROL_PEERS||unit>=count)return -EINVAL;
  if(stop_error)return stop_error; /* Drained fault is not permission to restart. */
  k_spinlock_key_t key=k_spin_lock(&lock);bool pending=gatt_request.pending;k_spin_unlock(&lock,key);
  if(pending)return -EBUSY;
  for(unsigned i=0;i<4;i++)if(duplicates[i].conn)return -EBUSY;
  if(transport_started){
   if(!stopping||!r2_ble_closed())return -EBUSY;
   /* All callbacks/tx slots have drained. Discard old per-peer protocol state,
    * but retain lifetime failure and traffic counters as evidence. */
   memset(peers,0,sizeof peers);memset(duplicates,0,sizeof duplicates);k_msgq_purge(&incoming);
   precision_discovery=precision_serving=sync_acquiring=precision_draining=false;
   client_aligned=client_search_seen=false;client_phase=0;client_resume=0;
  }
  if(!mtu_callbacks_registered){bt_gatt_cb_register(&mtu_callbacks);mtu_callbacks_registered=true;}
  transport_started=true;
  scan_stop_until=0;
  advertised_discovery_valid=false;
  coordinator=255;control_members=0;recent_admission=recent_admission_until=0;field_admission_allowed=true;field_admission_renewal=false;bootstrap_needed=true;bootstrap_urgent=true;
  memset(candidates,0,sizeof candidates);candidate_since=admission_parent=0;admission_seek_relay=false;
  self=unit;nodes=count;stopping=false;public_client=PEERS;group_id=0;admission_peer=CONTROL_PEERS;field_profile=false;
  for(unsigned i=0;i<count;i++){for(unsigned k=0;k<2;k++)peers[i].completion[k]=(struct tx_context){.peer=&peers[i],.slot=k};peers[i].address.type=BT_ADDR_LE_RANDOM;memcpy(peers[i].address.a.val,addresses[i],6);}
  return 0;
}
int r2_ble_group_start(uint64_t identity,uint64_t group,uint32_t epoch,const uint8_t address[6]){
 if(!identity||!group||!epoch||!address)return -EINVAL;
 uint8_t addresses[4][6]={{0}};memcpy(addresses[0],address,6);
 int rc=r2_ble_start(0,4,addresses);if(rc)return rc;
 local_identity=identity;group_id=group;group_epoch=epoch;peers[0].identity=identity;nodes=PEERS;
 for(unsigned k=0;k<2;k++){peers[CONTROL_PEERS].completion[k]=(struct tx_context){.peer=&peers[CONTROL_PEERS],.slot=k};}
 return 0;
}
int r2_ble_field_start(unsigned unit,const uint64_t ids[4],const uint8_t addresses[][6]){
 int rc=r2_ble_start(unit,3,addresses);if(rc)return rc;
 field_admission_allowed=true;field_admission_renewal=false;
 field_profile=true;admission_peer=unit==1?2:1;group_client=unit==2;
 for(unsigned p=0;p<3;p++)peers[p].identity=ids[p];
 /* The public client only contacts its requested relay for bounded coarse
  * bootstrap. Its connection never enters the GM network state machine. */
 public_client=unit==0?2:unit==2?0:PEERS;
 return 0;
}
unsigned r2_ble_admission_peer(void){return admission_peer;}
uint64_t r2_ble_peer_identity(unsigned peer,uint8_t address[6]){
 if(peer>=nodes){return 0;}
 k_spinlock_key_t k=k_spin_lock(&lock);
 uint64_t id=peers[peer].identity;if(address)memcpy(address,peers[peer].address.a.val,6);k_spin_unlock(&lock,k);return id;
}
void r2_ble_peer_authenticated(unsigned peer,bool yes){if(peer>=nodes)return;k_spinlock_key_t k=k_spin_lock(&lock);peers[peer].authenticated=yes;k_spin_unlock(&lock,k);}
/* Count a cold discovery opportunity only after100ms of controller-owned
 * scanning in its existing200ms window. Exclude ACL/initiation and long owner
 * gaps; this is reception opportunity evidence, not physical precision. */
static uint64_t discovery_sample,discovery_second,discovery_listen;
static bool discovery_was_listening,discovery_completed,discovery_reported;
bool r2_ble_admission_scan_opportunity(void){bool v=discovery_completed;discovery_completed=false;return v;}
static void discovery_opportunity_poll(uint64_t now)
{
 bool guest_busy=false;for(unsigned i=0;i<nodes;i++)guest_busy|=peers[i].conn!=NULL;
 uint64_t start=r2_ble_client_window_start(now);
 bool listening=!stopping&&group_client&&client_window&&scanning&&!guest_busy&&r2_ble_client_window_open(now,200000);
 if(start!=discovery_second){discovery_second=start;discovery_listen=0;discovery_reported=false;discovery_was_listening=false;}
 if(listening&&discovery_was_listening&&now>=discovery_sample&&now-discovery_sample<=50000)
  discovery_listen+=now-discovery_sample;
 if(!discovery_reported&&now-start>=200000){
  discovery_reported=true;if(discovery_listen>=100000)discovery_completed=true;
 }
 discovery_sample=now;discovery_was_listening=listening;
}
static void poll_owned(uint64_t now)
{
  discovery_opportunity_poll(now);
  duplicate_poll(now);
  if(scan_stop_until)(void)stop_scan(now);
  for(unsigned i=0;i<nodes;i++)if(i!=self){struct peer *p=&peers[i];
    /* Missing disconnect/TX/GATT callbacks must not retain a slot forever.
     * Never release unsafe ownership on timeout: the terminal service/watchdog
     * path resets it. The first request/callback owns this unextended budget. */
    if(p->disconnect_requested&&p->conn&&p->connect_until&&now>=p->connect_until&&
       (!atomic_get(&p->dead)||atomic_get(&p->tx_busy)||gatt_pending(i))&&!stop_error){
     /* Sample only this existing first terminal, with its retained host ref.
      * An earlier unrelated first fault stays untouched. No application lock
      * is held while the nonwaiting work getters take their kernel lock. */
     uint16_t host_detail=first_fault.operation?0:r2_disconnect_snapshot(p->conn,p->identity,p->incarnation);
     stop_error=-ETIMEDOUT;
     if(transport_error(stop_error,"DISCONNECT",i))first_fault.host_detail=host_detail;
     r2_timing_event(R2_TIMING_GM_CONTROL,stop_error,p->identity,22,p->incarnation,(uint32_t)atomic_get(&p->tx_busy));
    }
    if(gatt_pending(i))continue; /* never mutate an outstanding call's params */
    if(group_id&&(p->identity||p->conn)&&((!p->authenticated&&now>=p->provisional_until)||p->release)){
     if(p->conn)r2_ble_disconnect(i);
     else if(!atomic_get(&p->tx_busy)){k_spinlock_key_t key=k_spin_lock(&lock);
      if(p->identity)r2_timing_event(R2_TIMING_GM_CONTROL,0,p->identity,12,p->release,0);
      p->identity=0;p->retry=p->advertised_at=p->first_seen=0;p->release=false;k_spin_unlock(&lock,key);}
    }
    if(atomic_get(&p->dead)&&!atomic_get(&p->tx_busy)){
      /* Public cancellation invokes the pending callback synchronously in the
       * pinned host. Do not reuse params/unref this incarnation before it. */
      if(p->conn){bt_gatt_cancel(p->conn,&p->mtu);bt_gatt_cancel(p->conn,&p->discovery);bt_gatt_cancel(p->conn,&p->lookup);}
      k_spinlock_key_t key=k_spin_lock(&lock);struct bt_conn *old=p->conn;p->conn=NULL;
      p->connect_until=0;atomic_clear(&p->dead);atomic_clear(&p->connected);atomic_clear(&p->mtu_done);atomic_clear(&p->discovered);p->handle=0;p->mtu.func=NULL;p->discovery.func=NULL;p->lookup.func=NULL;atomic_clear(&p->lookup_done);p->retry=now+200000;p->advertised_at=p->first_seen=0;p->authenticated=false;p->provisional_until=now+5000000;
      /* A rebooted remote starts in JOIN, not the old control slot. Retire
       * drained group links symmetrically before rediscovery; queued events
       * remain fenced by the connection incarnation. */
      if(group_id){p->identity=0;p->release=p->disconnect_requested=false;++p->incarnation;}
      k_spin_unlock(&lock,key);if(old)bt_conn_unref(old);
    }
    /* Client bootstrap is a bounded opportunity, not permission to scan as
     * initiator through its quiet interval. Units may have different uptime
     * phases: bound this actual create attempt, not our local second boundary.
     * Public cancellation retains the connection until callbacks are drained. */
    if(p->conn&&!p->disconnect_requested&&!atomic_get(&p->connected)&&
       p->connect_until&&now>=p->connect_until){
      r2_timing_event(R2_TIMING_GM_CONTROL,-ETIMEDOUT,p->identity,2,p->incarnation,0);
      p->connect_until=0;r2_ble_disconnect(i);
    }
    /* A relay's own bootstrap need governs outgoing acquisition, not an
     * incoming client's bounded admission exchange. Provisional expiry,
     * release, quiet and shutdown still retire this guest independently. */
    bool serving_guest=relay_server&&i==admission_peer&&p->admission_server;
    if(!stopping&&p->conn&&!desired(i)&&!serving_guest){r2_ble_disconnect(i);continue;}
    if(!stopping&&!p->disconnect_requested&&atomic_get(&p->connected)){
      if(!p->mtu.func&&!usable_mtu(p)){p->mtu.func=mtu_done;int rc=gatt_call(i,GATT_MTU,0,NULL);if(rc){if(!gatt_pending(i))p->mtu.func=NULL;if(rc!=-EALREADY)transport_error(rc,"MTU",i);}}
      if(gatt_pending(i))continue;
      /* Keep our accepted asynchronous params owned until its own callback.
       * A peer-first exchange has no outstanding local callback to wait for. */
      bool mtu_ready=usable_mtu(p)&&(!p->mtu.func||atomic_get(&p->mtu_done)==1);
      bool targeted=(field_profile||group_id)&&i==admission_peer;
      if(mtu_ready&&targeted&&!p->lookup.func){
        p->lookup=(struct bt_gatt_read_params){.func=located,.handle_count=0,
          .by_uuid={.start_handle=1,.end_handle=0xffff,.uuid=&data_uuid.uuid}};
        int rc=gatt_call(i,GATT_READ,0,NULL);
        if(rc)atomic_set(&p->lookup_done,1);
      }
      if(gatt_pending(i))continue;
      if(mtu_ready&&!atomic_get(&p->discovered)&&!p->discovery.func&&
         (!targeted||atomic_get(&p->lookup_done))){
        p->discovery=(struct bt_gatt_discover_params){.uuid=&data_uuid.uuid,.func=discovered,.start_handle=1,.end_handle=0xffff,.type=BT_GATT_DISCOVER_CHARACTERISTIC};
        int rc=gatt_call(i,GATT_DISCOVER,0,NULL);if(rc){if(!gatt_pending(i))p->discovery.func=NULL;transport_error(rc,"DISCOVER",i);}
      }
    }
  }
  if(stopping||scan_stop_until||stop_error)return;
  if(gm5_mode)now=now_us(); /* Public calls consume the original G5 budgets. */
  if(gm5_mode)for(unsigned i=0;i<nodes;i++)if(i!=self&&peers[i].conn&&
     !peers[i].authenticated&&now>=peers[i].provisional_until)r2_ble_disconnect(i);
  candidate_select(now);
  bool need_adv=false,need_scan=precision_discovery,pending=false,connected=false;
  for(unsigned i=0;i<nodes;i++)if(i!=self){
    if(atomic_get(&peers[i].connected))connected=true;
    if(peers[i].conn&&!atomic_get(&peers[i].connected))pending=true;
    if(!peers[i].conn&&desired(i)){if(group_id?peers[i].identity<local_identity:i<self)need_adv=true;else need_scan=true;}
  }
  bool control_full=true;for(unsigned i=1;i<CONTROL_PEERS;i++)if(!peers[i].identity)control_full=false;
  uint8_t discovery[23]={'G','1',1};if(control_full)discovery[2]|=128;if(group_client)discovery[2]|=64;r2_group_put(discovery+3,group_id,8);r2_group_put(discovery+11,group_epoch,4);r2_group_put(discovery+15,local_identity,8);
  const struct bt_data ad[]={BT_DATA_BYTES(BT_DATA_FLAGS,BT_LE_AD_GENERAL|BT_LE_AD_NO_BREDR),{.type=BT_DATA_MANUFACTURER_DATA,.data_len=23,.data=discovery}};
  if(group_id){unsigned occupied=0;for(unsigned i=1;i<PEERS;i++)if(peers[i].conn)++occupied;if(occupied<4)need_adv=need_scan=true;}
  /* Ready group cores must keep discovering partitions/newcomers. Advertising
   * alone deadlocks two established cores that both stopped scanning. This is
   * bounded G1 discovery, not a new timing acquisition/authority path. */
  bool server_ready=control_ready();
  if(server_ready){need_adv=true;need_scan=precision_discovery||sync_acquiring||group_id;}
  if(group_client){
   need_adv=(relay_server||gm5_mode)&&client_window;
   bool opportunity=r2_ble_admission_reserves_scan(now)&&field_admission_allowed;
   need_scan=precision_discovery||opportunity;
  }
  if(group_client&&!client_window){
   need_adv=need_scan=false;
   for(unsigned i=0;i<nodes;i++)if(i!=self&&peers[i].conn){
    struct peer *p=&peers[i];
    /* Window permission starts NEW work only. Re-read this actual owner here,
     * after callbacks/GATT work, rather than trusting a caller's connected
     * sample. Accepted initiation still ends at its original role-specific cap. */
    bool retained=gm5_mode&&i==admission_peer&&!p->release&&!p->disconnect_requested&&
      !atomic_get(&p->dead)&&now<p->provisional_until&&
      (atomic_get(&p->connected)||(p->connect_until&&now<p->connect_until));
    if(!retained)r2_ble_disconnect(i);
   }
  }
  /* There is one incoming transient JOIN owner. Do not accept a second ACL
   * just to reject it while the first request/proof is still in flight.
   * Keep its reference through the real disconnect callback; the next
   * initiator can then connect within its original attempt. Control links,
   * passive discovery and the independent periodic publisher keep running. */
  if(group_id&&!group_client){
    k_spinlock_key_t k=k_spin_lock(&lock);
    bool incoming=peers[CONTROL_PEERS].conn&&peers[CONTROL_PEERS].admission_server;
    k_spin_unlock(&lock,k);
    if(incoming)need_adv=false;
  }
  static uint64_t retry_adv;
  if(need_adv&&now>=retry_adv){
    /* The half-duty group join window needs multiple discovery opportunities;
     * retain the legacy100ms cadence for the legacy roster profile. */
    unsigned admission_interval=(group_id||field_profile)?0x32:0xa0;
#if defined(R2_FIELD_FIXTURE)
    if(fixture_admission_interval)admission_interval=fixture_admission_interval;
#endif
    int rc=bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN|BT_LE_ADV_OPT_USE_IDENTITY,admission_interval,admission_interval+0x10,NULL),ad,group_id?ARRAY_SIZE(ad):1,NULL,0);
    {k_spinlock_key_t k=k_spin_lock(&lock);if(gm_adv_calls!=UINT32_MAX)++gm_adv_calls;gm_adv_result=rc;if(rc&&rc!=-EALREADY&&gm_adv_errors!=UINT32_MAX)++gm_adv_errors;k_spin_unlock(&lock,k);}
    if(rc==-EALREADY&&group_id&&(!advertised_discovery_valid||memcmp(advertised_discovery,discovery,sizeof discovery))){
      int update=bt_le_adv_update_data(ad,ARRAY_SIZE(ad),NULL,0);
      if(update==-EAGAIN){
        /* The pinned public host returns EAGAIN when an incoming connection
         * has stopped the legacy advertiser between start and data update.
         * It is no longer live; the next bounded start installs fresh data. */
        rc=update;advertising=false;advertised_discovery_valid=false;
      }else{
        if(update){transport_error(update,"ADV_UPDATE",self);stop_error=update;return;}
        memcpy(advertised_discovery,discovery,sizeof discovery);advertised_discovery_valid=true;
      }
    }
    if(!rc){memcpy(advertised_discovery,discovery,sizeof discovery);advertised_discovery_valid=group_id!=0;}
    if(!rc||rc==-EALREADY)advertising=true;
    retry_adv=now+100000;
  }
  if(!need_adv&&advertising){int rc=bt_le_adv_stop();if(!rc||rc==-EALREADY)advertising=false;else stop_error=rc;}
  /* Recovery discovery uses short distributed5ms/20ms opportunities. A ready
   * core stops this general scan and advertises for initiating newcomers.
   * Actual upstream PA acquisition retains its own full discovery window.
   * Passive clients preserve their separate400/450ms window and500ms quiet. */
  /* Every native GM core, including an unpaired eligible follower, discovers
   * peers in distributed5ms/20ms opportunities. A50ms control reservation is
   * inappropriate beside protected62.5ms precision events. Explicit PA
   * acquisition and passive-client windows retain their separate budgets. */
  bool core_discovery=group_id&&!group_client&&!precision_discovery&&!sync_acquiring;
  uint16_t wanted_interval=(core_discovery||group_client||(field_profile&&(precision_discovery||self==1)))?0x20:0xa0;
  uint16_t wanted_window=core_discovery?0x08:group_client||(field_profile&&self==1&&precision_discovery)?wanted_interval:field_profile&&self==1?0x08:connected?0x10:0x50;
#if defined(R2_FIELD_FIXTURE)
  if(field_profile&&self==1&&now>=132000000&&now<134000000){
   static unsigned trace_state=UINT32_MAX;
   unsigned state=need_scan|(pending<<1)|(precision_discovery<<2)|(sync_acquiring<<3)|(connected<<4)|(r2_ble_admission_due(now)<<5)|(wanted_window<<8);
   if(state!=trace_state){trace_state=state;bs_trace_raw(0,"FIELD_SCAN t=%llu need=%u pending=%u precision=%u acquiring=%u connected=%u due=%u window=%u interval=%u\n",(unsigned long long)now,need_scan,pending,precision_discovery,sync_acquiring,connected,r2_ble_admission_due(now),wanted_window,wanted_interval);}
  }
#endif
  if(precision_draining)need_scan=false;
  if((!need_scan||pending||scan_window!=wanted_window||scan_interval!=wanted_interval)&&scanning&&!stop_scan(now))return;
  if(need_scan&&!pending&&!scanning){
    struct bt_le_scan_param param={.type=BT_LE_SCAN_TYPE_PASSIVE,.interval=wanted_interval,.window=wanted_window};
    int rc=bt_le_scan_start(&param,scan_report);if(!rc||rc==-EALREADY){scanning=true;scan_window=wanted_window;scan_interval=wanted_interval;}
    else{
      /* A refused receive owner is not sixty seconds of healthy observation.
       * Preserve the public API error and enter the existing bounded fault
       * drain; do not silently run an isolated election without discovery. */
      transport_error(rc,"SCAN_START",self);stop_error=rc;return;
    }
  }
#if defined(R2_FIELD_FIXTURE)
  if(group_id&&group_client&&now>=70000000&&now<145000000&&r2_ble_client_window_open(now,400000)){
   static uint64_t trace_bucket;uint64_t bucket=now/100000;
   if(bucket!=trace_bucket){trace_bucket=bucket;struct peer *p=&peers[CONTROL_PEERS];
    bs_trace_raw(0,"R2_GUEST_OWNER t=%llu scan=%u pending=%u acquiring=%u needed=%u urgent=%u window=%u allowed=%u due=%u phase=%u peer=%llu conn=%u retry=%llu advertised=%llu provisional=%llu release=%u\n",
     (unsigned long long)now,scanning,pending,sync_acquiring,bootstrap_needed,bootstrap_urgent,client_window,field_admission_allowed,r2_ble_admission_due(now),client_phase,
     (unsigned long long)p->identity,p->conn!=NULL,(unsigned long long)p->retry,(unsigned long long)p->advertised_at,
     (unsigned long long)p->provisional_until,p->release);
   }
  }
#endif
  if(precision_draining)return;
  if(pending||sync_acquiring||(group_client&&!client_window))return;
  for(unsigned i=0;i<nodes;i++){
    if(server_ready&&!group_client&&(!group_id||i!=CONTROL_PEERS))continue;
    struct peer *p=&peers[i];if(!desired(i)||p->conn||!p->retry||now<p->retry||(field_profile&&self==1&&i==2))continue;
    bool lower=group_id?local_identity<p->identity:self<i;
    bool fallback=group_id&&!server_ready&&p->first_seen&&now>=p->first_seen+3000000&&now<p->provisional_until;
    if(!group_client&&!lower&&!fallback)continue;
    if(group_client&&(!r2_ble_admission_reserves_scan(now)||!field_admission_allowed))continue;
    /* A departed standby is not a discovery observation. Consume a fresh
     * connectable report for every attempt, including after disconnect/error.
     * Blind re-creation otherwise leaves the initiator scanning for an absent
     * peer while healthy source SERVICE records expire in the radio queue. */
    k_spinlock_key_t discovery_key=k_spin_lock(&lock);
    uint64_t observed=p->advertised_at;
    bool fresh=observed&&now>=observed&&now-observed<200000;
    if(fresh)p->advertised_at=0;
    k_spin_unlock(&lock,discovery_key);
    if(!fresh)continue;
    if(group_id&&(group_client||gm5_mode)&&!field_profile){k_spinlock_key_t key=k_spin_lock(&lock);candidate_cooldown(p->identity,now,gm5_mode?0:2000000);k_spin_unlock(&lock,key);}
    if(scanning&&!stop_scan(now))return;
    /* Connectable advertising reserves a host connection object. With three
     * persistent control links that reservation is the fourth object, so it
     * must be released before initiating the one transient join connection.
     * The next owner poll restores discovery if creation does not succeed. */
    if(group_id&&advertising){int rc=bt_le_adv_stop();if(!rc||rc==-EALREADY)advertising=false;else{stop_error=rc;return;}}
    struct bt_conn *conn=NULL;
    /* Native GM HELLO calibration has an80ms causal round-trip width limit.
     * A50ms ACL can consume100ms in two event waits before owner/air delay.
     * Use20ms opportunities for native GM control. Reserved precision PA
     * remains62.5ms/16Hz and wins radio conflicts; neither HELLO cadence nor
     * original source/authentication deadlines change. Passive bootstrap
     * keeps7.5ms and legacy/field control keeps50ms. */
    unsigned interval=(field_profile||group_id)&&group_client?6:group_id?16:40;
#if defined(R2_FIELD_FIXTURE)
    if(fixture_control_interval&&!group_client)interval=fixture_control_interval;
#endif
    unsigned supervision=field_profile&&group_client?10:100;
    if(group_id)p->admission_server=false;
    /* The stock initiator scans continuously60ms/60ms. Once protected PA
     * receive/transmit events exist that reservation can be skipped wholesale.
     * Finite5ms/20ms native G1 windows fit between precision events and use
     * the stock cancellation path rather than continuous-scan resumption. */
    const struct bt_conn_le_create_param *create=group_id?
      BT_CONN_LE_CREATE_PARAM(BT_CONN_LE_OPT_NONE,0x20,0x08):BT_CONN_LE_CREATE_CONN;
    /* One G5 initiation consumes this opportunity, including a refused public
     * create. The serialized control owner consumes this original timestamp
     * into next_poll before reopening work. Reuse the RC cooldown storage. */
    if(gm5_mode){gm5_inventory_snapshot(true,now);recent_admission_until=now;gm5_inventory_due=false;}
    /* Publish before the sleeping public call: its connected callback may run
     * before create returns, including on the original deadline boundary. */
    if(group_client||(group_id&&i==CONTROL_PEERS))p->connect_until=now+(gm5_mode?R2_GM5_IO_US:100000);
    int rc=bt_conn_le_create(&p->address,create,BT_LE_CONN_PARAM(interval,interval,0,supervision),&conn);
    if(!rc){
      /* A connected callback can run while the synchronous create command
       * sleeps. It may already own a reference to this same connection. Keep
       * one application reference, not both the callback and returned refs. */
      k_spinlock_key_t key=k_spin_lock(&lock);
      bool already_owned=p->conn!=NULL;
      bool conflict=already_owned&&p->conn!=conn;
      if(!already_owned)p->conn=conn;
      k_spin_unlock(&lock,key);
      if(conflict){
        /* A raced duplicate loses to the already tracked connection.
         * Never overwrite its owner or reference; close only the new object. */
        duplicate_track(conn,now);
      }
      if(already_owned)bt_conn_unref(conn);
      if(group_id&&i==CONTROL_PEERS&&!p->first_seen)p->provisional_until=now+5000000;
      /* All G5 roles use the existing1s discovery budget. Ordinary RC
       * keeps100ms.
       * Neither public-call delay nor a callback renews the original cap. */
      if(gm5_mode&&(p->release||(!atomic_get(&p->connected)&&p->connect_until&&now_us()>=p->connect_until))){
       p->release=true;r2_ble_disconnect(i);
      }
      r2_timing_event(R2_TIMING_GM_CONTROL,0,p->identity,1,interval,
                      p->connect_until>now?(uint32_t)((p->connect_until-now)/1000):0);
      p->retry=now+500000;
    }
    else{p->connect_until=0;p->retry=now+200000;if(rc!=-EALREADY&&rc!=-ENOMEM&&rc!=-EBUSY)transport_error(rc,"CREATE",i);}
    break;
  }
}
void r2_ble_poll(uint64_t now){poll_owned(now);gm_diagnostic_publish();}
bool r2_ble_ready(unsigned p)
{return p<nodes&&p!=self&&!peers[p].disconnect_requested&&atomic_get(&peers[p].connected)&&!atomic_get(&peers[p].dead)&&atomic_get(&peers[p].discovered)==1&&usable_mtu(&peers[p]);}
bool r2_ble_receive(struct r2_ble_event *event)
{
  if(k_msgq_get(&incoming,event,K_NO_WAIT))return false;
  if(event->peer>=nodes||event->incarnation!=peers[event->peer].incarnation||!atomic_get(&peers[event->peer].connected)){atomic_inc(&drop_count);event->peer=255;}
  return true;
}
int r2_ble_send(unsigned i,const uint8_t *data)
{
  if(k_current_get()!=(transport_owner?transport_owner:k_work_queue_thread_get(&k_sys_work_q)))return -EWOULDBLOCK;
  if(stopping||!r2_ble_ready(i))return -ENOTCONN;
  struct peer *p=&peers[i];unsigned slot;
  /* Only the workqueue admits writes. Completion can release an owned slot,
   * never acquire one. Keep one free for source SERVICE/ACK; do not cancel or
   * reuse the other immutable buffer before its public completion. */
  bool urgent=(data[0]==12||data[0]==13||data[0]==15||data[0]==17||data[0]==19)&&(data[1]==R2_AUTH_SERVICE||data[1]==R2_AUTH_ACK);
  if(!urgent&&atomic_get(&p->tx_busy))return -EAGAIN;
  for(slot=0;slot<2;slot++)if(!atomic_test_and_set_bit(&p->tx_busy,slot))break;
  if(slot==2)return -EAGAIN;
  size_t length=r2_control_wire_size(data);memcpy(p->tx[slot],data,length);
  bool owned=false;int rc=gatt_call(i,GATT_WRITE,slot,&owned);
  if(rc&&!owned)atomic_clear_bit(&p->tx_busy,slot);
  return rc;
}
static int disconnect_owned(unsigned i){
 k_spinlock_key_t key=k_spin_lock(&lock);struct bt_conn *conn=i<nodes?peers[i].conn:NULL;
 if(!conn||peers[i].disconnect_requested){k_spin_unlock(&lock,key);return conn?0:-ENOTCONN;}
 struct peer *p=&peers[i];
 int info_rc;uint32_t parameters,supervision;
 uint64_t allowance=close_allowance(conn,&info_rc,&parameters,&supervision);
 p->disconnect_requested=true;p->connect_until=now_us()+allowance;diag_count(3);
 uint64_t identity=p->identity;uint32_t incarnation=p->incarnation;
 k_spin_unlock(&lock,key);
 /* Unknown negotiated parameters are a terminal observation, not permission
  * to invent a shorter timeout or release controller-owned storage. */
 if(info_rc){stop_error=info_rc;transport_error(info_rc,"DISCONNECT",i);}
 r2_timing_event(R2_TIMING_GM_CONTROL,info_rc?info_rc:(int)supervision,identity,23,parameters,incarnation);
 int rc=bt_conn_disconnect(conn,BT_HCI_ERR_REMOTE_USER_TERM_CONN);
 /* Public close refusal can race a natural callback. Neither response alone
  * proves retirement. Unexpected failures are terminal; no request storm. */
 if(rc&&rc!=-EACCES&&rc!=-ENOTCONN){stop_error=rc;transport_error(rc,"DISCONNECT",i);}
 r2_timing_event(R2_TIMING_GM_CONTROL,rc,identity,19,incarnation,(uint32_t)(p->connect_until/1000));
 return rc;
}
void r2_ble_disconnect(unsigned i){(void)disconnect_owned(i);}
void r2_ble_stop(void)
{
  stopping=true;duplicate_poll(now_us());
  if(scanning)(void)stop_scan(now_us());
  int rc=bt_le_adv_stop();if(!rc||rc==-EALREADY)advertising=false;else stop_error=rc;
  for(unsigned i=0;i<nodes;i++)if(i!=self&&peers[i].conn){
    /* Pinned public bt_conn_disconnect cancels CONNECTING through the host. */
    rc=disconnect_owned(i);
    if(rc&&rc!=-EACCES&&rc!=-ENOTCONN)stop_error=rc;
  }
}
bool r2_ble_closed(void)
{k_spinlock_key_t key=k_spin_lock(&lock);bool pending=gatt_request.pending;k_spin_unlock(&lock,key);if(pending||scanning||advertising)return false;for(unsigned d=0;d<4;d++)if(duplicates[d].conn)return false;for(unsigned i=0;i<nodes;i++)if(i!=self&&(peers[i].conn||atomic_get(&peers[i].tx_busy)))return false;return true;}
int r2_ble_stop_error(void){return stop_error;}
uint32_t r2_ble_drops(void){return atomic_get(&drop_count);}
uint32_t r2_ble_receipts(void){return atomic_get(&rx_count);}
uint32_t r2_ble_errors(void){return atomic_get(&error_count);}

uint32_t r2_ble_stages(void)
{
 uint32_t stages=0;for(unsigned p=0;p<nodes&&p<CONTROL_PEERS;p++){
  unsigned stage=peers[p].conn?1:0;
  if(atomic_get(&peers[p].connected))stage|=2;
  if(atomic_get(&peers[p].mtu_done)==1)stage|=4;
  if(atomic_get(&peers[p].discovered)==1)stage|=8;
  if(atomic_get(&peers[p].mtu_done)<0)stage|=16;
  if(atomic_get(&peers[p].discovered)<0)stage|=32;
  if(atomic_get(&peers[p].tx_busy))stage|=64;
  stages|=stage<<(p*8);
 }return stages;
}

uint32_t r2_ble_parameters(unsigned i)
{
 struct bt_conn_info info;
 if(i>=nodes||!peers[i].conn||bt_conn_get_info(peers[i].conn,&info))return 0;
 return info.le.interval|((uint32_t)info.le.data_len->tx_max_len<<16);
}

void r2_ble_precision_draining(bool enabled){precision_draining=enabled;}
void r2_ble_precision_discovery(bool enabled){precision_discovery=enabled;}
void r2_ble_serving(bool enabled){precision_serving=enabled;}
void r2_ble_sync_acquiring(bool enabled){sync_acquiring=enabled;}
bool r2_ble_initiating(void){
 bool pending=false;k_spinlock_key_t key=k_spin_lock(&lock);
 for(unsigned i=0;i<nodes;i++)if(i!=self&&peers[i].conn&&!atomic_get(&peers[i].connected))pending=true;
 k_spin_unlock(&lock,key);return pending;
}

uint32_t r2_ble_backlog(void)
{
 unsigned tx=0;for(unsigned p=0;p<nodes;p++){unsigned b=atomic_get(&peers[p].tx_busy);tx+=(b&1)+((b>>1)&1);}
 return k_msgq_num_used_get(&incoming)|(tx<<8);
}

bool r2_ble_admission_server(void){return field_profile?self==1:peers[admission_peer].admission_server;}
void r2_ble_admission_completed(uint64_t identity,uint64_t now){
 /* One completed guest cannot monopolize discovery at every window start.
  * This bounded retry cooldown is not a membership/identity cache. */
 if(!gm5_mode){recent_admission=identity;recent_admission_until=now+(field_profile?3000000:5000000);}
 if(group_id&&!field_profile){k_spinlock_key_t k=k_spin_lock(&lock);candidate_cooldown(identity,now,gm5_mode?0:5000000);k_spin_unlock(&lock,k);}
}
bool r2_ble_admission_due(uint64_t now){return (!group_client||bootstrap_needed)&&(!field_profile||now>=recent_admission_until);}
bool r2_ble_admission_connected(void){return atomic_get(&peers[admission_peer].connected);}
uint64_t r2_ble_gm5_attempt_at(void){return gm5_mode?recent_admission_until:0;}
uint64_t r2_ble_admission_deadline(void){
 struct peer *p=&peers[admission_peer];
 return p->conn&&!p->release&&!p->disconnect_requested&&!atomic_get(&p->dead)&&now_us()<p->provisional_until?p->provisional_until:0;
}
void r2_ble_admission_release(void){if(!field_profile)peers[admission_peer].release=true;r2_ble_disconnect(admission_peer);}

void r2_ble_group_client(bool value){group_client=value;}
void r2_ble_group_relay(bool value){relay_server=value;}
void r2_ble_client_bootstrap(bool needed,bool urgent){bootstrap_needed=needed;bootstrap_urgent=urgent;}

uint64_t r2_ble_client_window_start(uint64_t now){
 uint64_t elapsed=(now%1000000+1000000-client_phase)%1000000;
 return now>=elapsed?now-elapsed:0;
}
bool r2_ble_client_window_open(uint64_t now,uint32_t width){
 return now>=client_resume&&now-r2_ble_client_window_start(now)<width;
}
void r2_ble_client_seek(uint64_t now,bool cold){
 if(!cold||gm5_mode||field_profile||!group_client||client_aligned){client_search_seen=false;return;}
 if(r2_ble_client_window_open(now,400000))client_search_seen=true;
 if(!client_search_seen||now<client_resume||sync_acquiring||scanning||
    r2_ble_admission_connected()||now-r2_ble_client_window_start(now)<500000)return;
 for(unsigned i=0;i<PEERS;i++)if(peers[i].conn)return;
 /* Passive peers with unrelated boot phases need rendezvous. Move only after
  * an actual completed window and drained resources; preserve500ms quiet. */
 client_phase=(client_phase+100000)%1000000;client_resume=now+500000;client_search_seen=false;
#if defined(R2_FIELD_FIXTURE)
 bs_trace_raw(0,"FIELD_SEARCH t=%llu phase=%u resume=%llu\n",(unsigned long long)now,client_phase,(unsigned long long)client_resume);
#endif
}
bool r2_ble_client_align(uint64_t now,uint64_t utc_us){
 /* Caller has drained its PA owner. Wait for existing transport work and old
  * quiet half; skip at least another half-second before the new phase starts. */
 for(unsigned i=0;i<PEERS;i++)if(peers[i].conn)return false;
 if(client_aligned||!group_client||sync_acquiring||scanning||
    r2_ble_admission_connected()||now-r2_ble_client_window_start(now)<500000)return false;
 client_phase=(uint32_t)((now%1000000+1200000-utc_us%1000000)%1000000);
 client_resume=now+500000;client_aligned=true;
#if defined(R2_FIELD_FIXTURE)
 bs_trace_raw(0,"FIELD_WINDOW t=%llu phase=%u resume=%llu\n",(unsigned long long)now,client_phase,(unsigned long long)client_resume);
#endif
 return true;
}
void r2_ble_client_window(bool enabled){client_window=enabled;}
void r2_ble_client_defer(uint64_t until){if(group_client&&until>client_resume)client_resume=until;}

bool r2_ble_control_available(void){
 for(unsigned i=1;i<(gm5_mode?2:CONTROL_PEERS);i++)if(!peers[i].identity&&!peers[i].conn)return true;
 return false;
}
bool r2_ble_control_member(void){
 for(unsigned i=1;i<CONTROL_PEERS;i++)if(peers[i].identity)return true;
 return false;
}
int r2_ble_admission_promote(void){
 struct peer *p=&peers[CONTROL_PEERS];
 if(gm5_mode&&(now_us()>=p->provisional_until||p->release||p->disconnect_requested||atomic_get(&p->dead)))return -ETIMEDOUT;
 if(!r2_ble_ready(CONTROL_PEERS)||atomic_get(&p->tx_busy)||gatt_pending(CONTROL_PEERS))return -EAGAIN;
 unsigned slot=CONTROL_PEERS;for(unsigned i=1;i<(gm5_mode?2:CONTROL_PEERS);i++)if(!peers[i].identity&&!peers[i].conn){slot=i;break;}
 if(slot==CONTROL_PEERS)return -ENOSPC;
 k_spinlock_key_t k=k_spin_lock(&lock);struct peer *d=&peers[slot];
 d->conn=p->conn;d->address=p->address;d->identity=p->identity;d->provisional_until=gm5_mode?p->provisional_until:now_us()+5000000;d->authenticated=false;d->release=false;
 d->mtu=p->mtu;d->discovery=p->discovery;d->handle=p->handle;++d->incarnation;
 atomic_set(&d->connected,1);atomic_clear(&d->dead);atomic_set(&d->mtu_done,atomic_get(&p->mtu_done));atomic_set(&d->discovered,1);atomic_clear(&d->tx_busy);
 /* The same negotiated link moved slots. Readiness still requires its cached
  * public MTU callback result; neither a flag nor a new host query replaces it. */
 atomic_set(&d->att_mtu,atomic_get(&p->att_mtu));atomic_clear(&p->att_mtu);
 p->conn=NULL;p->identity=0;p->handle=0;p->retry=p->advertised_at=p->first_seen=0;p->release=false;p->mtu.func=NULL;p->discovery.func=NULL;p->lookup.func=NULL;atomic_clear(&p->lookup_done);++p->incarnation;
 atomic_clear(&p->connected);atomic_clear(&p->dead);atomic_clear(&p->mtu_done);atomic_clear(&p->discovered);
 k_spin_unlock(&lock,k);return (int)slot;
}

int r2_ble_admission_bind(uint64_t id,const uint8_t address[6]){
 struct peer *p=&peers[admission_peer];if(!id||!address||!r2_ble_admission_connected()||memcmp(address,p->address.a.val,6))return -EACCES;
 if(field_profile)return p->identity==id?0:-EACCES;
 k_spinlock_key_t k=k_spin_lock(&lock);
 bool duplicate=false;for(unsigned i=0;i<CONTROL_PEERS;i++)if(peers[i].identity==id)duplicate=true;
 if(duplicate||(p->identity&&p->identity!=id)){k_spin_unlock(&lock,k);return -EACCES;}
 p->identity=id;k_spin_unlock(&lock,k);return 0;
}

void r2_ble_field_admission_window(bool allowed,bool renewal){field_admission_allowed=allowed;field_admission_renewal=renewal;}

bool r2_ble_admission_urgent(uint64_t now){(void)now;return bootstrap_urgent;}

/* Serialized transient owner only; never called in a radio callback. */
int r2_ble_admission_nonce(uint64_t *out){
 if(!out)return -EINVAL;
 *out=0;int rc=sys_csrand_get(out,sizeof *out);
 return rc?rc:!*out?-EIO:0;
}

bool r2_ble_admission_reserves_scan(uint64_t now){
 return group_client&&client_window&&field_admission_allowed&&r2_ble_admission_due(now)&&
  r2_ble_client_window_open(now,((field_profile||bootstrap_urgent||field_admission_renewal)?200000:100000));
}

/* Serialized owner publication only; callback ingress never writes this. */
void r2_ble_fault_snapshot(struct r2_transport_fault *out){
 k_spinlock_key_t key=k_spin_lock(&lock);*out=first_fault;
 out->bridge_calls=bridge_calls;out->bridge_queue_max_us=bridge_queue_max_us;
 out->bridge_call_max_us=bridge_call_max_us;out->bridge_timeouts=bridge_timeouts;
 out->bridge_pending=gatt_request.pending;k_spin_unlock(&lock,key);
}
static void gm_diagnostic_publish(void){
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(!gm_diag_requested&&(!stop_error||gm_diag_state[9]==(uint32_t)stop_error)){k_spin_unlock(&lock,key);return;}
 gm_diag_requested=false;const uint64_t at=now_us();uint32_t *out=gm_diag_state;
 out[0]=1;out[1]=scanning|(advertising<<1)|(precision_discovery<<2)|
  (precision_serving<<3)|(sync_acquiring<<4)|(group_client<<5)|(stopping<<6);
 out[2]=scan_interval;out[3]=scan_window;out[4]=coordinator;out[5]=control_members;
 out[6]=nodes;out[7]=self;out[8]=(uint32_t)atomic_get(&error_count);
 out[9]=(uint32_t)stop_error;out[10]=(uint32_t)at;out[11]=(uint32_t)(at>>32);
 for(unsigned i=0;i<PEERS;i++){
  struct peer *p=&peers[i];uint32_t *v=out+12+8*i;
  v[0]=(uint32_t)p->identity;v[1]=(uint32_t)(p->identity>>32);v[2]=p->incarnation;
  v[3]=(p->conn!=NULL)|(atomic_get(&p->connected)?2u:0)|(atomic_get(&p->dead)?4u:0)|
   (p->authenticated?8u:0)|(atomic_get(&p->mtu_done)==1?16u:0)|
   (atomic_get(&p->mtu_done)<0?32u:0)|(atomic_get(&p->discovered)==1?64u:0)|
   (p->admission_server?256u:0)|(p->release?512u:0)|(p->disconnect_requested?1024u:0)|
   ((uint32_t)atomic_get(&p->tx_busy)<<16);
  v[4]=(uint32_t)atomic_get(&p->att_mtu);
  v[5]=(uint32_t)p->provisional_until;v[6]=(uint32_t)(p->provisional_until>>32);
  uint64_t age=p->first_seen&&at>=p->first_seen?at-p->first_seen:UINT64_MAX;
  v[7]=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;
 }
 out[52]=gm_adv_calls;out[53]=(uint32_t)gm_adv_result;out[54]=gm_adv_errors;out[55]=gm_mtu_errors;
 k_spin_unlock(&lock,key);
}
void r2_ble_gm_diagnostics(uint32_t out[R2_GM_TRANSPORT_WORDS]){
 k_spinlock_key_t key=k_spin_lock(&lock);
 memcpy(out,gm_diag_state,12*sizeof *out);memcpy(out+12,gm_link_events,sizeof gm_link_events);
 memcpy(out+40,gm_diag_state+12,44*sizeof *out);gm_diag_requested=true;
 k_spin_unlock(&lock,key);
}
