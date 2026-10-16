/* RC7-only transient GATT owner. No GM-control admission or fixed leaf list.
 * JOIN4 is consumed only as a normal GM client; RQ7 serves local demand.
 */
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/random/random.h>
static struct bt_uuid_128 lt_service_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x48f6e538, 0x76c4, 0x4a94, 0x8c38, 0xd4e251bc0007));
static struct bt_uuid_128 lt_data_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x48f6e538, 0x76c4, 0x4a94, 0x8c38, 0xd4e251bd0007));
static struct bt_uuid_128 gm_data_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x850d0401, 0xa6ef, 0x4775, 0x974b, 0x188a7d4ed45c));
static struct rc5_gm_context gm_context;
static struct r2_join_exchange gm_exchange;
/* Last authenticated GM admission result only; diagnostic, never authority.
 * schema, result(1 codec/2 exchange/3 non-root/4 coarse gate/5 accepted),
 * proof round us, independent source bound us, remaining us, source flags,
 * computed coarse uncertainty us. Read/export under the existing mutex. */
static uint32_t gm_admission_diagnostic[7];
/* Owner-written, source/boot-bound diagnostics survive logical RC restarts.
 * No callback reads controller state or changes a recovery deadline. */
static uint32_t lt_diagnostic[20];
static uint64_t lt_attempt_started;
static uint64_t gm_sequence, lt_challenge, lt_deadline, lt_renew_at,
    lt_request_at, lt_next, lt_budget_until, lt_attempt_ready, lt_adv_check_at;
static uint32_t lt_sequence, lt_requests, lt_responses, lt_bindings, lt_rejects,
    lt_attempts, lt_initiations, lt_disconnects, lt_adv_recoveries;
static bool lt_paused, lt_acquiring, lt_advertising, lt_dead,
    lt_server, lt_server_pending, lt_reply_ready, gm_write_busy;
static struct rc5_demand lt_demand;
static struct bt_le_ext_adv *lt_adv;
static struct bt_conn *lt_conn;
struct lt_retired_connection {
  struct bt_conn *conn;
  bool ended, requested;
};
static struct lt_retired_connection lt_retired[CONFIG_BT_MAX_CONN];
static bool lt_retirement_error;
static struct rc7_control lt_request, lt_incoming, lt_reply;
static uint8_t lt_wire[212], lt_read_wire[212], lt_server_wire[212],
    gm_wire[R2_GROUP_JOIN_BYTES];
static size_t lt_read_size;
static uint16_t lt_handle;
enum lt_phase {
  LT_IDLE,
  LT_CONNECT,
  LT_MTU,
  LT_CAP,
  LT_WRITE,
  LT_REPLY,
  LT_GM_REQUEST,
  LT_GM_PROOF,
  LT_CLOSE
};
static enum lt_phase lt_phase;
static struct bt_gatt_exchange_params lt_exchange;
static struct bt_gatt_read_params lt_read;
static struct bt_gatt_write_params lt_write;
enum lt_event_type {
  LT_CONNECTED,
  LT_DISCONNECTED,
  LT_MTU_DONE,
  LT_DISCOVERED,
  LT_WRITTEN,
  LT_READ,
  LT_SERVER_WRITE,
  LT_GM_WRITE,
  LT_GM_SENT
};
struct lt_event {
  enum lt_event_type type;
  struct bt_conn *conn;
  uint8_t error;
  uint16_t handle;
  uint8_t wire[212];
};
K_MSGQ_DEFINE(lt_events, sizeof(struct lt_event), 4, 4);
K_MUTEX_DEFINE(lt_reply_lock);
static uint64_t lt_random(void) {
  uint64_t n;
  do {
    sys_rand_get(&n, sizeof n);
  } while (!n);
  return n;
}
static uint64_t lt_now(void) {
  struct r2_capture c;
  if (!r2_capture_now(&c)) {
    check(false, "GATT capture");
    return 0;
  }
  return c.tick;
}
static bool lt_live(uint64_t tick) {
  return transmitter || rc5_demand_live(&lt_demand, tick / 16) || parent_known;
}
static void lt_event_put(struct lt_event *e) {
  int rc = k_msgq_put(&lt_events, e, K_NO_WAIT);
  if (rc && e->type == LT_CONNECTED)
    bt_conn_unref(e->conn);
  check(!rc, "bounded RC7 callback queue");
}
static void lt_record(unsigned event, int result, unsigned phase) {
  uint64_t now = mono_us();
  r2_timing_event(event==1?R2_TIMING_RC_CONNECT:event==2?R2_TIMING_RC_CONNECTED:
                 event==3?R2_TIMING_RC_ERROR:R2_TIMING_RC_CLOSE,
                 result,parent_known?routes.node[parent_index].id:0,phase,
                 lt_attempt_started&&now>=lt_attempt_started?(uint32_t)((now-lt_attempt_started)/1000):0,
                 lt_attempts);
  k_mutex_lock(&state_lock, K_FOREVER);
  lt_diagnostic[0] = 1;
  if (event == 1) {
    if (lt_diagnostic[18] != UINT32_MAX) ++lt_diagnostic[18];
    if (result && lt_diagnostic[19] != UINT32_MAX) ++lt_diagnostic[19];
    if (!result && lt_diagnostic[1] != UINT32_MAX) ++lt_diagnostic[1];
    lt_diagnostic[2] = (uint32_t)result;
    lt_diagnostic[14] = (uint32_t)parent.a.val[0] |
        (uint32_t)parent.a.val[1] << 8 | (uint32_t)parent.a.val[2] << 16 |
        (uint32_t)parent.a.val[3] << 24;
    lt_diagnostic[15] = (uint32_t)parent.a.val[4] |
        (uint32_t)parent.a.val[5] << 8 | (uint32_t)parent.type << 16;
    uint64_t age = UINT64_MAX;
    for (unsigned i = 0; i < 8; ++i)
      if (parent_known && candidates[i].id == routes.node[parent_index].id &&
          now >= candidates[i].seen)
        age = (now - candidates[i].seen) / 1000;
    lt_diagnostic[16] = age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
    lt_diagnostic[17] = scanning | (creating << 1) | (lt_paused << 2) |
        (lt_advertising << 3);
  } else if (event == 2) {
    unsigned index = result ? 4 : 3;
    if (lt_diagnostic[index] != UINT32_MAX) ++lt_diagnostic[index];
    lt_diagnostic[5] = (uint32_t)result;
  } else if (event == 3) {
    if (lt_diagnostic[6] != UINT32_MAX) ++lt_diagnostic[6];
    lt_diagnostic[7] = phase;
    lt_diagnostic[8] = (uint32_t)result;
  } else if (event == 4) {
    if (lt_diagnostic[9] != UINT32_MAX) ++lt_diagnostic[9];
    lt_diagnostic[10] = (uint32_t)result;
    lt_diagnostic[11] = phase;
  }
  if (result || event == 4) lt_diagnostic[12] = (uint32_t)(now / 1000);
  uint64_t elapsed = lt_attempt_started && now >= lt_attempt_started ?
      (now - lt_attempt_started) / 1000 : 0;
  lt_diagnostic[13] = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
  k_mutex_unlock(&state_lock);
}
static void lt_disconnect(unsigned reason) {
  if (lt_conn && lt_phase != LT_CLOSE) {
    int rc = bt_conn_disconnect(lt_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    lt_record(4, rc, reason);
    if (rc && rc != -ENOTCONN)
      check(false, "GATT disconnect/cancel");
    lt_phase = LT_CLOSE;
  }
}
static bool lt_reset(void) {
  /* A late event is an ownership failure, never permission to discard refs
   * and start another controller owner. The dispatcher will contain failure.
   * Successful close has already retired the advertising callback source. */
  bool clean = !lt_conn && !lt_adv;
  for (unsigned i = 0; i < CONFIG_BT_MAX_CONN; ++i)
    if (lt_retired[i].conn) clean = false;
  struct lt_event e;
  while (!k_msgq_get(&lt_events, &e, K_NO_WAIT)) {
    if (e.type == LT_CONNECTED) bt_conn_unref(e.conn);
    clean = false;
  }
  if (!clean) return false;
  lt_attempt_started = 0;
  memset(gm_admission_diagnostic, 0, sizeof gm_admission_diagnostic);
  gm_context = (struct rc5_gm_context){0};
  gm_exchange = (struct r2_join_exchange){0};
  lt_demand = (struct rc5_demand){0};
  gm_sequence = lt_challenge = lt_deadline = lt_renew_at = lt_request_at =
      lt_next = lt_budget_until = lt_attempt_ready = lt_adv_check_at =
          last_context_progress = 0;
  lt_sequence = lt_requests = lt_responses = lt_bindings = lt_rejects =
      lt_attempts = lt_initiations = lt_disconnects = lt_adv_recoveries = 0;
  parent_relay = lt_paused = lt_acquiring = lt_advertising = lt_dead =
      lt_server = lt_server_pending = lt_reply_ready = gm_write_busy = false;
  lt_adv = NULL;
  lt_conn = NULL;
  lt_phase = LT_IDLE;
  lt_read_size = 0;
  lt_handle = 0;
  return true;
}
static struct rc7_control capability(void) {
  struct rc7_control c = {.type = RC7_CAPABILITY,
                          .sender = self_identity,
                          .sender_boot = local_boot,
                          .next_challenge = lt_challenge};
  if (make_publication_path()) {
    c.path = publication_path;
    c.publishing = downstream_ready(lt_now());
  }
  return c;
}
static void lt_publish_reply(const struct rc7_control *c) {
  uint8_t wire[212];
  if (rc7_control_encode(wire, c, &group)) {
    check(false, "RQ7 reply encode");
    return;
  }
  k_mutex_lock(&lt_reply_lock, K_FOREVER);
  memcpy(lt_server_wire, wire, 212);
  k_mutex_unlock(&lt_reply_lock);
}
static ssize_t lt_server_read(struct bt_conn *conn,
                              const struct bt_gatt_attr *attr, void *buf,
                              uint16_t len, uint16_t offset) {
  if (!r2_switch_rc_active() || conn != lt_conn || !lt_server)
    return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
  k_mutex_lock(&lt_reply_lock, K_FOREVER);
  ssize_t ret =
      bt_gatt_attr_read(conn, attr, buf, len, offset, lt_server_wire, 212);
  k_mutex_unlock(&lt_reply_lock);
  return ret;
}
static ssize_t lt_server_write(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr, const void *buf,
                               uint16_t len, uint16_t offset, uint8_t flags) {
  (void)attr;
  (void)flags;
  if (!r2_switch_rc_active() || conn != lt_conn || !lt_server || offset ||
      len != 212)
    return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
  struct lt_event e = {.type = LT_SERVER_WRITE, .conn = conn};
  memcpy(e.wire, buf, len);
  if (k_msgq_put(&lt_events, &e, K_NO_WAIT))
    return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
  return len;
}
BT_GATT_SERVICE_DEFINE(
    rc7_local_service, BT_GATT_PRIMARY_SERVICE(&lt_service_uuid),
    BT_GATT_CHARACTERISTIC(&lt_data_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           lt_server_read, lt_server_write, NULL));
int r2_rc7_gm_receive(struct bt_conn *conn, const void *buf, uint16_t len,
                      uint16_t offset) {
  if (!r2_switch_rc_active() || parent_relay || conn != lt_conn || lt_server ||
      offset || len != R2_GROUP_JOIN_BYTES)
    return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
  struct lt_event e = {.type = LT_GM_WRITE, .conn = conn};
  memcpy(e.wire, buf, len);
  if (k_msgq_put(&lt_events, &e, K_NO_WAIT))
    return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
  return len;
}
static void lt_connected(struct bt_conn *conn, uint8_t error) {
  if (!r2_switch_rc_active() || !status.active)
    return;
  /* Incoming ACLs are routed by the actual advertising-set callback. Never
   * adopt a persistent UCS1 connection into the transient RQ7 owner. */
  struct bt_conn_info incoming;
  if (!error && !bt_conn_get_info(conn, &incoming) &&
      incoming.role == BT_CONN_ROLE_PERIPHERAL)
    return;
#if defined(R2_RC_DYNAMIC_FIXTURE)
  struct bt_conn_info info = {0};
  int result = bt_conn_get_info(conn, &info);
  printk("RC7_CONNECTION self=%016llx at=%llu error=%u role=%u info=%d "
         "interval=%u supervision=%u\n",
         (unsigned long long)self_identity, (unsigned long long)mono_us(),
         error, info.role, result, info.le.interval, info.le.timeout);
#endif
  struct lt_event e = {
      .type = LT_CONNECTED, .conn = bt_conn_ref(conn), .error = error};
  lt_event_put(&e);
}
static void lt_disconnected(struct bt_conn *conn, uint8_t reason) {
  if (!r2_switch_rc_active())
    return;
  if (r2_uc_owns(conn))
    return;
  struct lt_event e = {.type = LT_DISCONNECTED, .conn = conn, .error = reason};
  lt_event_put(&e);
}
BT_CONN_CB_DEFINE(rc7_connections) = {.connected = lt_connected,
                                      .disconnected = lt_disconnected};
static void lt_adv_connected(struct bt_le_ext_adv *adv,
                             struct bt_le_ext_adv_connected_info *info) {
  (void)adv;
  struct lt_event e = {.type = LT_CONNECTED, .conn = bt_conn_ref(info->conn)};
  lt_event_put(&e);
}
static const struct bt_le_ext_adv_cb lt_adv_callbacks = {
    .connected = lt_adv_connected};
static void lt_mtu_done(struct bt_conn *conn, uint8_t error,
                        struct bt_gatt_exchange_params *p) {
  (void)p;
  struct lt_event e = {.type = LT_MTU_DONE, .conn = conn, .error = error};
  if (!error && bt_gatt_get_mtu(conn) < 215)
    e.error = 1;
  lt_event_put(&e);
}
static void lt_written(struct bt_conn *conn, uint8_t error,
                       struct bt_gatt_write_params *p) {
  (void)p;
  struct lt_event e = {.type = LT_WRITTEN, .conn = conn, .error = error};
  lt_event_put(&e);
}
static uint8_t lt_read_done(struct bt_conn *conn, uint8_t error,
                            struct bt_gatt_read_params *p, const void *data,
                            uint16_t length) {
  struct lt_event e = {.type = LT_READ, .conn = conn, .error = error};
  if (lt_read_size + length > 212)
    e.error = 1;
  if (!e.error && data) {
    memcpy(lt_read_wire + lt_read_size, data, length);
    lt_read_size += length;
    if (lt_read_size < 212)
      return BT_GATT_ITER_CONTINUE;
    if (!p->handle_count)
      e.handle = p->by_uuid.start_handle;
  }
  if (!e.error && lt_read_size != 212)
    e.error = 1;
  if (!e.error)
    memcpy(e.wire, lt_read_wire, 212);
  lt_event_put(&e);
  return BT_GATT_ITER_STOP;
}
static uint8_t gm_located(struct bt_conn *conn, uint8_t error,
                          struct bt_gatt_read_params *p, const void *data,
                          uint16_t length) {
  struct lt_event e = {.type = LT_DISCOVERED,
                       .conn = conn,
                       .error = error,
                       .handle = p->by_uuid.start_handle};
  if (!data || length != 5 || memcmp(data, "R2G1\1", 5))
    e.error = 1;
  lt_event_put(&e);
  return BT_GATT_ITER_STOP;
}
static void gm_sent(struct bt_conn *conn, void *user) {
  (void)user;
  struct lt_event e = {.type = LT_GM_SENT, .conn = conn};
  lt_event_put(&e);
}
static bool gm_renew_time(struct r2_join_time *out,
                          const struct r2_join_record *r, uint64_t proof,
                          uint64_t received, uint64_t tick) {
  /* Operator-approved RC-only renewal clarification: authentication renews
   * the unchanged binding, not precision. Keep the original reply-age and
   * permission subtraction, but allow RC7's conservative coarse interval.
   * Initial admission/new scope still use the normal GM adapter's gate. */
  struct rc5_stamp held;
  if (!r || r->kind != R2_JOIN_ACCEPT || !r->source_flags ||
      !gm_context.authenticated || !timing.bound ||
      tick >= timing.context_until || holdover_expired ||
      !rc5_clock_capture(&local_clock, &timing, tick, &held) ||
      held.ns >= current_origin.expires ||
      r->sender != gm_context.source.sender ||
      r->sender_boot != gm_context.source.sender_boot ||
      r->root != gm_context.source.root ||
      r->root_boot != gm_context.source.root_boot ||
      r->source_epoch != gm_context.source.source_epoch ||
      r->publication != gm_context.source.publication ||
      r->root_boot != timing.root_boot ||
      r->source_epoch != timing.root_epoch ||
      r->publication != timing.parent_train || received < proof ||
      received > INT64_MAX / 4 || !r->source_bound_us ||
      r->source_bound_us > 50000)
    return false;
  uint64_t round = received - proof;
  if (round >= R2_GROUP_JOIN_TTL_US)
    return false;
  uint64_t margin = (round + 499) / 500 + 64;
  uint64_t guard = round + r->source_bound_us + margin;
  uint64_t uncertainty = (round + 1) / 2 + r->source_bound_us + margin;
  /* RC7's admitted root interval is at most1.2s wide. Under the retained
   * 250ms reply-age and50ms source-bound limits this is at most351128us. */
  if (uncertainty > 600000 || r->remaining_us <= guard)
    return false;
  *out = (struct r2_join_time){.utc_us = r->utc_us + round / 2,
                               .until = received + r->remaining_us - guard,
                               .uncertainty_us = (uint32_t)uncertainty};
  return true;
}
static void gm_admission_record(const struct r2_join_record *r, uint64_t proof,
                                 uint64_t received, unsigned result) {
  uint64_t round = received >= proof ? received - proof : UINT64_MAX;
  uint64_t uncertainty = round <= UINT32_MAX && r
      ? (round + 1) / 2 + r->source_bound_us + (round + 499) / 500 + 64
      : UINT64_MAX;
  uint32_t view[7] = {1, result,
      round > UINT32_MAX ? UINT32_MAX : (uint32_t)round,
      r ? r->source_bound_us : 0, r ? r->remaining_us : 0,
      r ? r->source_flags : 0,
      uncertainty > UINT32_MAX ? UINT32_MAX : (uint32_t)uncertainty};
  k_mutex_lock(&state_lock, K_FOREVER);
  memcpy(gm_admission_diagnostic, view, sizeof view);
  k_mutex_unlock(&state_lock);
  r2_timing_event(R2_TIMING_GM_ADMISSION,(int)result,r?r->sender:0,
                 r?r->source_flags:0,view[2],view[6]);
}
static bool gm_accept(const struct r2_join_record *r, uint64_t proof,
                      uint64_t received, uint64_t tick) {
  struct r2_join_time time;
  bool valid = r && !r->control && r->sender == r->root &&
               r->sender_boot == r->root_boot &&
               (r2_group_join_time(&time, r, proof, received,
                                   r->source_bound_us) ||
                gm_renew_time(&time, r, proof, received, tick));
  gm_admission_record(r, proof, received, valid ? 5 :
      r && !r->control && r->sender == r->root &&
      r->sender_boot == r->root_boot ? 4 : 3);
#if defined(R2_RC_DYNAMIC_FIXTURE)
  printk("RC7_GM_TIME self=%016llx round_us=%llu bound_us=%u accepted=%u "
         "coarse_uncertainty_us=%u\n",
         (unsigned long long)self_identity,
         (unsigned long long)(received >= proof ? received - proof : UINT64_MAX),
         r ? r->source_bound_us : 0, valid,
         valid ? time.uncertainty_us : 0);
#endif
  if (!valid)
    return false;
  gm_context = (struct rc5_gm_context){.source = *r,
                                       .time = time,
                                       .at_tick = tick,
                                       .authenticated = true,
                                       .rebind = true};
  return true;
}
static void gm_apply(const uint8_t *wire) {
  struct r2_join_record r;
  uint64_t tick = lt_now(), now = tick / 16;
  int decoded = r2_group_join_decode(&r, wire, 164, &group);
  if (decoded || r.control || r2_group_join_receive(&gm_exchange, &r, now)) {
    gm_admission_record(decoded ? NULL : &r, gm_exchange.proof_at,
                        now, decoded ? 1 : 2);
    ++lt_rejects;
    lt_deadline = 0;
    return;
  }
  if (r.kind == R2_JOIN_CHALLENGE) {
    lt_phase = LT_GM_PROOF;
    lt_next = 0;
  } else if (r.kind == R2_JOIN_ACCEPT) {
    bool bound = gm_accept(&r, gm_exchange.proof_at, now, tick);
    if (bound) {
      ++lt_bindings;
      startup_complete();
      lt_attempts = 0;
      lt_budget_until = 0;
      last_context_progress = tick;
    } else {
      /* Authenticated GM followers advertise discovery, but cannot provide
       * direct root timing. Try other locators after the existing cooldown. */
      ++lt_rejects;
      candidate_defer(r.sender, now);
      if(r.sender!=r.root||r.sender_boot!=r.root_boot)
        candidate_nonroot(r.sender,mono_us());
    }
    lt_renew_at = tick + (bound ? RC5_RENEW_TICKS : 16000000);
    lt_deadline = 0;
  }
}
static int gm_send(unsigned kind, uint64_t tick) {
  if (gm_write_busy)
    return 0;
  uint64_t now = tick / 16;
  if (kind == R2_JOIN_REQUEST &&
      r2_group_join_begin(&gm_exchange, false, self_identity,
                          routes.node[parent_index].id, local_boot, lt_random(),
                          now))
    return -EINVAL;
  if (gm_sequence == UINT64_MAX)
    return -EOVERFLOW;
  struct r2_join_record r = {
      .kind = kind,
      .sender = self_identity,
      .receiver = routes.node[parent_index].id,
      .sender_boot = local_boot,
      .receiver_boot = gm_exchange.peer_boot,
      .client_nonce = gm_exchange.nonce,
      .server_nonce = gm_exchange.remote_nonce,
      .sequence = ++gm_sequence,
      .created_us = now,
      .echo_us = kind == R2_JOIN_PROOF ? gm_exchange.challenge_at : 0};
  snprintf(r.name, sizeof r.name, "%s", identity.unit);
  int rc = r2_group_join_encode(gm_wire, &r, &group);
  if (rc)
    return rc;
  rc = bt_gatt_write_without_response_cb(lt_conn, lt_handle, gm_wire, 164,
                                         false, gm_sent, NULL);
  if (!rc) {
    gm_write_busy = true;
    lt_phase = LT_CONNECT;
    ++lt_requests;
    if (kind == R2_JOIN_REQUEST)
      gm_exchange.request_at = now;
    else
      gm_exchange.proof_at = now;
  }
  return rc;
}
/* Translate already authenticated GR3; preserve its exact immutable envelope.
 */
static bool gm_packet(struct rc7_packet *out, const uint8_t *wire, size_t size,
                      uint64_t tick) {
  if (exclude_direct || !gm_context.authenticated ||
      tick < gm_context.at_tick ||
      tick - gm_context.at_tick >= RC5_CONTEXT_TICKS)
    return false;
  uint64_t ids[4] = {gm_context.source.root, 0, 0, 0};
  struct r2_relay_packet p;
  if (r2_group_relay_decode3(&p, wire, size, &group, ids) || p.route.depth ||
      p.route.transmitter || p.route.parent || p.origin.root)
    return false;
  if (p.origin.boot != gm_context.source.root_boot ||
      p.route.boot != gm_context.source.sender_boot ||
      p.origin.source_epoch != gm_context.source.source_epoch ||
      p.route.train != gm_context.source.publication) {
    gm_context.authenticated = false;
    lt_renew_at = 0;
    (void)stop();
    timing.bound = false;
    rc5_withdraw(&timing);
    local_clock = (struct rc5_clock){.hold_ticks = RC_FIELD_HOLD_TICKS};
    fit = (struct rc5_rate_fit){0};
    sent_count = 0;
    discipline_at = 0;
    return false;
  }
  struct rc7_path path = {.source_epoch = p.origin.source_epoch,
                          .depth = 0,
                          .node = {{.id = ids[0],
                                    .boot = p.route.boot,
                                    .train = p.route.train,
                                    .epoch = p.route.capture_epoch}}};
  uint64_t elapsed = (tick - gm_context.at_tick) / 16,
           utc = gm_context.time.utc_us + elapsed,
           margin =
               gm_context.time.uncertainty_us + elapsed * 300 / 1000000 + 1;
  if (utc > UINT64_MAX / 1000 || margin > UINT64_MAX / 1000)
    return false;
  uint64_t coarse = utc * 1000, spread = margin * 1000;
  if (p.origin.utc_offset_ns >= 0) {
    uint64_t offset = (uint64_t)p.origin.utc_offset_ns;
    if (coarse < offset)
      return false;
    coarse -= offset;
  } else {
    uint64_t offset = (uint64_t)(-(p.origin.utc_offset_ns + 1)) + 1;
    if (coarse > UINT64_MAX - offset)
      return false;
    coarse += offset;
  }
  if (coarse < spread || coarse > UINT64_MAX - spread)
    return false;
  if (!rc7_path_same(&upstream_path, &path) || gm_context.rebind) {
    struct rc5_admission a = {
        .parent_boot = p.route.boot,
        .root_boot = p.origin.boot,
        .root_epoch = p.origin.source_epoch,
        .parent_train = p.route.train,
        .parent_epoch = p.route.capture_epoch,
        .publication_floor = p.route.publication ? p.route.publication - 1 : 0,
        .now_tick = tick,
        .root_lo_ns = coarse - spread,
        .root_hi_ns = coarse + spread,
        .root_expires_ns = p.origin.expires_ns};
    if (!layout_path(&path) ||
        !rc7_bind(&receiver, &path, self_identity,
                  routes.node[node].transmitter, routes.node[node].slot, &a))
      return false;
    timing.routes = routes;
    gm_context.rebind = false;
    r2_link_quality_bind(&delivery, ids[0], p.route.boot,
                         p.route.capture_epoch);
    last_context_progress = tick;
  }
  *out = (struct rc7_packet){.origin = {.root = ids[0],
                                        .boot = p.origin.boot,
                                        .epoch = p.origin.source_epoch,
                                        .issued = p.origin.created_ns,
                                        .expires = p.origin.expires_ns,
                                        .utc_offset = p.origin.utc_offset_ns,
                                        .frame = p.origin.frame_ns,
                                        .hold_us = p.origin.hold_horizon_us,
                                        .hold_ns = p.origin.hold_budget_ns},
                             .sender = ids[0],
                             .boot = p.route.boot,
                             .train = p.route.train,
                             .publication = p.route.publication,
                             .epoch = p.route.capture_epoch,
                             .count = p.count};
  for (unsigned i = 0; i < p.count; i++) {
    if (p.correction[i].ticks > UINT64_MAX / 125)
      return false;
    out->correction[i] =
        (struct rc5_correction){.publication = p.correction[i].publication,
                                .event = p.correction[i].event,
                                .channel = p.correction[i].channel,
                                .generation = p.correction[i].generation,
                                .tx_ns = p.correction[i].ticks * 125 / 2,
                                .bound = UINT32_MAX};
  }
  return !rc7_origin_sign(out->proof, &out->origin, &group);
}
static void lt_apply_connected(struct bt_conn *conn, uint8_t error) {
  if (error) {
    if (conn == lt_conn) {
      lt_record(2, error, lt_phase);
      lt_dead = true;
    }
    return;
  }
  struct bt_conn_info info;
  if (bt_conn_get_info(conn, &info)) {
    check(false, "GATT info");
    return;
  }
  if (conn == lt_conn && info.role == BT_CONN_ROLE_CENTRAL)
    lt_record(2, 0, lt_phase);
  if (!lt_conn)
    lt_conn = bt_conn_ref(conn);
  else if (conn != lt_conn) {
    (void)bt_conn_disconnect(conn, BT_HCI_ERR_CONN_LIMIT_EXCEEDED);
    return;
  }
  lt_server = info.role == BT_CONN_ROLE_PERIPHERAL;
  if (lt_server) {
    lt_advertising = false;
    lt_deadline = lt_now() + 32000000;
    if (lt_challenge == UINT64_MAX) {
      lt_deadline = 0;
      return;
    }
    ++lt_challenge;
    lt_reply_ready = lt_server_pending = false;
    lt_phase = LT_IDLE;
    struct rc7_control cap = capability();
    lt_publish_reply(&cap);
  } else
    lt_phase = LT_MTU;
}
static void lt_apply_read(const struct rc7_control *c) {
  uint64_t tick = lt_now();
  lt_next = 0;
  if (lt_phase == LT_CAP && c->type == RC7_CAPABILITY &&
      c->sender == routes.node[parent_index].id) {
    /* The path can change after discovery. A loop or an exhausted RC depth
     * is a refused candidate, not a fatal codec/controller failure. */
    if (!parent_path_usable(&c->path)) {
      ++lt_rejects;
      lt_deadline = 0;
      lt_renew_at = tick + 16000000;
      parent_known = false;
      timing.bound = false;
      rc5_withdraw(&timing);
      (void)stop();
      return;
    }
    if (lt_sequence == UINT32_MAX) {
      check(false, "RQ7 sequence exhausted");
      return;
    }
    lt_request = (struct rc7_control){.type = RC7_REQUEST,
                                      .sender = self_identity,
                                      .target = c->sender,
                                      .sender_boot = local_boot,
                                      .target_boot = c->sender_boot,
                                      .challenge = c->next_challenge,
                                      .nonce = lt_random(),
                                      .sequence = ++lt_sequence,
                                      .lease_us = RC_DYNAMIC_LEASE_US,
                                      .path = c->path};
    /* Delivery loss is not evidence of slot contention. Production clients
     * request/renew demand only; the explicit fixture can still inspect. */
#if defined(R2_RC_DYNAMIC_FIXTURE)
    extern bool r2_rc7_fixture_rescan(uint64_t, uint64_t);
    if (r2_rc7_fixture_rescan(self_identity, tick))
      lt_request.operation = 1;
#endif
    lt_phase = LT_WRITE;
  } else if (lt_phase == LT_REPLY && c->type == RC7_RESPONSE &&
             c->sender == lt_request.target && c->target == self_identity &&
             c->sender_boot == lt_request.target_boot &&
             c->target_boot == local_boot &&
             c->challenge == lt_request.challenge &&
             c->nonce == lt_request.nonce &&
             c->sequence == lt_request.sequence) {
    uint64_t age_ns =
        (((tick - lt_request_at) * 125 / 2) * 1000000000 + 999699999) /
        999700000;
    bool bound = false;
    if (c->ready && parent_path_usable(&c->path) && !rc7_path_empty(&c->path) &&
        c->root_hi_ns <= UINT64_MAX - age_ns) {
      struct rc5_admission a = {
          .parent_boot = c->sender_boot,
          .root_boot = c->path.node[0].boot,
          .root_epoch = c->path.source_epoch,
          .parent_train = c->path.node[c->path.depth].train,
          .parent_epoch = c->path.node[c->path.depth].epoch,
          .publication_floor = c->publication_floor,
          .now_tick = tick,
          .root_lo_ns = c->root_lo_ns,
          .root_hi_ns = c->root_hi_ns + age_ns,
          .root_expires_ns = c->source_expires_ns};
      if (layout_path(&c->path))
        bound =
            rc7_bind(&receiver, &c->path, self_identity,
                     routes.node[node].transmitter, routes.node[node].slot, &a);
      if (bound) {
        ++lt_bindings;
        startup_complete();
        last_context_progress = tick;
        r2_link_quality_bind(&delivery, c->sender, c->sender_boot,
                             c->path.node[c->path.depth].epoch);
      }
    }
    lt_renew_at = tick + (bound ? RC5_RENEW_TICKS : 16000000);
    if (bound) {
      lt_attempts = 0;
      lt_budget_until = 0;
    } else if (!c->ready && parent_path_usable(&c->path)) {
      /* Authenticated PENDING is progress, not a failed transaction. The
       * fixed parent wait still expires; repeated PENDING cannot rearm it. */
      lt_attempts = 0;
      lt_budget_until = 0;
      parent_wait(1);
    }
#if defined(R2_RC_DYNAMIC_FIXTURE)
    printk("RC7_CONTROL client=%016llx ready=%u bound=%u op=%u depth=%u "
           "floor=%u lo=%llu hi=%llu expires=%llu age=%llu\n",
           (unsigned long long)self_identity, c->ready, bound,
           lt_request.operation, c->path.depth, c->publication_floor,
           (unsigned long long)c->root_lo_ns, (unsigned long long)c->root_hi_ns,
           (unsigned long long)c->source_expires_ns,
           (unsigned long long)age_ns);
#endif
    lt_deadline = 0;
  } else {
    ++lt_rejects;
    lt_deadline = 0;
  }
}
static void lt_drain(void) {
  struct lt_event e;
  while (!k_msgq_get(&lt_events, &e, K_NO_WAIT)) {
    if (e.type == LT_CONNECTED) {
      lt_apply_connected(e.conn, e.error);
      bt_conn_unref(e.conn);
      continue;
    }
    if (e.conn != lt_conn)
      continue;
    if (e.type == LT_DISCONNECTED) {
      r2_timing_event(R2_TIMING_RC_DISCONNECTED,e.error,
                     parent_known?routes.node[parent_index].id:0,lt_phase,
                     lt_attempt_started?(uint32_t)((mono_us()-lt_attempt_started)/1000):0,lt_server);
      ++lt_disconnects;
      lt_dead = true;
      continue;
    }
    if (e.error) {
      lt_record(3, e.error, lt_phase | (e.type << 16));
      lt_deadline = 0;
      continue;
    }
    switch (e.type) {
    case LT_MTU_DONE:
      lt_next = 0;
      lt_phase = LT_CAP;
      break;
    case LT_DISCOVERED:
      lt_handle = e.handle;
      lt_next = 0;
      lt_phase = LT_GM_REQUEST;
      break;
    case LT_GM_WRITE:
      gm_apply(e.wire);
      break;
    case LT_GM_SENT:
      gm_write_busy = false;
      break;
    case LT_WRITTEN:
      lt_phase = LT_REPLY;
      lt_next = lt_now() + 160000;
      break;
    case LT_READ: {
      struct rc7_control c;
      if (rc7_control_decode(&c, e.wire, 212, &group)) {
        ++lt_rejects;
        lt_deadline = 0;
        break;
      }
      if (e.handle)
        lt_handle = e.handle;
      lt_apply_read(&c);
      break;
    }
    case LT_SERVER_WRITE:
      if (rc7_control_decode(&lt_incoming, e.wire, 212, &group) ||
          lt_incoming.type != RC7_REQUEST ||
          lt_incoming.target != self_identity ||
          lt_incoming.target_boot != local_boot ||
          lt_incoming.challenge != lt_challenge || lt_server_pending ||
          lt_reply_ready) {
        ++lt_rejects;
        break;
      }
      lt_server_pending = true;
      break;
    default:
      break;
    }
  }
}
static void lt_quiesce(bool pause) {
  lt_paused = pause;
  if (pause && lt_advertising) {
    check(!bt_le_ext_adv_stop(lt_adv), "GATT acquisition yield");
    lt_advertising = false;
  }
  if (pause && lt_conn)
    lt_disconnect(3);
}
static void lt_guard(uint64_t tick, bool window) {
  (void)tick;
  lt_paused = !window || lt_acquiring;
}
static void slot_hint_update(void) {
  if (!lt_adv)
    return;
  struct rc7_control c = capability();
  uint8_t hint[RC_DYNAMIC_CAPABILITY_BYTES];
  if (rc7_capability_encode(hint, &c, &group)) {
    check(false, "RQ7 capability encode");
    return;
  }
  static const uint8_t flags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;
  struct bt_data data[] = {
      {.type = BT_DATA_FLAGS, .data_len = 1, .data = &flags},
      {.type = BT_DATA_UUID128_ALL,
       .data_len = 16,
       .data = lt_service_uuid.val},
      {.type = BT_DATA_MANUFACTURER_DATA,
       .data_len = sizeof hint,
       .data = hint}};
  check(!bt_le_ext_adv_set_data(lt_adv, data, 3, NULL, 0),
        "RQ7 capability data");
}
static void lt_init(void) {
  lt_challenge = lt_random() & UINT64_C(0x7fffffffffffffff);
  if (!lt_challenge)
    lt_challenge = 1;
  if (transmitter) {
    struct bt_le_adv_param p = {
        .id = 0,
        .sid = 13,
        .options = BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_EXT_ADV |
                   BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_NO_2M,
        .interval_min = 48,
        .interval_max = 48};
    check(!bt_le_ext_adv_create(&p, &lt_adv_callbacks, &lt_adv), "RQ7 capability create");
    slot_hint_update();
  }
}
static void lt_poll(uint64_t tick, bool window) {
  lt_drain();
#if defined(R2_RC_DYNAMIC_FIXTURE)
  static uint64_t report_at;
  uint64_t report_now = mono_us();
  if (report_now >= 100000000 && report_now >= report_at) {
    report_at = report_now + 5000000;
    printk("RC7_TRANSPORT self=%016llx at=%llu tick=%llu renew=%llu next=%llu "
           "paused=%u acquire=%u conn=%u phase=%u attempts=%u initiations=%u "
           "disconnects=%u advertise=%u adv_recoveries=%u\n",
           (unsigned long long)self_identity, (unsigned long long)report_now,
           (unsigned long long)tick, (unsigned long long)lt_renew_at,
           (unsigned long long)lt_next, lt_paused, lt_acquiring,
           lt_conn != NULL, lt_phase, lt_attempts, lt_initiations,
           lt_disconnects, lt_advertising, lt_adv_recoveries);
  }
  extern bool r2_rc7_fixture_renew(uint64_t, uint64_t);
  if (timing.bound && r2_rc7_fixture_renew(self_identity, tick))
    lt_renew_at = 0;
#endif
  if (lt_dead && lt_conn) {
    bt_conn_unref(lt_conn);
    lt_conn = NULL;
    gm_write_busy = lt_dead = lt_server = false;
    lt_phase = LT_IDLE;
    lt_next = tick + 320000;
  }
  if (lt_conn) {
    if (!window || tick >= lt_deadline) {
      lt_disconnect(!window ? 2 : !lt_deadline ? 1 : 6);
      return;
    }
    if (lt_server) {
      if (lt_server_pending) {
        lt_server_pending = false;
        struct rc7_control *r = &lt_incoming;
        if (!rc5_demand_begin(&lt_demand, local_boot, r->sender_boot,
                              r->challenge, tick / 16) ||
            !rc5_demand_accept(&lt_demand, local_boot, r->sender_boot,
                               r->challenge, r->sequence, r->lease_us,
                               tick / 16)) {
          ++lt_rejects;
          lt_disconnect(4);
          return;
        }
        if (r->operation) {
          ++slot_requests;
          if (!slot_state.scanning && !slot_move_pending &&
              tick / 16 >= slot_state.retry_at &&
              (!slot_state.changed_at ||
               tick / 16 - slot_state.changed_at >= RC_SLOT_COOLDOWN_US))
            slot_change_requested = true;
        }
        lt_reply = (struct rc7_control){.type = RC7_RESPONSE,
                                        .sender = self_identity,
                                        .target = r->sender,
                                        .sender_boot = local_boot,
                                        .target_boot = r->sender_boot,
                                        .challenge = r->challenge,
                                        .nonce = r->nonce,
                                        .sequence = r->sequence,
                                        .lease_us = r->lease_us,
                                        .next_challenge = lt_challenge};
        if (make_publication_path())
          lt_reply.path = publication_path;
        struct rc5_stamp held;
        if (!slot_state.scanning &&
            !slot_move_pending && downstream_ready(tick) &&
            rc5_clock_capture(&local_clock, &timing, tick, &held)) {
          uint64_t spread = 1000000 + (uint64_t)held.age_us * 300 / 1000;
          if (held.ns <= UINT64_MAX - spread &&
              current_origin.expires > held.ns + spread) {
            lt_reply.ready = true;
            lt_reply.publication_floor = publication;
            lt_reply.root_lo_ns = held.ns > spread ? held.ns - spread : 0;
            lt_reply.root_hi_ns = held.ns + spread;
            lt_reply.source_expires_ns = current_origin.expires;
          }
        }
#if defined(R2_RC_DYNAMIC_FIXTURE)
        extern bool r2_rc7_fixture_pending(uint64_t, uint64_t, uint64_t);
        if (r2_rc7_fixture_pending(self_identity, r->sender, tick)) {
          lt_reply.ready = false;
          lt_reply.publication_floor = 0;
          lt_reply.root_lo_ns = lt_reply.root_hi_ns =
              lt_reply.source_expires_ns = 0;
        }
#endif
        lt_publish_reply(&lt_reply);
#if defined(R2_RC_DYNAMIC_FIXTURE)
        printk("RC7_CONTROL server=%016llx ready=%u running=%u phase=%u "
               "sent=%u tx=%u selected=%u scan=%u moving=%u op=%u\n",
               (unsigned long long)self_identity, lt_reply.ready, running,
               phase_admitted, sent_count, tx, slot_selected,
               slot_state.scanning, slot_move_pending, r->operation);
#endif
        lt_reply_ready = true;
        ++lt_responses;
      }
      return;
    }
    if (tick < lt_next)
      return;
    int rc = 0;
    enum lt_phase attempted_phase = lt_phase;
    switch (lt_phase) {
    case LT_MTU:
      lt_exchange.func = lt_mtu_done;
      lt_phase = LT_CONNECT;
      rc = bt_gatt_exchange_mtu(lt_conn, &lt_exchange);
      break;
    case LT_GM_REQUEST:
      rc = gm_send(R2_JOIN_REQUEST, tick);
      break;
    case LT_GM_PROOF:
      rc = gm_send(R2_JOIN_PROOF, tick);
      break;
    case LT_CAP:
    case LT_REPLY:
      if (!parent_relay) {
        lt_read = (struct bt_gatt_read_params){
            .func = gm_located,
            .handle_count = 0,
            .by_uuid = {.start_handle = 1,
                        .end_handle = 0xffff,
                        .uuid = &gm_data_uuid.uuid}};
        lt_next = UINT64_MAX;
        rc = bt_gatt_read(lt_conn, &lt_read);
        break;
      }
      lt_read_size = 0;
      lt_read = (struct bt_gatt_read_params){
          .func = lt_read_done,
          .handle_count = 1,
          .single = {.handle = lt_handle, .offset = 0}};
      if (lt_phase == LT_CAP) {
        lt_read.handle_count = 0;
        lt_read.by_uuid.start_handle = 1;
        lt_read.by_uuid.end_handle = 0xffff;
        lt_read.by_uuid.uuid = &lt_data_uuid.uuid;
      }
      lt_next = UINT64_MAX;
      rc = bt_gatt_read(lt_conn, &lt_read);
      break;
    case LT_WRITE:
      check(!rc7_control_encode(lt_wire, &lt_request, &group),
            "RQ7 request encode");
      lt_write = (struct bt_gatt_write_params){.func = lt_written,
                                               .handle = lt_handle,
                                               .offset = 0,
                                               .data = lt_wire,
                                               .length = 212};
      lt_request_at = tick;
      lt_phase = LT_CONNECT;
      rc = bt_gatt_write(lt_conn, &lt_write);
      ++lt_requests;
      break;
    default:
      break;
    }
    if (rc) {
      lt_record(3, rc, attempted_phase);
      lt_deadline = 0;
    }
    return;
  }
  if (tick < lt_next)
    return;
  bool need = parent_known && window && !lt_paused && !creating &&
              (tick >= lt_renew_at);
  if (!need)
    lt_attempt_ready = 0;
  if (need && tick >= lt_budget_until) {
    if (!lt_attempt_ready) {
      /* Finish an accepted client first, then reserve this short admission
       * opportunity. Child retries must not starve their upstream binding.
       * This yields only connectable admission, never periodic timing RF.
       */
      if (lt_advertising) {
        check(!bt_le_ext_adv_stop(lt_adv), "GATT upstream admission yield");
        lt_advertising = false;
      }
      lt_attempt_ready = tick + (lt_random() % 80001) * 16;
      return;
    }
    if (tick < lt_attempt_ready)
      return;
    lt_attempt_ready = 0;
    if (lt_attempts >= 3) {
      lt_attempts = 0;
      lt_budget_until = tick + 160000000;
      lt_renew_at = lt_budget_until;
      return;
    }
    if (lt_advertising) {
      check(!bt_le_ext_adv_stop(lt_adv), "GATT upstream yield");
      lt_advertising = false;
    }
    if (scanning) {
      check(!bt_le_scan_stop(), "GATT scan yield");
      scanning = false;
    }
    ++lt_attempts;
    ++lt_initiations;
    lt_attempt_started = mono_us();
    lt_deadline = tick + 32000000;
    lt_phase = LT_CONNECT;
    lt_next = 0;
    struct rc5_stamp held;
    bool renewing = timing.bound && tick < timing.context_until &&
                    !holdover_expired &&
                    rc5_clock_capture(&local_clock, &timing, tick, &held) &&
                    held.ns < current_origin.expires;
    /* The default initiator scans continuously. In the pinned split LL that
     * permits a preempted scan to resume and queue central-offset work again
     * before its earlier deferred work has run (observed controller assert).
     * A finite window uses the stock cancel-on-preemption path instead. Keep
     * advanced scheduling and every assertion; leave room for PA and UCS1.
     * These are public GAP units (625us); the original2s attempt still applies.
     */
    struct rc_admission_scan scan=rc_admission_scan(lt_random());
    const struct bt_conn_le_create_param *create =
        BT_CONN_LE_CREATE_PARAM(BT_CONN_LE_OPT_NONE, scan.interval, scan.window);
    unsigned interval = rc_admission_interval(renewing);
    int rc = bt_conn_le_create(&parent, create,
                               /* Cold admission uses 15ms ACL events; the
                                * GM proof retains its100ms uncertainty gate.
                                * Renewal never quiesces periodic timing/sync.
                                * Original2s lifetime and1s supervision remain.
                                */
                               BT_LE_CONN_PARAM(interval, interval, 0, 100),
                               &lt_conn);
    lt_record(1, rc, lt_phase);
    if (rc) {
      lt_conn = NULL;
      lt_phase = LT_IDLE;
      lt_renew_at = tick + 16000000;
    }
    return;
  }
  if (lt_adv && !lt_paused &&
      (!lt_advertising || tick >= lt_adv_check_at)) {
    /* Connectable advertising can terminate before an accepted-connection
     * callback. Reconcile through the public host API: -EALREADY performs no
     * HCI/RF work. This never stops the periodic timing train or its sync.
     */
    bool expected = lt_advertising;
    if (!expected)
      slot_hint_update();
    int rc = bt_le_ext_adv_start(lt_adv, BT_LE_EXT_ADV_START_DEFAULT);
    check(!rc || rc == -EALREADY, "RQ7 capability start");
    if (!rc || rc == -EALREADY) {
      lt_advertising = true;
      lt_adv_check_at = tick + 16000000;
      if (!rc && expected) {
        ++lt_adv_recoveries;
        slot_hint_update();
      }
    }
  }
}
static struct lt_retired_connection *lt_retired_find(struct bt_conn *conn) {
  for (unsigned i = 0; i < CONFIG_BT_MAX_CONN; ++i)
    if (lt_retired[i].conn == conn) return &lt_retired[i];
  return NULL;
}
static struct lt_retired_connection *lt_retired_take(struct bt_conn *conn) {
  for (unsigned i = 0; i < CONFIG_BT_MAX_CONN; ++i)
    if (!lt_retired[i].conn) {
      lt_retired[i] = (struct lt_retired_connection){.conn = conn};
      return &lt_retired[i];
    }
  lt_retirement_error = true;
  return NULL;
}
static void lt_retire_events(void) {
  struct lt_event e;
  while (!k_msgq_get(&lt_events, &e, K_NO_WAIT)) {
    struct lt_retired_connection *r = lt_retired_find(e.conn);
    if (e.type == LT_CONNECTED) {
      if (e.error) {
        if (r) r->ended = true;
        bt_conn_unref(e.conn);
      } else if (r || r2_uc_owns(e.conn)) {
        bt_conn_unref(e.conn);
      } else if (!lt_retired_take(e.conn)) {
        /* Retain failure, release exactly the event's owned reference. */
        bt_conn_unref(e.conn);
      }
      /* A successful new arrival transfers its event reference to retirement.
       * Closing never starts MTU, discovery, authentication or publication. */
    } else if (e.type == LT_DISCONNECTED && r) {
      r->ended = true;
    }
  }
}
static bool lt_close(void) {
  /* One absolute local drain budget, inside the original outer close. */
  uint64_t until = k_uptime_get() + 500;
  bool closed = true;
  lt_retirement_error = false;
  if (lt_conn) {
    struct lt_retired_connection *r = lt_retired_take(lt_conn);
    if (r) {
      r->ended = lt_dead;
      lt_conn = NULL; /* Reference transfers to the bounded retirement owner. */
      lt_dead = false;
    } else closed = false;
  }
  /* Stop and delete the connectable source before declaring the queued ACLs
   * drained. The pinned host invokes this set's callback inline from its
   * advertising-handle lookup; retired sets cannot enqueue a later arrival. */
  if (lt_adv) {
    int rc = bt_le_ext_adv_stop(lt_adv);
    if (!rc)
      lt_advertising = false;
    else
      closed = false;
  }
  if (lt_adv) {
    int rc = bt_le_ext_adv_delete(lt_adv);
    if (!rc)
      lt_adv = NULL;
    else
      closed = false;
  }
  bool drained = false;
  while ((uint64_t)k_uptime_get() < until) {
    lt_retire_events();
    bool pending = false;
    for (unsigned i = 0; i < CONFIG_BT_MAX_CONN; ++i) {
      struct lt_retired_connection *r = &lt_retired[i];
      if (!r->conn || r->ended) continue;
      pending = true;
      if (!r->requested) {
        int rc = bt_conn_disconnect(r->conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        lt_record(4, rc, 5);
        r->requested = true;
        if (rc && rc != -ENOTCONN) lt_retirement_error = true;
      }
    }
    if (!pending && !k_msgq_num_used_get(&lt_events)) {
      drained = true;
      break;
    }
    k_sleep(K_MSEC(1));
  }
  /* Consume final callbacks even on timeout; never renew the deadline or issue
   * another controller operation. Unfinished references remain owned so a
   * failed close cannot silently start the next profile over live resources. */
  lt_retire_events();
  for (unsigned i = 0; i < CONFIG_BT_MAX_CONN; ++i)
    if (lt_retired[i].conn && lt_retired[i].ended) {
      bt_conn_unref(lt_retired[i].conn);
      lt_retired[i] = (struct lt_retired_connection){0};
    }
  for (unsigned i = 0; i < CONFIG_BT_MAX_CONN; ++i)
    if (lt_retired[i].conn) drained = false;
  return closed && drained && !lt_retirement_error && !lt_adv &&
      !k_msgq_num_used_get(&lt_events);
}
