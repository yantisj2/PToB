/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual shared transport, fake public host edges: private physical JOIN row
 * must preserve public logical4 and its asynchronous ownership. */
#include <sys/types.h>

#define k_msgq_put unused_queue_put
#define k_msgq_get unused_queue_get
#define k_msgq_purge unused_queue_purge
#include "ble_transport_fake.h"
#undef k_msgq_put
#undef k_msgq_get
#undef k_msgq_purge
static int gm7_test_queue_put(void *, void *, int);
static int gm7_test_queue_get(void *, void *, int);
static void gm7_test_queue_purge(void *);
#define k_msgq_put gm7_test_queue_put
#define k_msgq_get gm7_test_queue_get
#define k_msgq_purge gm7_test_queue_purge
static int sys_csrand_get(void *out, size_t n) {
  memset(out, 0x5a, n);
  return 0;
}
#include "../protocol/ble_transport.c"
#include <stdio.h>
static struct r2_ble_event queued_event;
static bool queue_live;
static int gm7_test_queue_put(void *q, void *v, int wait) {
  (void)q;
  (void)wait;
  if (queue_live)
    return -ENOSPC;
  queued_event = *(struct r2_ble_event *)v;
  queue_live = true;
  return 0;
}
static int gm7_test_queue_get(void *q, void *v, int wait) {
  (void)q;
  (void)wait;
  if (!queue_live)
    return -EAGAIN;
  *(struct r2_ble_event *)v = queued_event;
  queue_live = false;
  return 0;
}
static void gm7_test_queue_purge(void *q) {
  (void)q;
  queue_live = false;
  ++queue_purges;
}
static struct bt_conn outgoing;
static unsigned creates;
static int bt_conn_le_create(const bt_addr_le_t *a, const void *b,
                             const void *c, struct bt_conn **out) {
  assert(!gm7_server && group_client);
  (void)b;
  assert(((const struct bt_le_conn_param *)c)->interval_min == 6);
  outgoing = (struct bt_conn){.address = *a,
                              .refs = 2,
                              .interval = 6,
                              .timeout = 100,
                              .state = BT_CONN_STATE_CONNECTED};
  ++creates;
  callbacks.connected(&outgoing, 0);
  *out = &outgoing;
  return 0;
}
void r2_gm7_controller_scan(bool enabled, bool aligned, uint64_t frame) {
  (void)enabled;
  (void)aligned;
  (void)frame;
}
static void attach(struct bt_conn *c, uint64_t id) {
  c->refs = 1;
  c->role = BT_CONN_ROLE_PERIPHERAL;
  c->state = BT_CONN_STATE_CONNECTED;
  c->interval = 16;
  c->timeout = 100;
  c->address = (bt_addr_le_t){.type = 1, .a = {{3, 4, 5, 6, 7, 0xc0}}};
  callbacks.connected(c, 0);
  assert(peers[0].conn == c && c->refs == 2);
  assert(!r2_ble_admission_bind(id, c->address.a.val));
  att_mtu_updated(c, 226, 226);
  peers[0].discovered = 1;
  peers[0].handle = 42;
  assert(r2_ble_ready(4) && r2_ble_admission_peer() == 4 &&
         r2_ble_peer_identity(4, NULL) == id);
  assert(index_conn(c) == 0 && index_addr(&c->address) == 0 && self == 255 &&
         nodes == 1);
}
static void diagnostic_layout(void){
 uint32_t guarded[R2_GM_TRANSPORT_WORDS+2];for(unsigned i=0;i<ARRAY_SIZE(guarded);i++)guarded[i]=0xabcdef01;
 uint32_t expected[R2_GM_TRANSPORT_WORDS]={
  [0]=1,[2]=32,[3]=8,[4]=255,[6]=1,[7]=255,[10]=1000000,
  [12]=1, /* one actual connected callback */
  [72]=999,[74]=1,[75]=323,[76]=226,[77]=6000000,[79]=UINT32_MAX,
  [80]=7,[81]=(uint32_t)-EBUSY,[82]=3,[83]=2};
 scan_interval=32;scan_window=8;gm_adv_calls=7;gm_adv_result=-EBUSY;gm_adv_errors=3;gm_mtu_errors=2;
 unsigned scans=scan_starts,calls=fake_gatt_calls;
 r2_ble_gm_diagnostics(guarded+1);gm_diagnostic_publish();r2_ble_gm_diagnostics(guarded+1);
 assert(sizeof gm_diag_state==24*sizeof(uint32_t));
 assert(guarded[0]==0xabcdef01&&guarded[R2_GM_TRANSPORT_WORDS+1]==0xabcdef01);
 assert(!memcmp(guarded+1,expected,sizeof expected));
 assert(scans==scan_starts&&calls==fake_gatt_calls); /* observer creates no radio work */
}
static void client_bootstrap(void) {
  const uint8_t own[6] = {1, 2, 3, 4, 5, 0xc0};
  assert(r2_ble_closed());
  r2_ble_gm7_server(false);
  r2_ble_group_client(true);
  r2_ble_client_bootstrap(true, true);
  r2_ble_client_window(true);
  assert(!r2_ble_group_start(123, 456, 1, own));
  for (unsigned renewal = 0; renewal < 2; renewal++) {
    fake_time = UINT64_C(3000000) + renewal * UINT64_C(3000000);
    bt_addr_le_t address = {.type = 1, .a = {{9, 8, 7, 6, 5, 0xc0}}};
    uint8_t wire[25] = {24, BT_DATA_MANUFACTURER_DATA, 'G', '1', 1};
    r2_group_put(wire + 5, 456, 8);
    r2_group_put(wire + 13, 1, 4);
    r2_group_put(wire + 17, 999, 8);
    struct net_buf_simple ad = {.data = wire, .len = sizeof wire};
    /* Renewal preference remains original parent; locator itself is no grant.
     */
    r2_ble_admission_policy(renewal ? 999 : 0, false);
    scan_report(&address, -40, BT_GAP_ADV_TYPE_ADV_IND, &ad);
    unsigned before = creates;
    fake_time += 40000;
    r2_ble_poll(fake_time);
    assert(creates == before + 1 && peers[0].conn == &outgoing &&
           !r2_ble_admission_server());
    att_mtu_updated(&outgoing, 226, 226);
    r2_ble_poll(fake_time + 1);
    assert(peers[0].lookup.func && !r2_ble_ready(4));
    located(&outgoing, 0, &peers[0].lookup, discovery_marker,
            sizeof discovery_marker);
    assert(r2_ble_ready(4) && r2_ble_peer_identity(4, NULL) == 999);
    uint8_t data[R2_AUTH_BYTES] = {12, R2_AUTH_SERVICE};
    assert(!r2_ble_send(4, data));
    tx_done(&outgoing, &peers[0].completion[0]);
    size_t length = r2_control_wire_size(data);
    assert(write_record(&outgoing, NULL, data, length, 0, 0) ==
           (ssize_t)length);
    struct r2_ble_event event;
    assert(r2_ble_receive(&event) && event.peer == 4 &&
           event.incarnation == peers[0].incarnation &&
           !memcmp(event.data, data, length));
    /* An old queued incarnation remains rejected, and cannot become logical4.
     */
    queued_event = event;
    queued_event.peer = 0;
    queued_event.incarnation--;
    queue_live = true;
    assert(r2_ble_receive(&event) && event.peer == 255);
    r2_ble_admission_release();
    callbacks.disconnected(&outgoing, 19);
    r2_ble_poll(fake_time + 2);
    assert(!peers[0].conn && outgoing.refs == 1);
    bt_conn_unref(&outgoing);
  }
  r2_ble_stop();
  r2_ble_poll(fake_time + 3);
  assert(r2_ble_closed());
  /* The shared transport can return to GM with no client policy owner live. */
  r2_ble_gm7_server(true);
  assert(!r2_ble_group_start(123, 456, 1, own));
  assert(!group_client && gm7_server && self == 255 && !peers[0].conn);
  r2_ble_stop();
  assert(r2_ble_closed());
}
int main(void) {
  const uint8_t own[6] = {1, 2, 3, 4, 5, 0xc0};
  uint8_t addresses[4][6] = {{0}};
  assert(PEERS == 1 && sizeof peers == sizeof(struct peer));
  assert(r2_ble_start(0, 4, addresses) == -ENOTSUP);
  assert(r2_ble_field_start(1, (uint64_t[4]){1, 2, 3, 4}, addresses) ==
         -ENOTSUP);
  assert(!transport_started && !r2_ble_group_start(123, 456, 1, own));
  r2_ble_gm7_server(true);
  r2_ble_group_relay(true);
  r2_ble_gm7_source(true); /* Explicit live-source policy in this transport fixture. */
  assert(local_identity == 123 && !peers[0].identity &&
         !r2_ble_control_available() && !r2_ble_control_member());
  struct bt_conn first = {0};
  attach(&first, 999);
  diagnostic_layout();
  uint8_t low[R2_AUTH_BYTES] = {12, R2_AUTH_HELLO},
          high[R2_AUTH_BYTES] = {12, R2_AUTH_SERVICE};
  for (unsigned i = 0; i < 8; i++)
    if (i != 4) {
      struct peer saved = peers[0];
      unsigned dc = disconnects;
      uint8_t untouched[6] = {9, 9, 9, 9, 9, 9};
      assert(!r2_ble_ready(i) && !r2_ble_peer_identity(i, untouched) &&
             !r2_ble_parameters(i));
      assert(r2_ble_send(i, low) == -ENOTCONN);
      r2_ble_peer_authenticated(i, true);
      r2_ble_disconnect(i);
      r2_ble_public_client(i);
      r2_ble_topology(i, 255);
      assert(!memcmp(&saved, &peers[0], sizeof saved) && disconnects == dc &&
             untouched[0] == 9);
    }
  assert(r2_ble_admission_promote() == -EACCES);
  assert(!r2_ble_send(4, low) && !r2_ble_send(4, high) &&
         atomic_get(&peers[0].tx_busy) == 3);
  uint32_t incarnation = peers[0].incarnation;
  r2_ble_stop();
  assert(!r2_ble_closed() && peers[0].disconnect_requested && first.refs == 2);
  callbacks.disconnected(&first, 19);
  r2_ble_poll(fake_time + 1);
  assert(peers[0].conn == &first &&
         !r2_ble_closed()); /* immutable TX still owns row */
  tx_done(&first, &peers[0].completion[0]);
  r2_ble_poll(fake_time + 2);
  assert(peers[0].conn == &first && !r2_ble_closed());
  tx_done(&first, &peers[0].completion[1]);
  r2_ble_poll(fake_time + 3);
  assert(!peers[0].conn && first.refs == 1 && r2_ble_closed());
  bt_conn_unref(&first);
  assert(!r2_ble_group_start(123, 456, 1, own));
  struct bt_conn second = {0};
  attach(&second, 888);
  atomic_t busy_before = peers[0].tx_busy;
  callbacks.disconnected(&first, 19);
  att_mtu_updated(&first, 23, 23);
  tx_done(&first, &peers[0].completion[0]);
  assert(peers[0].conn == &second && !peers[0].dead &&
         peers[0].att_mtu == 226 && peers[0].tx_busy == busy_before);
  assert(peers[0].incarnation != incarnation || &first != &second);
  assert(!r2_ble_send(4, low));
  callbacks.disconnected(&first, 19);
  tx_done(&first, &peers[0].completion[0]);
  assert(peers[0].tx_busy == 1); /* stale connection cannot release new TX */
  r2_ble_stop();
  callbacks.disconnected(&second, 19);
  tx_done(&second, &peers[0].completion[0]);
  r2_ble_poll(fake_time + 4);
  assert(r2_ble_closed() && second.refs == 1);
  bt_conn_unref(&second);
  client_bootstrap();
  puts("PASS production GM7 JOIN logical4/private physical0; retired indices "
       "rejected without mutation, self locator, normal MTU/readiness, both "
       "immutable TX slots, disconnect/drain/restart and stale connection "
       "callbacks");
  return 0;
}
