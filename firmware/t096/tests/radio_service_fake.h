/* Public Zephyr PA boundary for actual lifecycle race tests. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include "rc_shadow.h"
bool r2_shadow_enabled(void){return true;}
void r2_shadow_normal_map(const struct rc_shadow_map *map){(void)map;}
static void (*shadow_withdraw_probe)(void);
void r2_shadow_normal_withdraw(void){if(shadow_withdraw_probe)shadow_withdraw_probe();}
#define BT_ADDR_LE_RANDOM 1
#define BT_LE_ADV_OPT_NO_2M 1
#define BT_LE_ADV_OPT_EXT_ADV 2
#define BT_LE_ADV_OPT_USE_IDENTITY 4
#define BT_DATA_MANUFACTURER_DATA 255
#define BT_LE_EXT_ADV_START_DEFAULT NULL
struct k_spinlock {int held;};typedef int k_spinlock_key_t;
static int k_spin_lock(struct k_spinlock *s){assert(!s->held);s->held=1;return 0;}
static void k_spin_unlock(struct k_spinlock *s,int k){assert(s->held);s->held=0;}
#define K_MUTEX_DEFINE(name) struct k_spinlock name
#define K_FOREVER -1
static int k_mutex_lock(struct k_spinlock *s,int wait){return k_spin_lock(s);}
static int k_mutex_unlock(struct k_spinlock *s){k_spin_unlock(s,0);return 0;}
typedef int atomic_t;
static int atomic_get(const atomic_t *v){return *v;}
static void atomic_set(atomic_t *v,int n){*v=n;}
static int atomic_inc(atomic_t *v){return (*v)++;}
static bool atomic_cas(atomic_t *v,int old,int next){if(*v!=old)return false;*v=next;return true;}
struct k_thread {int unused;};
#define K_THREAD_STACK_DEFINE(name,size) char name[size]
#define K_THREAD_STACK_SIZEOF(name) sizeof(name)
#define K_NO_WAIT 0
#define K_MSEC(n) (n)
static void k_thread_create(struct k_thread *t,char *s,size_t n,void (*fn)(void *,void *,void *),void *a,void *b,void *c,int priority,int options,int delay){}
static void k_thread_name_set(struct k_thread *t,const char *name){}
static void k_sleep(int delay){}
static uint64_t fake_time=1000000;
static uint64_t k_uptime_ticks(void){return fake_time;}
static uint64_t k_ticks_to_us_floor64(uint64_t ticks){return ticks;}
typedef struct {uint8_t type;struct {uint8_t val[6];} a;} bt_addr_le_t;
static bool bt_addr_le_eq(const bt_addr_le_t *a,const bt_addr_le_t *b){return !memcmp(a,b,sizeof *a);}
static void bt_addr_le_copy(bt_addr_le_t *a,const bt_addr_le_t *b){*a=*b;}
struct bt_le_ext_adv {int unused;};struct bt_le_per_adv_sync {int unused;};
struct net_buf_simple {int unused;};
struct bt_le_scan_recv_info {uint8_t sid;uint16_t interval;const bt_addr_le_t *addr;};
struct bt_le_per_adv_sync_synced_info {uint8_t sid;const bt_addr_le_t *addr;};
struct bt_le_per_adv_sync_term_info {uint8_t sid;const bt_addr_le_t *addr;};
struct bt_le_scan_cb {void (*recv)(const struct bt_le_scan_recv_info *,struct net_buf_simple *);};
struct bt_le_per_adv_sync_cb {
 void (*synced)(struct bt_le_per_adv_sync *,struct bt_le_per_adv_sync_synced_info *);
 void (*term)(struct bt_le_per_adv_sync *,const struct bt_le_per_adv_sync_term_info *);
};
struct bt_le_adv_param {uint8_t id,sid;unsigned options,interval_min,interval_max;};
struct bt_le_per_adv_param {unsigned interval_min,interval_max;};
struct bt_le_per_adv_sync_param {uint8_t sid;unsigned skip,timeout;bt_addr_le_t addr;};
struct bt_data {uint8_t type,data_len;const uint8_t *data;};
static void bt_le_scan_cb_register(struct bt_le_scan_cb *c){}
static int bt_le_per_adv_sync_cb_register(struct bt_le_per_adv_sync_cb *c){return 0;}
static int bt_le_ext_adv_create(const struct bt_le_adv_param *p,void *cb,struct bt_le_ext_adv **out){return -1;}
static int bt_le_per_adv_set_param(struct bt_le_ext_adv *a,const struct bt_le_per_adv_param *p){return 0;}
static int bt_le_per_adv_set_data(struct bt_le_ext_adv *a,const struct bt_data *p,size_t count){return 0;}
static unsigned role_ad_updates;
static int role_ad_error;
static uint8_t role_ad_wire[116];
static int bt_le_ext_adv_set_data(struct bt_le_ext_adv *a,const struct bt_data *p,size_t count,const struct bt_data *sd,size_t sn){
 assert(a&&count==1&&p->type==255&&p->data_len==114&&!sd&&!sn);
 ++role_ad_updates;role_ad_wire[0]=115;role_ad_wire[1]=255;memcpy(role_ad_wire+2,p->data,114);return role_ad_error;
}
static int bt_le_per_adv_start(struct bt_le_ext_adv *a){return 0;}
static int bt_le_ext_adv_start(struct bt_le_ext_adv *a,const void *p){return 0;}
static int bt_le_ext_adv_stop(struct bt_le_ext_adv *a){return 0;}
static int bt_le_per_adv_stop(struct bt_le_ext_adv *a){return 0;}
static int bt_le_ext_adv_delete(struct bt_le_ext_adv *a){return 0;}
static int bt_le_per_adv_sync_create(const struct bt_le_per_adv_sync_param *,struct bt_le_per_adv_sync **);
static int bt_le_per_adv_sync_delete(struct bt_le_per_adv_sync *);
static struct bt_le_per_adv_sync *bt_le_per_adv_sync_lookup_addr(const bt_addr_le_t *,uint8_t);
