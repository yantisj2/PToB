/* SPDX-License-Identifier: Apache-2.0
 * UCS1: four client-paced leaf connections sharing 16 updates/second. No
 * per-client timing timer. RX only captures/copies; the RC owner offers work
 * after precision handling. Notification submission runs on the stock
 * nonblocking system-workqueue path.
 */
#include "rc_unicast.h"
#include "capture.h"
#include "unicast_time.h"
#if defined(R2_SWITCHABLE_FIELD)
#include "ble_service.h"
#endif
#include <errno.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/random/random.h>

#define UC_UUID(n)                                                             \
  BT_UUID_128_ENCODE(0xe8860000u + (n), 0x9746, 0x48de, 0x9fa1, 0x186e9a67c301)
const uint32_t r2_uc_contract[12] = {2,  144, 144,       72,        64, 4,
                                     16, 6,   360000000, 300000000, 3,  500000};
static struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(UC_UUID(0));
static struct bt_uuid_128 state_uuid = BT_UUID_INIT_128(UC_UUID(1));
static struct bt_uuid_128 control_uuid = BT_UUID_INIT_128(UC_UUID(2));
static struct bt_uuid_128 time_uuid = BT_UUID_INIT_128(UC_UUID(3));
static struct k_spinlock lock;
struct uc_client {
  struct bt_conn *conn;
  struct uc1_session session;
  struct r2_capture received;
  uint64_t unauth_until, request_at;
  uint32_t generation, requests, replies, drops;
  uint16_t mtu;
  uint8_t command[UC1_CONTROL_BYTES], probe[UC1_PROBE_BYTES];
  uint8_t state[UC1_STATE_BYTES], wire[UC1_SAMPLE_BYTES];
  bool dead, command_pending, probe_pending, state_pending, state_ready;
  bool first_sample, tx_busy, submitting, tx_done;
  struct bt_gatt_notify_params notification;
};
static struct {
  struct bt_le_ext_adv *adv;
  struct r2_group_config key;
  struct r2_uc_clock clock;
  struct uc_client clients[UC1_CLIENTS];
  uint64_t id, boot, challenge, pool_next, adv_check_at;
  uint32_t generation, trace[3];
  unsigned cursor;
  bool enabled, advertising, trace_pending;
} u;
/* All lookups and slot lifecycle changes are protected by lock. */
static struct uc_client *find(struct bt_conn *conn) {
  for (unsigned i = 0; i < UC1_CLIENTS; i++)
    if (conn && u.clients[i].conn == conn)
      return &u.clients[i];
  return NULL;
}
static void work(struct k_work *);
K_WORK_DEFINE(job, work);
static uint64_t now_us(void) { return k_ticks_to_us_floor64(k_uptime_ticks()); }
static bool rc_owner(void) {
#if defined(R2_SWITCHABLE_FIELD)
  return r2_switch_rc_active();
#else
  return true; /* Standalone transport fixture. */
#endif
}
static uint64_t random64(void) {
  uint64_t n;
  do {
    sys_rand_get(&n, sizeof n);
  } while (!n);
  return n;
}
bool r2_uc_owns(struct bt_conn *conn) {
  k_spinlock_key_t k = k_spin_lock(&lock);
  bool owned = find(conn) != NULL;
  k_spin_unlock(&lock, k);
  return owned;
}
static ssize_t state_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          void *buf, uint16_t len, uint16_t offset) {
  uint8_t copy[UC1_STATE_BYTES];
  k_spinlock_key_t k = k_spin_lock(&lock);
  struct uc_client *c = find(conn);
  bool ok = rc_owner() && u.enabled && c && !c->dead && c->state_ready;
  if (ok)
    memcpy(copy, c->state, sizeof copy);
  k_spin_unlock(&lock, k);
  return ok ? bt_gatt_attr_read(conn, attr, buf, len, offset, copy, sizeof copy)
            : BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
}
static ssize_t control_write(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, const void *data,
                             uint16_t len, uint16_t offset, uint8_t flags) {
  (void)attr;
  (void)flags;
  if (offset || len != UC1_CONTROL_BYTES)
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  k_spinlock_key_t k = k_spin_lock(&lock);
  struct uc_client *c = find(conn);
  bool ok = rc_owner() && u.enabled && c && !c->dead && !c->command_pending;
  if (ok) {
    memcpy(c->command, data, len);
    c->command_pending = true;
  } else if (c)
    ++c->drops;
  k_spin_unlock(&lock, k);
  return ok ? len : BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
}
static ssize_t probe_write(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr, const void *data,
                           uint16_t len, uint16_t offset, uint8_t flags) {
  (void)attr;
  (void)flags;
  struct r2_capture capture;
  /* This is the software callback-entry reference, never an RF timestamp. */
  bool captured = r2_capture_now(&capture);
  uint64_t at = now_us();
  if (offset || len != UC1_PROBE_BYTES || !captured)
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  k_spinlock_key_t k = k_spin_lock(&lock);
  struct uc_client *c = find(conn);
  bool ok = rc_owner() && u.enabled && c && !c->dead && !c->probe_pending;
  if (ok) {
    memcpy(c->probe, data, len);
    c->received = capture;
    c->request_at = at;
    c->probe_pending = true;
    ++c->requests;
  } else if (c)
    ++c->drops;
  k_spin_unlock(&lock, k);
  return ok ? len : BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
}
static void state_ccc(const struct bt_gatt_attr *attr, uint16_t value) {
  (void)attr;
  (void)value; /* Subscription is queried per connection in the worker. */
  k_spinlock_key_t k = k_spin_lock(&lock);
  for (unsigned i = 0; i < UC1_CLIENTS; i++)
    if (u.clients[i].conn)
      u.clients[i].state_pending = true;
  k_spin_unlock(&lock, k);
}
BT_GATT_SERVICE_DEFINE(
    uc_service, BT_GATT_PRIMARY_SERVICE(&service_uuid),
    BT_GATT_CHARACTERISTIC(&state_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, state_read, NULL, NULL),
    BT_GATT_CCC(state_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(&control_uuid.uuid, BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE, NULL, control_write, NULL),
    BT_GATT_CHARACTERISTIC(&time_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY |
                               BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, probe_write, NULL),
    BT_GATT_CCC(state_ccc, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));
static void adv_connected(struct bt_le_ext_adv *adv,
                          struct bt_le_ext_adv_connected_info *info) {
  (void)adv;
  k_spinlock_key_t k = k_spin_lock(&lock);
  u.advertising = false;
  u.adv_check_at = 0;
  struct uc_client *c = NULL;
  for (unsigned i = 0; i < UC1_CLIENTS; i++)
    if (!u.clients[i].conn) {
      c = &u.clients[i];
      break;
    }
  if (!rc_owner() || !u.enabled || !c || u.generation == UINT32_MAX ||
      u.challenge == UINT64_MAX) {
    k_spin_unlock(&lock, k);
    (void)bt_conn_disconnect(info->conn, BT_HCI_ERR_CONN_LIMIT_EXCEEDED);
    return;
  }
  memset(c, 0, sizeof *c);
  c->conn = bt_conn_ref(info->conn);
  c->generation = ++u.generation;
  c->session = (struct uc1_session){.boot = u.boot, .challenge = ++u.challenge};
  c->unauth_until = now_us() + 10000000;
  c->mtu = 23;
  c->state_pending = true;
  k_spin_unlock(&lock, k);
}
static const struct bt_le_ext_adv_cb adv_callbacks = {.connected =
                                                          adv_connected};
static void disconnected(struct bt_conn *conn, uint8_t reason) {
  (void)reason;
  k_spinlock_key_t k = k_spin_lock(&lock);
  struct uc_client *c = find(conn);
  if (c) {
    c->dead = true;
    c->session.hz = 0;
    c->probe_pending = c->command_pending = false;
  }
  k_spin_unlock(&lock, k);
}
BT_CONN_CB_DEFINE(uc_connections) = {.disconnected = disconnected};
static void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx) {
  k_spinlock_key_t k = k_spin_lock(&lock);
  struct uc_client *c = find(conn);
  if (c)
    c->mtu = tx < rx ? tx : rx;
  k_spin_unlock(&lock, k);
}
static struct bt_gatt_cb uc_gatt_callbacks = {.att_mtu_updated = mtu_updated};
static void sent(struct bt_conn *conn, void *user) {
  (void)user;
  k_spinlock_key_t k = k_spin_lock(&lock);
  struct uc_client *c = find(conn);
  if (c) {
    c->tx_done = true;
    if (!c->submitting)
      c->tx_busy = false;
  }
  k_spin_unlock(&lock, k);
}
static bool mapped(const struct r2_uc_clock *clock, const struct r2_capture *at,
                   uint64_t *ns) {
  uint64_t elapsed;
  return clock->valid && at->epoch == clock->capture_epoch &&
         at->tick < clock->until_tick &&
         rc5_map_project(&clock->map, at->tick, ns, &elapsed) &&
         *ns < clock->origin.expires;
}
static uint64_t validity_us(const struct r2_uc_clock *clock,
                            const struct r2_capture *at, uint64_t ns) {
  uint64_t local = (clock->until_tick - at->tick) / 16;
  /* Existing map rates are capped at 300ppm; a 1000ppm conversion margin
   * conservatively clips functional permission. It is not precision evidence.
   */
  uint64_t source = (clock->origin.expires - ns) / 1001;
  return local < source ? local : source;
}
static void make_state(uint8_t *out, const struct uc1_session *s,
                       const struct r2_uc_clock *clock,
                       const struct r2_capture *at, uint64_t now) {
  memset(out, 0, UC1_STATE_BYTES);
  uint64_t ns = 0;
  bool valid = mapped(clock, at, &ns), live = uc1_live(s, now);
  out[4] = live ? s->hz : 0;
  out[5] = (valid ? UC1_SOURCE_VALID : 0) | (live ? UC1_ACTIVE : 0);
  out[6] = s->reason;
  out[7] = clock->depth;
  r2_group_put(out + 8, u.id, 8);
  r2_group_put(out + 16, s->boot, 8);
  r2_group_put(out + 24, s->challenge, 8);
  r2_group_put(out + 32, s->nonce, 8);
  r2_group_put(out + 40, s->stream, 8);
  r2_group_put(out + 48, s->control_seq, 4);
  r2_group_put(out + 52, live ? s->until - now : 0, 4);
  r2_group_put(out + 56, at->tick, 8);
  r2_group_put(out + 64, valid ? ns : 0, 8);
  r2_group_put(out + 72, (uint64_t)clock->origin.utc_offset, 8);
  r2_group_put(out + 80, clock->origin.root, 8);
  r2_group_put(out + 88, clock->origin.boot, 8);
  r2_group_put(out + 96, clock->origin.epoch, 4);
  r2_group_put(out + 100, clock->capture_epoch, 4);
  r2_group_put(out + 104, clock->map.generation, 4);
  r2_group_put(out + 108, clock->map.age_us, 4);
  r2_group_put(out + 112, UC1_UNKNOWN, 4);
  r2_group_put(out + 116, valid ? validity_us(clock, at, ns) : 0, 4);
  r2_group_put(out + 120, (uint32_t)clock->map.ppb, 4);
  r2_group_put(out + 124, 1u | 4u | 8u | 16u, 4);
  (void)uc1_sign(out, UC1_STATE_BYTES, UC1_STATE, &u.key);
}
static void serve(struct uc_client *c) {
  uint8_t command[UC1_CONTROL_BYTES], probe[UC1_PROBE_BYTES],
      wire[UC1_SAMPLE_BYTES];
  struct r2_uc_clock clock;
  struct uc1_session session;
  struct r2_capture received, at;
  struct bt_conn *conn;
  uint64_t request_at;
  uint32_t generation;
  uint16_t mtu;
  bool control, precision, state_notify, time_notify;
  k_spinlock_key_t k = k_spin_lock(&lock);
  if (!rc_owner() || !u.enabled || !c->conn || c->dead || c->tx_busy) {
    k_spin_unlock(&lock, k);
    return;
  }
  conn = c->conn;
  generation = c->generation;
  mtu = c->mtu;
  clock = u.clock;
  session = c->session;
  received = c->received;
  request_at = c->request_at;
  control = c->command_pending || c->state_pending;
  precision = !control && c->probe_pending;
  if (control) {
    memcpy(command, c->command, sizeof command);
    c->state_pending = false;
  }
  if (precision)
    memcpy(probe, c->probe, sizeof probe);
  bool command_pending = c->command_pending;
  if (control)
    c->command_pending = false;
  if (precision)
    c->probe_pending = false;

  k_spin_unlock(&lock, k);
  state_notify =
      bt_gatt_is_subscribed(conn, &uc_service.attrs[2], BT_GATT_CCC_NOTIFY);
  time_notify =
      bt_gatt_is_subscribed(conn, &uc_service.attrs[7], BT_GATT_CCC_NOTIFY);
  if (uc1_live(&session, now_us()) && (!state_notify || !time_notify)) {
    (void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    return;
  }
  if (!r2_capture_now(&at))
    return;
  uint64_t now = now_us();
  if (control) {
    if (command_pending) {
      struct bt_conn_info info;
      uint32_t interval = UINT32_MAX;
      if (!bt_conn_get_info(conn, &info))
        interval = info.le.interval * 1250u;
      if (!state_notify || !time_notify ||
          uc1_control(&session, command, sizeof command, &u.key, now,
                      random64(), mtu, interval) < 0) {
        k = k_spin_lock(&lock);
        ++c->drops;
        k_spin_unlock(&lock, k);
        return;
      }
    }
    if (command_pending && command[4] == UC1_ON && uc1_live(&session, now)) {
      k = k_spin_lock(&lock);
      /* Authentication runs outside the callback lock. A departing client must
       * not consume capacity or lower other grants after its connection dies.
       */
      if (!u.enabled || c->dead || c->conn != conn ||
          c->generation != generation) {
        k_spin_unlock(&lock, k);
        return;
      }
      struct uc1_session prior = c->session;
      c->session = session;
      struct uc_client *ordered[UC1_CLIENTS];
      struct uc1_session *sessions[UC1_CLIENTS];
      for (unsigned i = 0; i < UC1_CLIENTS; i++) {
        ordered[i] = &u.clients[i];
        for (unsigned j = i;
             j && ordered[j]->generation < ordered[j - 1]->generation; j--) {
          struct uc_client *swap = ordered[j];
          ordered[j] = ordered[j - 1];
          ordered[j - 1] = swap;
        }
      }
      uint8_t before[UC1_CLIENTS];
      for (unsigned i = 0; i < UC1_CLIENTS; i++) {
        sessions[i] = &ordered[i]->session;
        before[i] = sessions[i]->hz;
      }
      if (!uc1_pool(sessions, now)) {
        c->session = prior;
        session.hz = 0;
        session.until = now;
        session.reason = UC1_CAPACITY;
      } else {
        session = c->session;
        for (unsigned i = 0; i < UC1_CLIENTS; i++)
          if (before[i] != sessions[i]->hz)
            ordered[i]->state_pending = true;
      }
      k_spin_unlock(&lock, k);
    }
    make_state(wire, &session, &clock, &at, now);
  } else if (precision) {
    if (!time_notify || !uc1_live(&session, now) || now < request_at ||
        now - request_at > UINT32_C(2000000) / session.hz ||
        now - request_at > r2_group_get(probe + 32, 4) ||
        !uc1_probe(&session, probe, sizeof probe, &u.key, request_at) ||
        !uc1_credit(&u.pool_next, now, UC1_POOL_HZ, UC1_CLIENTS)) {
      k = k_spin_lock(&lock);
      ++c->drops;
      k_spin_unlock(&lock, k);
      return;
    }
    memset(wire, 0, sizeof wire);
    uint64_t ns = 0;
    bool valid = mapped(&clock, &at, &ns) && received.epoch == at.epoch;
    wire[4] = session.hz;
    wire[5] = UC1_ACTIVE | (valid ? UC1_SOURCE_VALID : 0);
    wire[6] = valid ? session.reason : UC1_SOURCE;
    wire[7] = clock.depth;
    r2_group_put(wire + 8, session.boot, 8);
    r2_group_put(wire + 16, session.stream, 8);
    r2_group_put(wire + 24, session.nonce, 8);
    r2_group_put(wire + 32, session.probe_seq, 4);
    r2_group_put(wire + 36, clock.map.generation, 4);
    r2_group_put(wire + 40, received.tick, 8);
    r2_group_put(wire + 48, at.tick, 8);
    r2_group_put(wire + 56, valid ? ns : 0, 8);
    r2_group_put(wire + 64, (uint64_t)clock.origin.utc_offset, 8);
    r2_group_put(wire + 72, clock.origin.root, 8);
    r2_group_put(wire + 80, clock.origin.boot, 8);
    r2_group_put(wire + 88, clock.origin.epoch, 4);
    r2_group_put(wire + 92, clock.capture_epoch, 4);
    uint64_t age =
        clock.map.age_us + (at.tick >= clock.map.anchor_tick
                                ? (at.tick - clock.map.anchor_tick) / 16
                                : 0);
    r2_group_put(wire + 96, age > UINT32_MAX ? UINT32_MAX : age, 4);
    r2_group_put(wire + 100, UC1_UNKNOWN, 4);
    r2_group_put(wire + 104, valid ? validity_us(&clock, &at, ns) : 0, 4);
    r2_group_put(wire + 108, (uint32_t)clock.map.ppb, 4);
    memcpy(wire + 112, clock.scope, 16);
    (void)uc1_sign(wire, sizeof wire, UC1_SAMPLE, &u.key);
  } else
    return;
  k = k_spin_lock(&lock);
  if (!u.enabled || c->dead || c->conn != conn || c->generation != generation) {
    k_spin_unlock(&lock, k);
    return;
  }
  c->session = session;
  if (control && command_pending) {
    if (u.trace_pending)
      ++c->drops;
    u.trace[0] = session.control_seq;
    u.trace[1] = UINT32_C(0x55430100) + command[4];
    u.trace[2] = session.hz | ((uint32_t)session.reason << 8);
    u.trace_pending = true;
  }
  if (control) {
    memcpy(c->state, wire, sizeof wire);
    c->state_ready = true;
  }
  bool notify = control ? state_notify : time_notify;
  if (!notify) {
    k_spin_unlock(&lock, k);
    return;
  }
  memcpy(c->wire, wire, sizeof wire);
  c->tx_busy = c->submitting = true;
  c->tx_done = false;
  c->notification =
      (struct bt_gatt_notify_params){.attr = &uc_service.attrs[control ? 2 : 7],
                                     .data = c->wire,
                                     .len = sizeof wire,
                                     .func = sent};
  k_spin_unlock(&lock, k);
  /* In the pinned system workqueue, ATT data admission is nonblocking. This
   * timestamp denotes preparation; API completion is not RF event evidence. */
  int result = bt_gatt_notify_cb(conn, &c->notification);
  k = k_spin_lock(&lock);
  c->submitting = false;
  if (result || c->tx_done)
    c->tx_busy = false;
  if (result)
    ++c->drops;
  else
    ++c->replies;
  if (!result && precision && !c->first_sample) {
    if (u.trace_pending)
      ++c->drops;
    c->first_sample = true;
    u.trace[0] = session.probe_seq;
    u.trace[1] = UINT32_C(0x55430104);
    u.trace[2] = session.hz | ((uint32_t)wire[5] << 8);
    u.trace_pending = true;
  }
  k_spin_unlock(&lock, k);
}
static void work(struct k_work *item) {
  (void)item;
  unsigned first = u.cursor;
  u.cursor = (u.cursor + 1u) % UC1_CLIENTS;
  for (unsigned i = 0; i < UC1_CLIENTS; i++)
    serve(&u.clients[(first + i) % UC1_CLIENTS]);
}
int r2_uc_start(const struct r2_group_config *group, uint64_t id,
                uint64_t boot) {
  if (!rc_owner())
    return -EPERM;
  static bool gatt_registered;
  static int client_identity = -1;
  if (!gatt_registered) {
    bt_gatt_cb_register(&uc_gatt_callbacks);
    gatt_registered = true;
  }
  if (u.enabled || u.adv || k_work_busy_get(&job))
    return -EBUSY;
  for (unsigned i = 0; i < UC1_CLIENTS; i++)
    if (u.clients[i].conn)
      return -EBUSY;
  /* A central connects to an address, not a particular advertising SID.
   * Keep UCS1 separate from the transient RQ7 address so CoreBluetooth cannot
   * accidentally enter its two-second owner. One RAM identity is reused on
   * profile re-entry; no settings/journal write or new RF timer is involved. */
  if (client_identity < 0) {
    int identity = bt_id_create(NULL, NULL);
    if (identity < 0)
      return identity;
    if (identity == BT_ID_DEFAULT || identity >= CONFIG_BT_ID_MAX)
      return -EINVAL;
    client_identity = identity;
  }
  memset(&u, 0, sizeof u);
  u.id = id;
  u.boot = boot;
  u.challenge = random64() & INT64_MAX;
  uc1_key(&u.key, group, id);
  struct bt_le_adv_param parameters = {.id = (uint8_t)client_identity,
                                       .sid = 14,
                                       .options = BT_LE_ADV_OPT_CONN |
                                                  BT_LE_ADV_OPT_USE_IDENTITY,
                                       .interval_min = 510,
                                       .interval_max = 510};
  int result = bt_le_ext_adv_create(&parameters, &adv_callbacks, &u.adv);
  if (result)
    return result;
  const uint8_t flags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;
  const struct bt_data data[] = {
      BT_DATA(BT_DATA_FLAGS, &flags, 1),
      BT_DATA(BT_DATA_UUID128_ALL, service_uuid.val, 16)};
  const char name[] = "PToB RC UCS1";
  const struct bt_data response[] = {
      BT_DATA(BT_DATA_NAME_COMPLETE, name, sizeof name - 1)};
  result = bt_le_ext_adv_set_data(u.adv, data, 2, response, 1);
  if (result) {
    (void)bt_le_ext_adv_delete(u.adv);
    u.adv = NULL;
    return result;
  }
  u.enabled = true;
  return 0;
}
void r2_uc_poll(const struct r2_uc_clock *clock) {
  if (!rc_owner())
    return;
  struct bt_conn *retired[UC1_CLIENTS], *disconnect[UC1_CLIENTS];
  unsigned nretired = 0, ndisconnect = 0, occupied = 0;
  bool advertise, submit = false;
  uint64_t now = now_us();
  k_spinlock_key_t k = k_spin_lock(&lock);
  u.clock = *clock;
  bool idle = !k_work_busy_get(&job);
  for (unsigned i = 0; i < UC1_CLIENTS; i++) {
    struct uc_client *c = &u.clients[i];
    if (c->conn && c->dead && !c->submitting && idle) {
      retired[nretired++] = c->conn;
      c->conn = NULL;
      c->tx_busy = false;
      c->session.hz = 0;
    }
    if (!c->conn)
      continue;
    ++occupied;
    if (!c->dead) {
      if ((!c->session.control_seq && now >= c->unauth_until) ||
          (c->session.control_seq && !uc1_live(&c->session, now)))
        disconnect[ndisconnect++] = bt_conn_ref(c->conn);
      submit |= !c->tx_busy &&
                (c->state_pending || c->command_pending || c->probe_pending);
    }
  }
  /* A controller can terminate advertising without accepting a connection.
   * Reconcile through the public API; EALREADY performs no HCI/RF work. The
   * same elapsed-time check bounds retries on resource errors, without a timer.
   */
  advertise =
      u.enabled && u.adv && occupied < UC1_CLIENTS && now >= u.adv_check_at;
  if (advertise)
    u.adv_check_at = now + UINT64_C(1000000);
  k_spin_unlock(&lock, k);
  for (unsigned i = 0; i < nretired; i++)
    bt_conn_unref(retired[i]);
  for (unsigned i = 0; i < ndisconnect; i++) {
    (void)bt_conn_disconnect(disconnect[i], BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    bt_conn_unref(disconnect[i]);
  }
  if (advertise) {
    int result = bt_le_ext_adv_start(u.adv, BT_LE_EXT_ADV_START_DEFAULT);
    if (!result || result == -EALREADY) {
      k = k_spin_lock(&lock);
      u.advertising = u.enabled;
      k_spin_unlock(&lock, k);
    }
  }
  if (submit)
    (void)k_work_submit(&job);
}
bool r2_uc_close(void) {
  struct bt_conn *connections[UC1_CLIENTS];
  unsigned count = 0;
  k_spinlock_key_t k = k_spin_lock(&lock);
  u.enabled = false;
  for (unsigned i = 0; i < UC1_CLIENTS; i++) {
    struct uc_client *c = &u.clients[i];
    c->command_pending = c->probe_pending = c->state_pending = false;
    if (c->conn)
      connections[count++] = bt_conn_ref(c->conn);
  }
  k_spin_unlock(&lock, k);
  (void)k_work_cancel(&job);
  bool closed = true;
  if (u.adv) {
    int result = bt_le_ext_adv_stop(u.adv);
    if (result && result != -EALREADY)
      closed = false;
    result = bt_le_ext_adv_delete(u.adv);
    if (!result)
      u.adv = NULL;
    else
      closed = false;
  }
  for (unsigned i = 0; i < count; i++) {
    int result =
        bt_conn_disconnect(connections[i], BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (result && result != -ENOTCONN)
      closed = false;
    bt_conn_unref(connections[i]);
  }
  uint64_t until = now_us() + 500000;
  bool done;
  do {
    k = k_spin_lock(&lock);
    done = !k_work_busy_get(&job);
    for (unsigned i = 0; i < UC1_CLIENTS; i++)
      done &=
          (!u.clients[i].conn || u.clients[i].dead) && !u.clients[i].submitting;
    k_spin_unlock(&lock, k);
    if (done)
      break;
    k_sleep(K_MSEC(1));
  } while (now_us() < until);
  if (done) {
    for (unsigned i = 0; i < UC1_CLIENTS; i++) {
      k = k_spin_lock(&lock);
      struct bt_conn *conn = u.clients[i].conn;
      u.clients[i].conn = NULL;
      k_spin_unlock(&lock, k);
      if (conn)
        bt_conn_unref(conn);
    }
  }
  u.advertising = false;
  return closed && done;
}
bool r2_uc_quiescent(void) {
  k_spinlock_key_t k = k_spin_lock(&lock);
  bool quiet = !u.enabled && !u.adv && !u.advertising &&
               !k_work_busy_get(&job);
  for (unsigned i = 0; i < UC1_CLIENTS; ++i)
    quiet &= !u.clients[i].conn && !u.clients[i].submitting;
  k_spin_unlock(&lock, k);
  return quiet;
}
bool r2_uc_trace(uint32_t out[3]) {
  k_spinlock_key_t k = k_spin_lock(&lock);
  bool pending = u.trace_pending;
  if (pending) {
    memcpy(out, u.trace, sizeof u.trace);
    u.trace_pending = false;
  }
  k_spin_unlock(&lock, k);
  return pending;
}
void r2_uc_stats(uint32_t out[8]) {
  k_spinlock_key_t k = k_spin_lock(&lock);
  uint32_t words[8] = {2};
  for (unsigned i = 0; i < UC1_CLIENTS; i++) {
    const struct uc_client *c = &u.clients[i];
    words[1] += c->conn != NULL;
    words[2] += uc1_live(&c->session, now_us()) ? c->session.hz : 0;
    words[3] += c->session.control_seq;
    words[4] += c->session.probe_seq;
    words[5] += c->requests;
    words[6] += c->replies;
    words[7] += c->drops;
  }
  memcpy(out, words, sizeof words);
  k_spin_unlock(&lock, k);
}
bool r2_uc_serving_clients(void) {
  const uint64_t now = now_us();
  k_spinlock_key_t k = k_spin_lock(&lock);
  bool serving = false;
  if (rc_owner() && u.enabled)
    for (unsigned i = 0; i < UC1_CLIENTS; i++) {
      const struct uc_client *c = &u.clients[i];
      serving |= c->conn && !c->dead && uc1_live(&c->session, now) &&
                 bt_gatt_is_subscribed(c->conn, &uc_service.attrs[2],
                                       BT_GATT_CCC_NOTIFY) &&
                 bt_gatt_is_subscribed(c->conn, &uc_service.attrs[7],
                                       BT_GATT_CCC_NOTIFY);
    }
  k_spin_unlock(&lock, k);
  return serving;
}
