/* Host boundary for actual transport connection-lifetime tests. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#define CONTAINER_OF(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define K_NO_WAIT 0
#define K_USEC(x) (x)
#define BT_UUID_INIT_128(...) {0}
#define BT_UUID_128_ENCODE(...) 0
#define BT_GATT_ERR(n) (-(n))
#define BT_ATT_ERR_INVALID_ATTRIBUTE_LEN 13
#define BT_ATT_ERR_UNLIKELY 14
#define BT_ATT_ERR_INSUFFICIENT_RESOURCES 17
#define BT_GATT_ITER_STOP 0
#define BT_GAP_ADV_TYPE_ADV_IND 0
#define BT_ADDR_LE_RANDOM 1
#define BT_GATT_DISCOVER_CHARACTERISTIC 1
#define BT_DATA_FLAGS 1
#define BT_DATA_MANUFACTURER_DATA 255
#define BT_LE_AD_GENERAL 2
#define BT_LE_AD_NO_BREDR 4
#define BT_LE_ADV_OPT_CONN 1
#define BT_LE_ADV_OPT_USE_IDENTITY 2
#define BT_DATA_BYTES(...) {0}
#define BT_LE_ADV_PARAM(...) NULL
#define BT_LE_SCAN_TYPE_PASSIVE 0
struct bt_conn_le_create_param {uint32_t options;uint16_t interval,window;};
#define BT_CONN_LE_OPT_NONE 0
#define BT_CONN_LE_CREATE_PARAM(o,i,w) ((const struct bt_conn_le_create_param[]){{.options=(o),.interval=(i),.window=(w)}})
#define BT_CONN_LE_CREATE_CONN BT_CONN_LE_CREATE_PARAM(0,0x60,0x60)
#define BT_LE_CONN_PARAM(a,b,c,d) (&(struct bt_le_conn_param){a,b,c,d})
#define BT_HCI_ERR_REMOTE_USER_TERM_CONN 19
#define BT_GATT_SERVICE_DEFINE(...)
#define K_MSGQ_DEFINE(n,...) static int n
#define BT_CONN_CB_DEFINE(n) static struct callbacks n
struct bt_uuid {int unused;};
struct bt_uuid_128 {struct bt_uuid uuid;};
typedef struct {uint8_t type;struct {uint8_t val[6];} a;} bt_addr_le_t;
struct bt_le_conn_param {unsigned interval_min,interval_max,latency,timeout;};
struct bt_conn {bt_addr_le_t address;int refs;unsigned role,state,interval,latency,timeout;int info_error;};
#define BT_CONN_ROLE_PERIPHERAL 1
#define BT_CONN_TYPE_LE 1
#define BT_CONN_STATE_CONNECTED 2
#define BT_CONN_STATE_DISCONNECTING 3
struct callbacks {void (*connected)(struct bt_conn *,uint8_t);void (*disconnected)(struct bt_conn *,uint8_t);};
struct bt_gatt_read_params {uint8_t (*func)(struct bt_conn *,uint8_t,struct bt_gatt_read_params *,const void *,uint16_t);uint16_t handle_count;struct {uint16_t start_handle,end_handle;struct bt_uuid *uuid;} by_uuid;};
struct bt_gatt_attr {void *user_data;};
struct bt_gatt_chrc {uint16_t value_handle;};
struct bt_gatt_cb {void (*att_mtu_updated)(struct bt_conn *,uint16_t,uint16_t);};
static void bt_gatt_cb_register(struct bt_gatt_cb *cb){(void)cb;}
struct bt_gatt_exchange_params {void (*func)(struct bt_conn *,uint8_t,struct bt_gatt_exchange_params *);};
struct bt_gatt_discover_params {struct bt_uuid *uuid;uint8_t (*func)(struct bt_conn *,const struct bt_gatt_attr *,struct bt_gatt_discover_params *);uint16_t start_handle,end_handle;int type;};
struct bt_data {uint8_t type,data_len;const uint8_t *data;};
struct net_buf_simple {unsigned len;uint8_t *data;};
struct bt_le_scan_param {unsigned type,interval,window;};
struct data_len {unsigned tx_max_len;};
struct bt_conn_info {unsigned role,state,type;struct {unsigned interval,latency,timeout;struct data_len *data_len;} le;};
struct k_spinlock {int unused;};typedef int k_spinlock_key_t;typedef int atomic_t;
static int k_sys_work_q;
static int atomic_get(const atomic_t *p){return *p;}
static void atomic_set(atomic_t *p,int v){*p=v;}
static void atomic_clear(atomic_t *p){*p=0;}
static void atomic_inc(atomic_t *p){++*p;}
static bool atomic_test_and_set_bit(atomic_t *p,unsigned b){bool old=(*p>>b)&1;*p|=1<<b;return old;}
static void atomic_clear_bit(atomic_t *p,unsigned b){*p&=~(1<<b);}
static int k_spin_lock(struct k_spinlock *p){return 0;}
static void k_spin_unlock(struct k_spinlock *p,int key){}
static uint64_t fake_time=1000000;
static uint64_t k_uptime_ticks(void){return fake_time;}
static uint64_t k_ticks_to_us_floor64(uint64_t x){return x;}
struct k_thread {int unused;};
static void *fake_current;
static void *k_current_get(void){return fake_current?fake_current:&k_sys_work_q;}
static void *k_work_queue_thread_get(void *q){return q;}
struct k_work {void (*handler)(struct k_work *);};
struct k_sem {unsigned count;};
#define K_WORK_DEFINE(n,h) static struct k_work n={h}
#define K_SEM_DEFINE(n,c,l) static struct k_sem n={c}
static struct k_work *fake_queued;
static bool fake_defer,fake_early_tx,fake_stale_wake;
static int fake_submit_result,fake_gatt_result;
static unsigned fake_gatt_calls;
static void (*fake_before_dispatch)(void);
static void fake_dispatch(void){
 struct k_work *w=fake_queued;assert(w);fake_queued=NULL;
 if(fake_before_dispatch)fake_before_dispatch();
 void *previous=fake_current;fake_current=NULL;w->handler(w);fake_current=previous;
}
static int k_work_submit(struct k_work *w){
 if(fake_submit_result<0)return fake_submit_result;
 assert(!fake_queued);fake_queued=w;if(!fake_defer)fake_dispatch();return 1;
}
static void k_sem_reset(struct k_sem *s){s->count=0;}
static void k_sem_give(struct k_sem *s){s->count=1;}
static uint64_t fake_dispatch_due;
static int k_sem_take(struct k_sem *s,uint64_t timeout){
 if(fake_stale_wake){fake_stale_wake=false;return 0;}
 if(s->count){s->count=0;return 0;}
 if(fake_queued&&fake_dispatch_due&&fake_dispatch_due<=fake_time+timeout){fake_time=fake_dispatch_due;fake_dispatch_due=0;fake_dispatch();return 0;}
 fake_time+=timeout;return -EAGAIN;
}
static int k_msgq_put(void *q,void *event,int wait){return 0;}
static unsigned queue_purges;
static void k_msgq_purge(void *q){queue_purges++;}
static int k_msgq_get(void *q,void *event,int wait){return -1;}
static const bt_addr_le_t *bt_conn_get_dst(struct bt_conn *c){return &c->address;}
static int bt_addr_le_cmp(const bt_addr_le_t *a,const bt_addr_le_t *b){return memcmp(a,b,sizeof(*a));}
static struct bt_conn *bt_conn_ref(struct bt_conn *c){++c->refs;return c;}
static void bt_conn_unref(struct bt_conn *c){assert(c->refs>0);--c->refs;}
static int fake_att_allocation(void){assert(k_current_get()==k_work_queue_thread_get(&k_sys_work_q));++fake_gatt_calls;return fake_gatt_result;}
static unsigned fake_mtu_calls;
static int bt_gatt_exchange_mtu(struct bt_conn *c,void *p){++fake_mtu_calls;return fake_att_allocation();}
static unsigned discovers,reads,cancels;
static int bt_gatt_discover(struct bt_conn *c,void *p){++discovers;return fake_att_allocation();}
static int bt_gatt_read(struct bt_conn *c,void *p){++reads;return fake_att_allocation();}
static ssize_t bt_gatt_attr_read(struct bt_conn *c,const struct bt_gatt_attr *a,void *b,uint16_t n,uint16_t o,const void *v,uint16_t len){return len;}
static void bt_gatt_cancel(struct bt_conn *c,void *p){++cancels;}
static unsigned fake_mtu=226;
static unsigned bt_gatt_get_mtu(struct bt_conn *c){return fake_mtu;}
static int bt_gatt_write_without_response_cb(struct bt_conn *c,uint16_t h,const void *data,size_t n,bool sign,void (*done)(struct bt_conn *,void *),void *user){int rc=fake_att_allocation();if(!rc&&fake_early_tx)done(c,user);return rc;}
static int bt_le_adv_start(const void *p,const void *ad,size_t n,const void *sd,size_t sn){return 0;}
static int bt_le_adv_update_data(const struct bt_data *ad,size_t n,const struct bt_data *sd,size_t sn){return 0;}
static unsigned adv_stops;
static int bt_le_adv_stop(void){++adv_stops;return 0;}
static struct bt_le_scan_param last_scan;static unsigned scan_starts,scan_stops;
static int bt_le_scan_start(const void *p,void (*cb)(const bt_addr_le_t *,int8_t,uint8_t,struct net_buf_simple *)){last_scan=*(const struct bt_le_scan_param *)p;assert(last_scan.window<=last_scan.interval);++scan_starts;return 0;}
static uint64_t fake_scan_ready;
static int bt_le_scan_stop(void){++scan_stops;return fake_time<fake_scan_ready?-EBUSY:0;}
static int disconnect_result;
static unsigned disconnects;static struct bt_conn *last_disconnect;
static int bt_conn_disconnect(struct bt_conn *c,uint8_t reason){++disconnects;last_disconnect=c;return disconnect_result;}
static int bt_conn_get_info(struct bt_conn *c,struct bt_conn_info *i){static struct data_len length;memset(i,0,sizeof *i);i->role=c->role;i->state=c->state;i->type=BT_CONN_TYPE_LE;i->le.interval=c->interval;i->le.latency=c->latency;i->le.timeout=c->timeout;i->le.data_len=&length;return c->info_error;}
static int bt_conn_le_create(const bt_addr_le_t *,const void *,const void *,struct bt_conn **);

static unsigned k_msgq_num_used_get(void *q){return 0;}
