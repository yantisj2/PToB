/* MIT. Copyright (c) 2026 Jonathan Yantis.
 * Production relay GATT callbacks and operation selection with SDK boundaries.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define BT_GATT_ITER_STOP 0
#define BT_GATT_ITER_CONTINUE 1
#define BT_GATT_DISCOVER_CHARACTERISTIC 3
#define BT_GATT_CHRC_READ 2
#define BT_GATT_CHRC_WRITE 8
#define BT_ATT_ERR_ATTRIBUTE_NOT_FOUND 10
struct bt_conn {int id;};
struct bt_uuid {int id;};
static struct {struct bt_uuid uuid;} lt_data_uuid={{7}};
static int bt_uuid_cmp(const struct bt_uuid *a,const struct bt_uuid *b){return a->id-b->id;}
struct bt_gatt_chrc {const struct bt_uuid *uuid;uint16_t value_handle;uint8_t properties;};
struct bt_gatt_attr {uint16_t handle;const void *user_data;};
struct bt_gatt_discover_params {
 const struct bt_uuid *uuid;
 uint8_t (*func)(struct bt_conn *,const struct bt_gatt_attr *,struct bt_gatt_discover_params *);
 uint16_t start_handle,end_handle;unsigned type;
};
struct bt_gatt_read_params {
 uint8_t (*func)(struct bt_conn *,uint8_t,struct bt_gatt_read_params *,const void *,uint16_t);
 unsigned handle_count;
 union {struct {uint16_t handle,offset;} single;
        struct {uint16_t start_handle,end_handle;const struct bt_uuid *uuid;} by_uuid;};
};
enum {LT_DISCOVERED,LT_READ};
enum {LT_CAP,LT_REPLY};
struct lt_event {unsigned type;struct bt_conn *conn;uint8_t error;uint16_t handle;uint8_t wire[212];};
static struct lt_event queued;
static unsigned events,discovered,by_handle,by_uuid;
static struct bt_conn peer={1},foreign={2},*lt_conn=&peer;
static struct bt_gatt_discover_params lt_discovery;
static struct bt_gatt_read_params lt_read;
static uint64_t lt_next,lt_deadline=123456;
static uint16_t lt_handle;
static unsigned lt_phase=LT_CAP;
static size_t lt_read_size;
static uint8_t lt_read_wire[212],capability[212];
static void lt_event_put(const struct lt_event *e){queued=*e;++events;}
static int bt_gatt_discover(struct bt_conn *c,struct bt_gatt_discover_params *p){
 assert(c==lt_conn&&p->uuid==&lt_data_uuid.uuid&&p->type==BT_GATT_DISCOVER_CHARACTERISTIC);
 assert(p->start_handle==1&&p->end_handle==0xffff);++discovered;return 0;
}
static int bt_gatt_read(struct bt_conn *c,struct bt_gatt_read_params *p){
 assert(c==lt_conn);
 if(!p->handle_count){
  /* Pinned NimBLE Read By Type uses a19-byte flat buffer. Its read_flat
   * rejects a212-byte value with ATT0x0e, independently of negotiated MTU. */
  ++by_uuid;assert(sizeof capability>19);p->func(c,14,p,NULL,0);return 0;
 }
 assert(p->handle_count==1&&p->single.handle==42&&p->single.offset==0);
 ++by_handle;p->func(c,0,p,capability,sizeof capability);return 0;
}
#include "rc_relay_discovery_under_test.h"
static unsigned checks;
#define CHECK(x) do {assert(x);++checks;} while(0)
int main(void){
 for(unsigned i=0;i<sizeof capability;i++)capability[i]=(uint8_t)(i^0xa5);
 CHECK(!lt_relay_fetch());CHECK(discovered==1&&!by_handle&&!by_uuid&&!events);
 CHECK(lt_next==UINT64_MAX&&lt_deadline==123456&&!lt_handle);
 struct bt_gatt_chrc chrc={&lt_data_uuid.uuid,42,BT_GATT_CHRC_READ|BT_GATT_CHRC_WRITE};
 struct bt_gatt_attr attr={41,&chrc};
 CHECK(lt_discovery.func(&peer,&attr,&lt_discovery)==BT_GATT_ITER_STOP);
 CHECK(queued.type==LT_DISCOVERED&&queued.conn==&peer&&!queued.error&&queued.handle==42&&!lt_handle);
 /* Deliver the owner's discovered-handle event; discovery grants no timing. */
 lt_handle=queued.handle;lt_read_size=17;
 CHECK(!lt_relay_fetch());CHECK(by_handle==1&&!by_uuid&&queued.type==LT_READ);
 CHECK(lt_read_size==212&&!queued.error&&!memcmp(queued.wire,capability,212));
 CHECK(lt_deadline==123456);
 lt_phase=LT_REPLY;CHECK(!lt_relay_fetch());CHECK(by_handle==2&&discovered==1);
 lt_handle=0;CHECK(lt_relay_read()==-EINVAL&&by_handle==2);
 /* A normal read supports chunks, but never a partial/oversized value. */
 lt_read_size=0;CHECK(lt_read_done(&peer,0,&lt_read,capability,100)==BT_GATT_ITER_CONTINUE);
 CHECK(lt_read_done(&peer,0,&lt_read,capability+100,112)==BT_GATT_ITER_STOP);
 CHECK(!queued.error&&!memcmp(queued.wire,capability,212));
 lt_read_size=0;lt_read_done(&peer,0,&lt_read,capability,100);
 CHECK(lt_read_done(&peer,0,&lt_read,NULL,0)==BT_GATT_ITER_STOP&&queued.error);
 lt_read_size=211;CHECK(lt_read_done(&peer,0,&lt_read,capability,2)==BT_GATT_ITER_STOP&&queued.error);
 /* Missing, malformed or wrong characteristic metadata is refused. */
 CHECK(lt_relay_located(&peer,NULL,&lt_discovery)==BT_GATT_ITER_STOP&&queued.error);
 chrc.value_handle=0;lt_relay_located(&peer,&attr,&lt_discovery);CHECK(queued.error&&!queued.handle);
 chrc.value_handle=41;lt_relay_located(&peer,&attr,&lt_discovery);CHECK(queued.error);
 chrc.value_handle=42;chrc.properties=BT_GATT_CHRC_READ;lt_relay_located(&peer,&attr,&lt_discovery);CHECK(queued.error);
 chrc.properties=BT_GATT_CHRC_READ|BT_GATT_CHRC_WRITE;
 struct bt_uuid wrong={8};chrc.uuid=&wrong;lt_relay_located(&peer,&attr,&lt_discovery);CHECK(queued.error);
 chrc.uuid=&lt_data_uuid.uuid;lt_relay_located(&foreign,&attr,&lt_discovery);
 CHECK(queued.conn==&foreign&&!lt_handle); /* callbacks only queue; owner filters ACL */
 /* Negative peer operation: old read-by-UUID fails before any valid wire. */
 struct bt_gatt_read_params old={.func=lt_read_done,.handle_count=0};lt_read_size=0;
 CHECK(!bt_gatt_read(&peer,&old));CHECK(by_uuid==1&&queued.error==14&&!lt_read_size);
 printf("PASS %u production relay discovery/read assertions;212-byte handle read,19-byte ReadByType refusal, unchanged deadline\n",checks);
}
