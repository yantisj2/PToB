/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Real MAC/association/fitter/PT lifecycle. Only physical HAL/HCI are fake. */
#define main timing_baseline_cases
#include "gm7_pt_recovery_test.c"
#undef main
#include "ble_causal_events.h"
int64_t k_uptime_get(void) { return (int64_t)(fake_time / 1000); }
int r2_console_note(const char *s, size_t n, uint64_t until) {
  (void)s;
  (void)n;
  (void)until;
  return -EAGAIN;
}
#ifdef R2_BLE_CAUSAL_EVENTS
static void advance(uint64_t tick) {
  assert(tick >= capture_tick);
  uint64_t delta = tick - capture_tick;
  capture_tick = tick;
  fake_time = capture_us += (delta + 15) / 16;
}
static void extra_packet(unsigned i) {
  uint64_t base = 1000000000, ns = base + (uint64_t)i * 62500000;
  struct r2_enrollment receive_roster = enrollment;
  struct r2_precision_packet p = {.root = 0,
                                  .boot = 77,
                                  .source_epoch = 8,
                                  .train = 9,
                                  .publication = i + 1,
                                  .capture_epoch = 4,
                                  .flags = 1,
                                  .created_ns = ns - 1000000,
                                  .expires_ns =
                                      base + (i / 16 + 1) * UINT64_C(1000000000) + 450000000,
                                  .utc_offset_ns = INT64_C(1792800000000000000),
                                  .count = 1};
  p.correction[0] =
      (struct r2_precise_correction){.publication = i,
                                     .event = (uint16_t)i,
                                     .channel = 3,
                                     .ticks = (base + (uint64_t)(i - 1) * 62500000) * 2 / 125,
                                     .generation = 9,
                                     .expires_ns = p.expires_ns};
  enrollment = receive_roster;
  enrollment.identity[0] = 99;
  enrollment.identity[1] = 17;
  packet = (struct r2_radio_record){.raw = 16000000 + i * 1000000 + 144,
                                    .epoch = 1,
                                    .length = R2_RELAY_AD_BYTES,
                                    .event = (uint16_t)(i + 1),
                                    .channel = 3};
  assert(!generic_encode(packet.ad, &p));
  enrollment = receive_roster;
  fake_time = capture_us = 1000000 + (uint64_t)i * 62500 + 1000;
  capture_tick = (uint64_t)packet.raw + 16000;
  queued = true;
  precision_step(fake_time);
  assert(!queued && !error);
}
static void new_control_scope(void) {
  fake_mutex_unlocked = NULL;
  ++grant.source_epoch;
  ++grant.publication_epoch;
  r2_radio_service_gm7_publish(grant, &anchor, &path, -1, fake_time, anchor.until);
}
#endif
int main(int argc, char **argv) {
  assert(argc == 2);
  unsigned mode = (unsigned)strtoul(argv[1], NULL, 10);
  if (mode >= 90)
    return timing_baseline_cases(argc, argv);
#ifdef R2_BLE_CAUSAL_EVENTS
  setup_pt();
  r2_radio_service_source(&(struct r2_source_clock){0}, false, 0);
  follower();
  precision_poll(fake_time, &running);
  drive_stream(900);
  uint64_t utc;
  assert(r2_radio_service_client_clock(&utc, fake_time));
  struct r2_ble_causal_meta before, after;
  struct r2_ble_causal_record onset, latest, recovery;
  r2_ble_causal_snapshot(0, &onset, NULL, &before);
  assert(before.flags & 4); /* Actual producer readability, not a query trigger. */
  if(mode==11){
   struct r2_radio_precision model=precision;pt_output.valid_until=fake_time-1;
   precision_step(fake_time);r2_ble_causal_snapshot(0,&onset,NULL,&after);
   assert(after.losses==before.losses&&after.recoveries==before.recoveries&&after.atomic_refreshes==before.atomic_refreshes+1);
   assert(r2_radio_service_client_clock(&utc,fake_time)&&!memcmp(&model.servo,&precision.servo,sizeof model.servo));
   puts("PASS actual atomic valid replacement suppresses same-commit false LOSS/RECOVERY, counter retained, model unchanged");return 0;
  }
  if (mode == 4) {
    struct r2_radio_precision original = precision;
    fake_mutex_unlocked = new_control_scope;
    precision_step(fake_time);
    r2_ble_causal_snapshot(1, &latest, NULL, &after);
    assert(r2_radio_service_client_clock(&utc, fake_time) && after.losses == before.losses);
    assert(latest.flags & R2_BLE_AVAILABLE);
    assert(latest.flags & R2_BLE_BOUND);
    assert(!(latest.flags & R2_BLE_SCOPE_EQUAL));
    assert(latest.source == 8 && latest.header_source == 8 && latest.grant_source == 9 &&
           latest.train == 9 && latest.grant_train == 10);
    assert(!memcmp(&original.servo, &precision.servo, sizeof original.servo));
    puts("PASS actual CONTROL/PT in-flight fullscope difference retained as descriptive; original "
         "identity_follow clock contract unchanged, no false LOSS");
    return 0;
  }
  if (mode == 9) {
    capture_test_epoch = 2;
    precision_step(fake_time);
    r2_ble_causal_snapshot(0, &onset, NULL, &after);
    assert(after.losses == before.losses + 1 && onset.epoch == 2 && onset.publication_epoch == 1);
    assert(onset.flags & R2_BLE_SAMPLE_KNOWN);
    assert(!(onset.flags & R2_BLE_CAPTURED));
    assert(!(onset.flags & R2_BLE_MODEL_KNOWN));
    char text[2976];
    assert(r2_ble_causal_json(text, sizeof text, 1, 11) > 0);
    assert(strstr(text, "\"physical_remaining_us\":null") &&
           strstr(text, "\"delivered_model_source_ns_at_sample\":null"));
    puts("PASS actual counterchange keeps sampleepoch and originalpublicationepoch distinct; no "
         "oldbound ages or model-source projection across epoch");
    return 0;
  }
  if (mode == 10) {
    struct pt_output original = pt_output, invalid = pt_output;
    struct r2_radio_precision model = precision;
    invalid.gm7.retained = true;
    invalid.gm7.map.valid = true;
    invalid.gm7.map.delivery = capture_tick-16;
    invalid.gm7.map.anchor = INT64_MAX;
    invalid.gm7.map.phase = 1;
    invalid.gm7.map.rate = 0;
    new_control_scope();
    struct r2_capture at = {capture_tick, 1, R2_CAPTURE_NOW};
    causal_locked(&invalid, invalid.scope, at, fake_time, fake_time, true, true, false, UINT32_MAX,
                  0, 0);
    r2_ble_causal_snapshot(1, &latest, NULL, NULL);
    assert(!(latest.flags & R2_BLE_MODEL_KNOWN));
    assert(!memcmp(&original, &pt_output, sizeof original) &&
           !memcmp(&model, &precision, sizeof model));
    puts("PASS checked published-map source projection overflow remains unknown and cannot alter "
         "clock/model authority");
    return 0;
  }
  if (mode == 8) {
    pt_input.grant.until_us = fake_time;
    assert(!r2_radio_service_client_clock(&utc, fake_time));
    precision_step(fake_time);
    r2_ble_causal_snapshot(0, &onset, NULL, &after);
    assert(after.losses == before.losses + 1 && onset.effective_until == onset.grant_until &&
           onset.effective_until == fake_time);
    assert(R2_BLE_PRIMARY(onset) == R2_BLE_P_CONTROL_PERMISSION && !(onset.flags & R2_BLE_RETIRED));
    puts("PASS event effectivekernel permission is clipped to currentgrant, not unbounded "
         "oldpublication; no fabricated explicitretirement");
    return 0;
  }
  if (mode == 6) {
    /* Source/control grant retirement reaches the real final PT pass and
     * then idle. It must retain old source/bounds before scope is zeroed. */
    uint64_t physical = pt_output.capture_until, data = pt_output.gm7.data_until;
    pt_input.grant = (struct r2_radio_scope){0};
    assert(running && !precision_needed(fake_time));
    precision_poll(fake_time, &running);
    assert(!running && !precision.scope.boot);
    r2_ble_causal_snapshot(0, &onset, NULL, &after);
    assert(after.losses == before.losses + 1 && onset.root == 99 && onset.boot == 77 &&
           onset.source == 8);
    assert(onset.physical_until == physical && onset.data_until == data && onset.grant_root == 0);
    assert(R2_BLE_PRIMARY(onset) == R2_BLE_P_CONTROL_PERMISSION);
    puts("PASS actual last PT pass before idle preserves prior source/rawbounds across natural "
         "inputgrant retirement, not zeroed aftermath");
    return 0;
  }
  if (mode == 1) {
    struct r2_radio_precision original = precision;
    r2_radio_service_gm7_withdraw_cause(72, 99);
    r2_ble_causal_snapshot(0, &onset, NULL, &after);
    assert(after.losses == before.losses + 1 && R2_BLE_PRIMARY(onset) == R2_BLE_P_CONTROL);
    assert(onset.reason == 72 && onset.generation == 99 && onset.caller_pc &&
           (onset.flags & R2_BLE_REASON_KNOWN));
    assert(pt_input.stopped && !r2_radio_service_client_clock(&utc, fake_time) &&
           !memcmp(&original, &precision, sizeof original));
    puts("PASS real still-readable CONTROL retirement captures known "
         "ownerreason/generation/callerPC without estimator mutation");
    return 0;
  }
  assert(pt_output.gm7.data_until < pt_output.capture_until);
  uint64_t until = mode == 3 ? pt_output.capture_until : pt_output.gm7.data_until;
  advance(until + 1);
  if (mode == 5) {
    assert(precision_needed(fake_time));
    precision_poll(fake_time, &running);
    assert(running);
    r2_ble_causal_snapshot(0, &onset, NULL, &after);
    assert(after.losses == before.losses + 1 && onset.root == 99 &&
           R2_BLE_PRIMARY(onset) == R2_BLE_P_DATA);
    puts("PASS natural signed-data expiry via actual PT poll, no console; original continuous-GM7 "
         "following correctly remains running");
    return 0;
  }

  if (mode == 2 || mode == 3) {
    r2_radio_service_gm7_withdraw_cause(72, 99);
    r2_ble_causal_snapshot(0, &onset, NULL, &after);
    r2_ble_causal_snapshot(1, &latest, NULL, NULL);
    assert(after.losses == before.losses + 1 && R2_BLE_PRIMARY(onset) == R2_BLE_P_DATA);
    assert(!(onset.flags & R2_BLE_RETIRED) && !(onset.flags & R2_BLE_REASON_KNOWN));
    assert(latest.flags & R2_BLE_RETIRED);
    assert(latest.reason == 72 && latest.caller_pc);
    assert(onset.data_until < onset.physical_until && onset.tick >= onset.data_until);
    puts("PASS actual delayed CONTROL retirement preserves earlier signed/HFexpiry onset; CONTROL "
         "is subsequent materialcause, not invented first cause");
    return 0;
  }
  if (mode == 7) {
    precision_step(fake_time);
    struct r2_ble_causal_first first, again;
    r2_ble_causal_snapshot(0, &onset, &first, &after);
    assert(after.first_sequence);
    r2_radio_service_withdraw();
    assert(r2_radio_service_closed());
    assert(r2_radio_service_reset(17) == -EAGAIN);
    precision_poll(fake_time, &running);
    assert(r2_radio_service_reset(17) == 0);
    r2_ble_causal_snapshot(0, &latest, &again, &before);
    assert(!memcmp(&first, &again, sizeof first) && before.first_sequence == after.first_sequence);
    puts("PASS actual logical owner reset/profile drain preserves immutable first-loss summary "
         "without any authority/cache renewal");
    return 0;
  }
  struct r2_radio_precision original = precision;
  precision_step(fake_time);
  r2_ble_causal_snapshot(0, &onset, NULL, &after);
  assert(!r2_radio_service_client_clock(&utc, fake_time) && after.losses == before.losses + 1);
  assert(R2_BLE_PRIMARY(onset) == R2_BLE_P_DATA && onset.tick >= onset.data_until &&
         onset.tick < onset.physical_until);
  assert(onset.root == 99 && onset.boot == 77 && onset.source == 8 && onset.train == 9 &&
         onset.capture == 4);
  assert(onset.flags & R2_BLE_HEADER_KNOWN);
  assert(onset.flags & R2_BLE_MATCHED_KNOWN);
  assert(onset.header_created == gm7_direct.created_ns &&
         onset.header_expires == gm7_direct.expires_ns);
  assert(onset.matched_expires == original.origin_expires_ns &&
         onset.matched_created == original.origin_created_ns);
  assert(!memcmp(&original.servo, &precision.servo, sizeof original.servo) &&
         original.data_until == precision.data_until &&
         original.last_accepted == precision.last_accepted);
  struct r2_ble_causal_record saved = onset;
  for (unsigned i = 0; i < 10; i++) {
    advance(capture_tick + 1600);
    precision_step(fake_time);
  }
  r2_ble_causal_snapshot(0, &onset, NULL, &after);
  assert(!memcmp(&saved, &onset, sizeof saved));
  advance(pt_output.capture_until + 1);
  precision_step(fake_time);
  r2_ble_causal_snapshot(1, &latest, NULL, &after);
  assert(latest.why & R2_FOLLOW_PHYSICAL_EXPIRED);
  r2_ble_causal_snapshot(0, &onset, NULL, NULL);
  assert(!memcmp(&saved, &onset, sizeof saved));
  unsigned first = (unsigned)((capture_tick - 16016144 + 999999) / 1000000) + 1;
  bool recovered = false;
  for (unsigned i = first; i < first + 128; i++) {
    extra_packet(i);
    if (r2_radio_service_client_clock(&utc, fake_time)) {
      recovered = true;
      break;
    }
  }
  assert(recovered);
  r2_ble_causal_snapshot(2, &recovery, NULL, &after);
  assert(after.recoveries == before.recoveries + 1 && R2_BLE_KIND(recovery) == R2_BLE_RECOVERY);
  r2_ble_causal_snapshot(0, &onset, NULL, NULL);
  assert(!memcmp(&saved, &onset, sizeof saved));
  struct r2_radio_precision model = precision;
  struct pt_input input = pt_input;
  struct pt_output output = pt_output;
  struct r2_ble_causal_meta counters = after;
  char json[2976];
  for (unsigned page = 10; page <= 13; page++)
    assert(r2_ble_causal_json(json, sizeof json, 1, page) > 0);
  r2_ble_causal_snapshot(0, NULL, NULL, &after);
  assert(!memcmp(&counters, &after, sizeof after) && !memcmp(&model, &precision, sizeof model) &&
         !memcmp(&input, &pt_input, sizeof input) && !memcmp(&output, &pt_output, sizeof output));
  puts("PASS actual fixedsource900MAC PT loss observed without queries; same-snapshot "
       "dataexpiry/raworigin/physicalbounds, repeat/cascade/onset, actual freshMAC recovery, "
       "readonlyhistory no model/permission mutations");
  return 0;
#else
  assert(!"Event-only mode requires capture build");
  return 1;
#endif
}
