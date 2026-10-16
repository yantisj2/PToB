/* RC5 dedicated, transient GATT capability/request/reply. One connection total.
 * Discovery is an untrusted 21-byte flags + custom128-bit UUID hint. Authority
 * is granted only by the136-byte PSK transcript. No GM control dependency. */
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/random/random.h>
#define LT_UUID BT_UUID_128_ENCODE(0x48f6e538,0x76c4,0x4a94,0x8c38,0xd4e251bc0005)
static struct bt_uuid_128 lt_service_uuid=BT_UUID_INIT_128(LT_UUID);
static struct bt_uuid_128 lt_data_uuid=BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x48f6e538,0x76c4,0x4a94,0x8c38,0xd4e251bd0005));
static struct rc5_gm_context gm_context;
static struct r2_join_exchange gm_exchange;
static uint64_t gm_sequence;
static uint8_t gm_wire[R2_GROUP_JOIN_BYTES];
static bool gm_write_busy;
#if !defined(R2_SWITCHABLE_FIELD)
static struct bt_uuid_128 gm_service_uuid=BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x850d0400,0xa6ef,0x4775,0x974b,0x188a7d4ed45c));
#endif
static struct bt_uuid_128 gm_data_uuid=BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x850d0401,0xa6ef,0x4775,0x974b,0x188a7d4ed45c));
static bool lt_paused,lt_acquiring,lt_advertising,lt_dead,lt_server,lt_server_pending,lt_reply_ready;
#if defined(R2_RC_AUTO)
static uint64_t lt_rescan_at;
#endif
static uint64_t local_boot,lt_challenge,lt_deadline,lt_renew_at,lt_request_at,lt_next,lt_budget_until,lt_attempt_ready;
static uint32_t lt_sequence,lt_requests,lt_responses,lt_bindings,lt_rejects,lt_attempts,lt_initiations,lt_disconnects;
static struct rc5_demand lt_demand;
static struct bt_le_ext_adv *lt_adv;
static struct bt_conn *lt_conn;
static struct rc5_control lt_request,lt_incoming,lt_reply;
static uint8_t lt_wire[RC5_CONTROL_BYTES],lt_read_wire[RC5_CONTROL_BYTES];
static size_t lt_read_size;
static uint16_t lt_handle;
enum lt_phase {LT_IDLE,LT_CONNECT,LT_MTU,LT_DISCOVER,LT_CAP,LT_WRITE,LT_REPLY,LT_GM_REQUEST,LT_GM_PROOF,LT_CLOSE};
static enum lt_phase lt_phase;
static struct bt_gatt_exchange_params lt_exchange;
static struct bt_gatt_read_params lt_read;
static struct bt_gatt_write_params lt_write;
enum lt_event_type {LT_CONNECTED,LT_DISCONNECTED,LT_MTU_DONE,LT_DISCOVERED,LT_WRITTEN,LT_READ,LT_SERVER_WRITE,LT_GM_WRITE,LT_GM_SENT};
struct lt_event {enum lt_event_type type;struct bt_conn *conn;uint8_t error;uint16_t handle;struct rc5_control control;uint8_t wire[R2_GROUP_JOIN_BYTES];};
K_MSGQ_DEFINE(lt_events,sizeof(struct lt_event),4,4);
K_MUTEX_DEFINE(lt_reply_lock);
static uint8_t lt_server_wire[RC5_CONTROL_BYTES];
static atomic_t lt_queue_peak;
static void lt_note_queue(void){atomic_val_t n=(atomic_val_t)k_msgq_num_used_get(&lt_events),old;do{old=atomic_get(&lt_queue_peak);if(n<=old)return;}while(!atomic_cas(&lt_queue_peak,old,n));}
static void lt_event_put(struct lt_event *e){int rc=k_msgq_put(&lt_events,e,K_NO_WAIT);if(rc&&e->type==LT_CONNECTED)bt_conn_unref(e->conn);check(!rc,"bounded GATT callback queue");lt_note_queue();}
static uint64_t lt_random(void){uint64_t n;do{sys_rand_get(&n,sizeof n);}while(!n);return n;}
static uint64_t lt_now(void){struct r2_capture c;check(r2_capture_now(&c),"GATT capture");return c.tick;}
static void lt_owner(bool active){(void)active;}
static bool lt_live(uint64_t tick){
#if defined(R2_RC_AUTO)
 /* A dedicated RC first synchronizes and maintains its own upstream lease.
  * Downstream demand never controls its GM synchronization or availability. */
 if(node&&routes.node[node].transmitter)return true;
#endif
 return !local_demand||!routes.node[node].transmitter||rc5_demand_live(&lt_demand,tick/16);
}
static void lt_disconnect(void){if(lt_conn&&lt_phase!=LT_CLOSE){
 /* Pinned Zephyr bt_conn_disconnect cancels SCAN_BEFORE_INITIATING and
  * INITIATING through the public host API as well as established ACLs. */
 int rc=bt_conn_disconnect(lt_conn,BT_HCI_ERR_REMOTE_USER_TERM_CONN);
 if(rc&&rc!=-ENOTCONN)check(false,"GATT disconnect/cancel");
 lt_phase=LT_CLOSE;
}}
static ssize_t lt_server_read(struct bt_conn *conn,const struct bt_gatt_attr *attr,void *buf,uint16_t len,uint16_t offset){
 if(conn!=lt_conn||!lt_server)return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
 k_mutex_lock(&lt_reply_lock,K_FOREVER);
 ssize_t result=bt_gatt_attr_read(conn,attr,buf,len,offset,lt_server_wire,sizeof lt_server_wire);
 k_mutex_unlock(&lt_reply_lock);return result;
}
static ssize_t lt_server_write(struct bt_conn *conn,const struct bt_gatt_attr *attr,const void *buf,uint16_t len,uint16_t offset,uint8_t flags){
 if(conn!=lt_conn||!lt_server||offset||len!=RC5_CONTROL_BYTES)return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
 struct lt_event e={.type=LT_SERVER_WRITE,.conn=conn};memcpy(e.wire,buf,len);
 if(k_msgq_put(&lt_events,&e,K_NO_WAIT))return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
 lt_note_queue();return len;
}
static void lt_publish_reply(const struct rc5_control *input){
 struct rc5_control copy=*input;
#if defined(R2_RC_AUTO)
 copy.slots=slot_bits();
#endif
 const struct rc5_control *c=&copy;
 uint8_t wire[RC5_CONTROL_BYTES];if(rc5_control_encode(wire,c,&routes,&group)){check(false,"control encode");return;}
 k_mutex_lock(&lt_reply_lock,K_FOREVER);memcpy(lt_server_wire,wire,sizeof wire);k_mutex_unlock(&lt_reply_lock);
}
BT_GATT_SERVICE_DEFINE(rc5_local_service,
 BT_GATT_PRIMARY_SERVICE(&lt_service_uuid),
 BT_GATT_CHARACTERISTIC(&lt_data_uuid.uuid,BT_GATT_CHRC_READ|BT_GATT_CHRC_WRITE,BT_GATT_PERM_READ|BT_GATT_PERM_WRITE,lt_server_read,lt_server_write,NULL));
static void lt_connected(struct bt_conn *conn,uint8_t error){
#if defined(R2_SWITCHABLE_FIELD)
 if(!r2_switch_rc_active())return;
#endif
struct lt_event e={.type=LT_CONNECTED,.conn=bt_conn_ref(conn),.error=error};lt_event_put(&e);}
static void lt_disconnected(struct bt_conn *conn,uint8_t reason){
#if defined(R2_SWITCHABLE_FIELD)
 if(!r2_switch_rc_active())return;
#endif
struct lt_event e={.type=LT_DISCONNECTED,.conn=conn,.error=reason};lt_event_put(&e);}
BT_CONN_CB_DEFINE(rc5_connections)={.connected=lt_connected,.disconnected=lt_disconnected};
static void lt_mtu_done(struct bt_conn *conn,uint8_t error,struct bt_gatt_exchange_params *p){struct lt_event e={.type=LT_MTU_DONE,.conn=conn,.error=error};lt_event_put(&e);}

static void lt_written(struct bt_conn *conn,uint8_t error,struct bt_gatt_write_params *p){struct lt_event e={.type=LT_WRITTEN,.conn=conn,.error=error};lt_event_put(&e);}
static uint8_t lt_read_done(struct bt_conn *conn,uint8_t error,struct bt_gatt_read_params *p,const void *data,uint16_t length){
 struct lt_event e={.type=LT_READ,.conn=conn,.error=error};
 if(lt_read_size+length>sizeof lt_read_wire)e.error=1;
 if(!e.error&&data){memcpy(lt_read_wire+lt_read_size,data,length);lt_read_size+=length;if(lt_read_size<sizeof lt_read_wire)return BT_GATT_ITER_CONTINUE;if(!p->handle_count)e.handle=p->by_uuid.start_handle;}
 if(!e.error&&lt_read_size!=RC5_CONTROL_BYTES)e.error=1;
 if(!e.error)memcpy(e.wire,lt_read_wire,RC5_CONTROL_BYTES);
 lt_event_put(&e);return BT_GATT_ITER_STOP;
}

/* Normal GM public-client compatibility, on the same single connection owner.
 * The role hint is untrusted; JOIN PSK/transcript validation grants admission. */
#if !defined(R2_SWITCHABLE_FIELD)
static ssize_t gm_marker(struct bt_conn *conn,const struct bt_gatt_attr *attr,void *buf,uint16_t len,uint16_t offset){
 static const uint8_t marker[5]={'R','2','G','1',1};return bt_gatt_attr_read(conn,attr,buf,len,offset,marker,sizeof marker);
}
static ssize_t gm_receive(struct bt_conn *conn,const struct bt_gatt_attr *attr,const void *buf,uint16_t len,uint16_t offset,uint8_t flags){
 if((!node||parent_index!=0)||conn!=lt_conn||lt_server||offset||len!=R2_GROUP_JOIN_BYTES)return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
 struct lt_event e={.type=LT_GM_WRITE,.conn=conn};memcpy(e.wire,buf,len);
 if(k_msgq_put(&lt_events,&e,K_NO_WAIT))return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
 lt_note_queue();return len;
}
BT_GATT_SERVICE_DEFINE(normal_gm_client_service,
 BT_GATT_PRIMARY_SERVICE(&gm_service_uuid),
 BT_GATT_CHARACTERISTIC(&gm_data_uuid.uuid,BT_GATT_CHRC_READ|BT_GATT_CHRC_WRITE_WITHOUT_RESP,BT_GATT_PERM_READ|BT_GATT_PERM_WRITE,gm_marker,gm_receive,NULL));
#endif
static uint8_t gm_located(struct bt_conn *conn,uint8_t error,struct bt_gatt_read_params *p,const void *data,uint16_t length){
 struct lt_event e={.type=LT_DISCOVERED,.conn=conn,.error=error,.handle=p->by_uuid.start_handle};
 if(!data||length!=5||memcmp(data,"R2G1\1",5))e.error=1;
 lt_event_put(&e);return BT_GATT_ITER_STOP;
}
static void gm_sent(struct bt_conn *conn,void *user){(void)user;struct lt_event e={.type=LT_GM_SENT,.conn=conn};lt_event_put(&e);}
static void gm_apply(const uint8_t *wire){
 struct r2_join_record r;uint64_t tick=lt_now(),now=tick/16;
 if(r2_group_join_decode(&r,wire,R2_GROUP_JOIN_BYTES,&group)||r.control||r2_group_join_receive(&gm_exchange,&r,now)){++lt_rejects;lt_deadline=0;return;}
 if(r.kind==R2_JOIN_CHALLENGE){lt_phase=LT_GM_PROOF;lt_next=0;}
 else if(r.kind==R2_JOIN_ACCEPT){
  bool bound=rc5_gm_accept(&gm_context,&r,gm_exchange.proof_at,now,tick);
  if(bound){++lt_bindings;lt_attempts=0;lt_budget_until=0;}
  lt_renew_at=tick+(bound?RC5_RENEW_TICKS:16000000);lt_deadline=0;
 }
}
static int gm_send(unsigned kind,uint64_t tick){
 if(gm_write_busy)return 0;
 uint64_t now=tick/16;
 if(kind==R2_JOIN_REQUEST&&r2_group_join_begin(&gm_exchange,false,routes.node[node].id,routes.node[0].id,local_boot,lt_random(),now))return -EINVAL;
 struct r2_join_record r={.kind=kind,.sender=routes.node[node].id,.receiver=routes.node[0].id,.sender_boot=local_boot,
  .receiver_boot=gm_exchange.peer_boot,.client_nonce=gm_exchange.nonce,.server_nonce=gm_exchange.remote_nonce,.sequence=++gm_sequence,
  .created_us=now,.echo_us=kind==R2_JOIN_PROOF?gm_exchange.challenge_at:0};
 snprintf(r.name,sizeof r.name,"%s",identity.unit);
 int rc=r2_group_join_encode(gm_wire,&r,&group);if(rc)return rc;
 rc=bt_gatt_write_without_response_cb(lt_conn,lt_handle,gm_wire,sizeof gm_wire,false,gm_sent,NULL);
 if(!rc){gm_write_busy=true;lt_phase=LT_CONNECT;++lt_requests;if(kind==R2_JOIN_REQUEST)gm_exchange.request_at=now;else gm_exchange.proof_at=now;}
 return rc;
}

static void lt_apply_connected(struct bt_conn *conn,uint8_t error){
 if(error){if(conn==lt_conn)lt_dead=true;return;}
 struct bt_conn_info info;check(!bt_conn_get_info(conn,&info),"GATT info");
 if(!lt_conn)lt_conn=bt_conn_ref(conn);else if(conn!=lt_conn){(void)bt_conn_disconnect(conn,BT_HCI_ERR_CONN_LIMIT_EXCEEDED);return;}
 lt_server=info.role==BT_CONN_ROLE_PERIPHERAL;if(lt_server)lt_deadline=lt_now()+32000000;
 if(lt_server){lt_advertising=false;if(lt_challenge==UINT64_MAX){lt_deadline=0;return;}++lt_challenge;lt_reply_ready=false;lt_server_pending=false;lt_phase=LT_IDLE;
 struct rc5_control cap={.type=RC5_CAPABILITY,.sender=node,.target=255,.sender_boot=local_boot,.next_challenge=lt_challenge};lt_publish_reply(&cap);}
 else lt_phase=LT_MTU;

 /* Requested parameters are defaults; other supported controller parameters
  * are not rejected merely to fit a historical listening window. */
}
static void lt_apply_read(struct rc5_control c){
 uint64_t tick=lt_now();lt_next=0;
 if(lt_phase==LT_CAP&&c.type==RC5_CAPABILITY&&c.sender==parent_index){
#if defined(R2_RC_AUTO)
  if(!apply_parent_slots(c.slots)){lt_deadline=0;return;}
#endif
  uint32_t lease=RC5_LEASE_US;
#if !defined(R2_RC_AUTO)
  if(routes.node[node].transmitter)lease=rc5_upstream_lease(lt_demand.deadline>tick/16?lt_demand.deadline-tick/16:0);
#endif
  if(!lease){lt_deadline=0;return;}
  lt_request=(struct rc5_control){.type=RC5_REQUEST,.sender=node,.target=parent_index,.sender_boot=local_boot,.target_boot=c.sender_boot,.challenge=c.next_challenge,.nonce=lt_random(),.sequence=++lt_sequence,.lease_us=lease};
#if defined(R2_RC_AUTO)
  lt_request.slots=slot_bits();
  if(!routes.node[node].transmitter&&lt_rescan_at&&tick>=lt_rescan_at){lt_request.operation=1;lt_rescan_at=tick+UINT64_C(120)*16000000;}
#endif
  lt_phase=LT_WRITE;
 }else if(lt_phase==LT_REPLY&&c.type==RC5_RESPONSE&&c.sender==parent_index&&c.target==node&&c.sender_boot==lt_request.target_boot&&c.target_boot==local_boot&&c.challenge==lt_request.challenge&&c.nonce==lt_request.nonce&&c.sequence==lt_request.sequence){
#if defined(R2_RC_AUTO)
  if(!apply_parent_slots(c.slots)){lt_deadline=0;return;}
#endif
  uint64_t age_ns=(((tick-lt_request_at)*125/2)*1000000000+999699999)/999700000;bool bound=false;
  if(c.ready&&c.root_hi_ns<=UINT64_MAX-age_ns){struct rc5_admission grant={.parent_boot=c.sender_boot,.root_boot=c.root_boot,.root_epoch=c.root_epoch,.parent_train=c.train,.parent_epoch=c.capture_epoch,.publication_floor=c.publication,.now_tick=tick,.root_lo_ns=c.root_lo_ns,.root_hi_ns=c.root_hi_ns+age_ns,.root_expires_ns=c.root_expires_ns};bound=rc5_bind(&timing,&grant);if(bound)++lt_bindings;}

  lt_renew_at=tick+(bound?RC5_RENEW_TICKS:16000000);if(bound){lt_attempts=0;lt_budget_until=0;
#if defined(R2_RC_AUTO)
   if(!routes.node[node].transmitter&&!lt_rescan_at)lt_rescan_at=tick+UINT64_C(120)*16000000;
#endif
  }lt_deadline=0;
 }else lt_next=tick+160000;
}
static void lt_drain(void){struct lt_event e;while(!k_msgq_get(&lt_events,&e,K_NO_WAIT)){
 if(e.type==LT_CONNECTED){lt_apply_connected(e.conn,e.error);bt_conn_unref(e.conn);continue;}
 if(e.conn!=lt_conn)continue;

 if(e.type==LT_DISCONNECTED){++lt_disconnects;lt_dead=true;continue;}
 if(e.error){lt_deadline=0;continue;}
 switch(e.type){
  case LT_MTU_DONE:lt_next=0;lt_phase=LT_CAP;break;
  case LT_DISCOVERED:lt_handle=e.handle;lt_next=0;lt_phase=node&&parent_index==0&&!lt_server?LT_GM_REQUEST:LT_CAP;break;
  case LT_GM_WRITE:gm_apply(e.wire);break;
  case LT_GM_SENT:gm_write_busy=false;break;
  case LT_WRITTEN:lt_phase=LT_REPLY;lt_next=lt_now()+160000;break;
  case LT_READ:if(rc5_control_decode(&e.control,e.wire,RC5_CONTROL_BYTES,&routes,&group)){++lt_rejects;lt_deadline=0;break;}if(e.handle)lt_handle=e.handle;lt_apply_read(e.control);break;
  case LT_SERVER_WRITE:
   if(rc5_control_decode(&e.control,e.wire,RC5_CONTROL_BYTES,&routes,&group)||e.control.type!=RC5_REQUEST||e.control.target!=node||e.control.target_boot!=local_boot||e.control.challenge!=lt_challenge){++lt_rejects;break;}
   if(!lt_server_pending&&!lt_reply_ready){lt_incoming=e.control;lt_server_pending=true;}else ++lt_rejects;break;
  default:break;
 }
}}

static void lt_quiesce(bool pause){lt_paused=pause;if(!local_demand||!pause)return;if(lt_advertising){check(!bt_le_ext_adv_stop(lt_adv),"GATT acquisition yield");lt_advertising=false;lt_owner(false);}if(lt_conn)lt_disconnect();}
static void lt_guard(uint64_t tick,bool window){(void)tick;lt_paused=!window||lt_acquiring;}
static void lt_init(void){
 lt_challenge=lt_random()&UINT64_C(0x7fffffffffffffff);
 if(routes.node[node].transmitter){
  struct bt_le_adv_param p={.id=0,.sid=13,.options=BT_LE_ADV_OPT_CONN|BT_LE_ADV_OPT_EXT_ADV|BT_LE_ADV_OPT_USE_IDENTITY|BT_LE_ADV_OPT_NO_2M,.interval_min=48,.interval_max=48};
  check(!bt_le_ext_adv_create(&p,NULL,&lt_adv),"GATT capability create");
  static const uint8_t flags=BT_LE_AD_GENERAL|BT_LE_AD_NO_BREDR;
  struct rc5_control capability={.type=RC5_CAPABILITY,.sender=node,.target=255,.sender_boot=local_boot,.next_challenge=lt_challenge};
 #if defined(R2_RC_AUTO)
  capability.slots=slot_bits();capability.publishing=running;
#endif
  uint8_t hint[RC5_CONTROL_BYTES];check(!rc5_control_encode(hint,&capability,&routes,&group),"capability encode");
  struct bt_data data[]={{.type=BT_DATA_FLAGS,.data_len=1,.data=&flags},{.type=BT_DATA_UUID128_ALL,.data_len=16,.data=lt_service_uuid.val},{.type=BT_DATA_MANUFACTURER_DATA,.data_len=sizeof hint,.data=hint}};
  check(!bt_le_ext_adv_set_data(lt_adv,data,3,NULL,0),"authenticated capability hint");
 }
}
#if defined(R2_RC_AUTO)
static void slot_hint_update(void){
 if(!lt_adv)return;
 struct rc5_control capability={.type=RC5_CAPABILITY,.sender=node,.target=255,.sender_boot=local_boot,.next_challenge=lt_challenge,.slots=slot_bits(),.publishing=running};
 uint8_t hint[RC5_CONTROL_BYTES];if(rc5_control_encode(hint,&capability,&routes,&group)){check(false,"slot hint encode");return;}
 static const uint8_t flags=BT_LE_AD_GENERAL|BT_LE_AD_NO_BREDR;
 struct bt_data data[]={{.type=BT_DATA_FLAGS,.data_len=1,.data=&flags},{.type=BT_DATA_UUID128_ALL,.data_len=16,.data=lt_service_uuid.val},{.type=BT_DATA_MANUFACTURER_DATA,.data_len=sizeof hint,.data=hint}};
 check(!bt_le_ext_adv_set_data(lt_adv,data,3,NULL,0),"slot hint update");
}
#endif
static void lt_poll(uint64_t tick,bool window){
 lt_drain();

 if(lt_dead&&lt_conn){bt_conn_unref(lt_conn);lt_conn=NULL;gm_write_busy=false;lt_dead=false;lt_server=false;lt_phase=LT_IDLE;lt_owner(false);lt_next=tick+320000;}
 if(lt_conn){
  if(!window||tick>=lt_deadline){lt_disconnect();return;}
  if(lt_server){
   if(lt_server_pending){lt_server_pending=false;struct rc5_control *r=&lt_incoming;
    if(!rc5_demand_begin(&lt_demand,local_boot,r->sender_boot,r->challenge,tick/16)||!rc5_demand_accept(&lt_demand,local_boot,r->sender_boot,r->challenge,r->sequence,r->lease_us,tick/16)){++lt_rejects;lt_disconnect();return;}

#if defined(R2_RC_AUTO)
    if(r->operation){slot_change_requested=true;++slot_requests;}
#endif
    lt_reply=(struct rc5_control){.type=RC5_RESPONSE,.sender=node,.target=r->sender,.sender_boot=local_boot,.target_boot=r->sender_boot,.challenge=r->challenge,.nonce=r->nonce,.sequence=r->sequence,.lease_us=r->lease_us,.next_challenge=lt_challenge};
    uint64_t lo=0,hi=0,expires=0,boot=0;uint32_t epoch=0;
    if(!node){uint64_t ns,generation;struct r2_capture captured;if(r2_capture_now(&captured)&&root_read(&captured,&ns,&generation)&&ns<UINT64_MAX-RC5_SOURCE_NS-1000000){lo=ns>1000000?ns-1000000:0;hi=ns+1000000;expires=ns+RC5_SOURCE_NS;boot=local_boot;epoch=root_epoch;}}
    else {struct rc5_stamp held;
     if(rc5_clock_capture(&local_clock,&timing,tick,&held)){
      /* Challenge-bound coarse admission is the parent's local estimate,
       * not a new root proof or precision claim. Preserve root identity. */
      uint64_t spread=1000000+(uint64_t)held.age_us*300/1000;
      if(held.ns<=UINT64_MAX-spread-RC5_SOURCE_NS){lo=held.ns>spread?held.ns-spread:0;hi=held.ns+spread;expires=hi+RC5_SOURCE_NS;boot=timing.root_boot;epoch=timing.root_epoch;}
     }
    }
    if(boot&&expires>hi
#if defined(R2_RC_AUTO)
      &&!slot_change_requested&&!slot_state.scanning&&!slot_move_pending&&(!node||downstream_ready(tick))
#endif
      ){lt_reply.ready=true;lt_reply.train=train;lt_reply.capture_epoch=capture_epoch;lt_reply.publication=publication;lt_reply.root_boot=boot;lt_reply.root_epoch=epoch;lt_reply.root_lo_ns=lo;lt_reply.root_hi_ns=hi;lt_reply.root_expires_ns=expires;}
    lt_publish_reply(&lt_reply);lt_reply_ready=true;++lt_responses;

   }return;
  }
  if(tick<lt_next)return;
  int rc=0;
  switch(lt_phase){
   case LT_MTU:lt_exchange.func=lt_mtu_done;lt_phase=LT_CONNECT;rc=bt_gatt_exchange_mtu(lt_conn,&lt_exchange);break;

   case LT_GM_REQUEST:rc=gm_send(R2_JOIN_REQUEST,tick);break;
   case LT_GM_PROOF:rc=gm_send(R2_JOIN_PROOF,tick);break;
   case LT_CAP:case LT_REPLY:
    if(node&&parent_index==0){lt_read=(struct bt_gatt_read_params){.func=gm_located,.handle_count=0,.by_uuid={.start_handle=1,.end_handle=0xffff,.uuid=&gm_data_uuid.uuid}};lt_next=UINT64_MAX;rc=bt_gatt_read(lt_conn,&lt_read);break;}
    lt_read_size=0;lt_read=(struct bt_gatt_read_params){.func=lt_read_done,.handle_count=1,.single={.handle=lt_handle,.offset=0}};if(lt_phase==LT_CAP){lt_read.handle_count=0;lt_read.by_uuid.start_handle=1;lt_read.by_uuid.end_handle=0xffff;lt_read.by_uuid.uuid=&lt_data_uuid.uuid;}lt_next=UINT64_MAX;rc=bt_gatt_read(lt_conn,&lt_read);break;
   case LT_WRITE:check(!rc5_control_encode(lt_wire,&lt_request,&routes,&group),"GATT request encode");lt_write=(struct bt_gatt_write_params){.func=lt_written,.handle=lt_handle,.offset=0,.data=lt_wire,.length=sizeof lt_wire};lt_request_at=tick;lt_phase=LT_CONNECT;rc=bt_gatt_write(lt_conn,&lt_write);++lt_requests;break;
   default:break;
  }
  if(rc)lt_deadline=0;
  return;
 }
 if(tick<lt_next)return;
 bool need=node&&parent_known&&window&&!lt_paused&&lt_live(tick)&&
  (tick>=lt_renew_at
#if defined(R2_RC_AUTO)
   ||(!routes.node[node].transmitter&&lt_rescan_at&&tick>=lt_rescan_at)
#endif
  );
#if !defined(R2_RC_AUTO)
 if(need&&routes.node[node].transmitter&&lt_bindings&&lt_demand.deadline<tick/16+65000000)need=false;
#endif



 uint32_t attempt_us=2000000;
#if defined(R2_RC_AUTO)
 if(!routes.node[node].transmitter&&!rc6_client_continuous){uint64_t offset=tick/16%1000000;if(offset>=240000)need=false;else attempt_us=(uint32_t)(320000-offset);}
#endif
 /* Bounded transaction default, not a service SLA. */
 if(!need)lt_attempt_ready=0;
 if(need&&tick>=lt_budget_until){
  /* One randomized initiation opportunity inside the existing awake window.
   * This breaks simultaneous CONNECT_IND collisions between passive clients. */
  if(!lt_attempt_ready){uint32_t slack=80000;if(slack>80000)slack=80000;uint32_t delay=slack?(uint32_t)(lt_random()%(slack+1)):0;lt_attempt_ready=tick+(uint64_t)delay*16;return;}
  if(tick<lt_attempt_ready)return;
  lt_attempt_ready=0;
  if(lt_attempts>=3){lt_attempts=0;lt_budget_until=tick+160000000;lt_renew_at=lt_budget_until;return;}
  if(lt_advertising){check(!bt_le_ext_adv_stop(lt_adv),"GATT upstream yield");lt_advertising=false;lt_owner(false);}
  if(scanning){check(!bt_le_scan_stop(),"GATT create scan yield");scanning=false;}
  ++lt_attempts;++lt_initiations;lt_owner(true);lt_deadline=tick+(uint64_t)attempt_us*16;lt_phase=LT_CONNECT;lt_next=0;
  bt_addr_le_t attempt_parent=parent;
#if defined(R2_RC_AUTO) && defined(R2_RC_FIELD_TEST)
  extern void r2_rc_auto_bad_peer(unsigned,bt_addr_le_t *);r2_rc_auto_bad_peer(node,&attempt_parent);
#endif
  int rc=bt_conn_le_create(&attempt_parent,BT_CONN_LE_CREATE_CONN,BT_LE_CONN_PARAM(6,6,0,10),&lt_conn);
  if(rc){lt_conn=NULL;lt_phase=LT_IDLE;lt_owner(false);lt_renew_at=tick+16000000;}return;
 }
 if(lt_adv&&!lt_advertising&&!lt_paused
#if defined(R2_RC_AUTO)
   &&(!node||downstream_ready(tick))
#endif
  ){check(!bt_le_ext_adv_start(lt_adv,BT_LE_EXT_ADV_START_DEFAULT),"GATT capability start");lt_advertising=true;lt_owner(true);}
}
static bool lt_close(void){
 bool closed=true;
 if(lt_conn){
  lt_disconnect();uint64_t until=k_uptime_get()+500;
  while(!lt_dead&&(uint64_t)k_uptime_get()<until){k_sleep(K_MSEC(1));lt_drain();}
  if(lt_dead){bt_conn_unref(lt_conn);lt_conn=NULL;lt_owner(false);}else closed=false;
 }
 if(lt_advertising){int rc=bt_le_ext_adv_stop(lt_adv);if(!rc){lt_advertising=false;lt_owner(false);}else closed=false;}
 if(lt_adv){int rc=bt_le_ext_adv_delete(lt_adv);if(!rc)lt_adv=NULL;else closed=false;}
 check(closed,"GATT owners closed");return closed;
}
