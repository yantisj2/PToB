/* Actual display predicate with public connection/subscription boundaries. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "unicast_time.h"
struct bt_conn { bool connected, state_notify, time_notify; };
struct bt_gatt_attr { unsigned index; };
static struct { struct bt_gatt_attr attrs[8]; } uc_service;
struct uc_client { struct bt_conn *conn; struct uc1_session session; bool dead; };
static struct { struct uc_client clients[UC1_CLIENTS]; bool enabled; } u;
static int lock;
typedef int k_spinlock_key_t;
static bool owned=true, locked;
static uint64_t now=100;
static uint64_t now_us(void) { return now; }
static bool rc_owner(void) { return owned; }
static int k_spin_lock(int *p) { assert(p==&lock&&!locked);locked=true;return 1; }
static void k_spin_unlock(int *p,int k) { assert(p==&lock&&locked&&k==1);locked=false; }
#define BT_GATT_CCC_NOTIFY 1
static bool bt_gatt_is_subscribed(struct bt_conn *c,const struct bt_gatt_attr *a,unsigned kind) {
  assert(locked&&kind==BT_GATT_CCC_NOTIFY);
  return c->connected&&(a==&uc_service.attrs[2]?c->state_notify:c->time_notify);
}
#include "uc1_live_under_test.h"
#include "rc_client_indicator_under_test.h"
int main(void) {
  struct bt_conn conn={true,true,true};
  assert(!r2_uc_serving_clients());
  u.enabled=true;
  u.clients[3]=(struct uc_client){.conn=&conn,.session={.hz=8,.until=200}};
  assert(r2_uc_serving_clients()&&!locked);
  now=200;assert(!r2_uc_serving_clients());now=199;
  conn.time_notify=false;assert(!r2_uc_serving_clients());conn.time_notify=true;
  conn.state_notify=false;assert(!r2_uc_serving_clients());conn.state_notify=true;
  conn.connected=false;assert(!r2_uc_serving_clients());conn.connected=true;
  u.clients[3].dead=true;assert(!r2_uc_serving_clients());u.clients[3].dead=false;
  u.clients[3].session.hz=0;assert(!r2_uc_serving_clients());u.clients[3].session.hz=8;
  u.clients[3].conn=NULL;assert(!r2_uc_serving_clients());u.clients[3].conn=&conn;
  owned=false;assert(!r2_uc_serving_clients());owned=true;
  u.enabled=false;assert(!r2_uc_serving_clients());
  return 0;
}
