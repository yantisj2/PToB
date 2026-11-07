/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual owners observe; main copies/formats/drains. No time authority. */
#include "ble_causal_events.h"
#include "ble_service.h"
#include "build_identity.h"
#include "gm7_timing.h"
#include "timing_events.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifdef CONFIG_ARM
#include <zephyr/kernel.h>
#define R2_LOCK_UNIT 17u
#include "health_lock.h"
static struct k_spinlock causal_lock;
#else
static unsigned causal_lock;
typedef unsigned k_spinlock_key_t;
static unsigned k_spin_lock(unsigned *p) {
  (void)p;
  return 0;
}
static void k_spin_unlock(unsigned *p, unsigned k) {
  (void)p;
  (void)k;
}
#endif
/* No extra journal slots. Complete module mutable state <=984B native;
 * +24B authenticated-header metadata and +16B descriptive PT publication
 * is bounded at1024B by the actual native inspector. */
static struct {
  struct r2_ble_causal_record onset, latest, recovery;
  struct r2_ble_causal_first first;
  struct r2_ble_causal_meta meta;
} causal;
_Static_assert(sizeof(causal) == 976, "Fixed three-record/first-loss/counter state");
_Static_assert(sizeof(causal) + sizeof(causal_lock) <= 984, "Native recorder+lock hard ceiling");
enum {
  SEEN = 1u,
  USABLE = 2u,
  AVAILABLE = 4u,
  DRAIN_INIT = 8u,
  J_SHIFT = 4u,
  N_SHIFT = 8u,
  T_SHIFT = 12u,
  T_MASK = 3u << T_SHIFT
};
static void inc(uint32_t *p) {
  if (*p < UINT32_MAX)
    ++*p;
}
static struct r2_ble_causal_record *slot(unsigned n) {
  return n == 0 ? &causal.onset : n == 1 ? &causal.latest : &causal.recovery;
}
static bool context_same(const struct r2_ble_causal_record *a,
                         const struct r2_ble_causal_record *b) {
  return a->root == b->root && a->boot == b->boot && a->source == b->source &&
         a->train == b->train && a->capture == b->capture && a->epoch == b->epoch &&
         a->grant_root == b->grant_root && a->grant_boot == b->grant_boot &&
         a->grant_source == b->grant_source && a->grant_train == b->grant_train &&
         a->grant_capture == b->grant_capture && a->header_root == b->header_root &&
         a->header_boot == b->header_boot && a->header_source == b->header_source &&
         a->header_train == b->header_train && a->header_capture == b->header_capture &&
         a->header_epoch == b->header_epoch;
}
static unsigned primary(const struct r2_ble_causal_record *r) {
  uint32_t w = r->why;
  if (r->flags & R2_BLE_AVAILABLE)
    return R2_BLE_P_NONE;
  if (r->flags & R2_BLE_RETIRED)
    return R2_BLE_P_CONTROL;
  if (w & (R2_FOLLOW_CAPTURE | R2_FOLLOW_COUNTER | R2_FOLLOW_BACKWARD))
    return R2_BLE_P_CAPTURE;
  bool phys = w & R2_FOLLOW_PHYSICAL_EXPIRED, data = w & R2_FOLLOW_DATA_EXPIRED;
  bool control = w & (R2_FOLLOW_STOPPED | R2_FOLLOW_OWNER_ERROR | R2_FOLLOW_BINDING |
                      R2_FOLLOW_SCOPE_EXPIRED | R2_FOLLOW_GRANT_EXPIRED);
  /* HF bounds are comparable to one another. A control/kernel refusal is
   * another domain: no fabricated ordering if both are first observed late. */
  if ((phys || data) && control)
    return R2_BLE_P_MULTIPLE;
  if (phys && data)
    return r->data_until < r->physical_until ? R2_BLE_P_DATA : R2_BLE_P_PHYSICAL;
  if (data)
    return R2_BLE_P_DATA;
  if (phys)
    return R2_BLE_P_PHYSICAL;
  if (control)
    return R2_BLE_P_CONTROL_PERMISSION;
  if (w & R2_FOLLOW_DATA_MISSING)
    return R2_BLE_P_DATA;
  if (w & R2_FOLLOW_PHYSICAL_MISSING)
    return R2_BLE_P_PHYSICAL;
  if (w & R2_FOLLOW_OUTPUT)
    return R2_BLE_P_OUTPUT;
  if (w & R2_FOLLOW_FIT)
    return R2_BLE_P_FIT;
  if (w & R2_FOLLOW_EFFECTIVE_EXPIRED)
    return R2_BLE_P_KERNEL;
  if (w & R2_FOLLOW_PT_UTC)
    return R2_BLE_P_UTC;
  return R2_BLE_P_UNKNOWN;
}
void r2_ble_causal_atomic_refresh(void) {
  k_spinlock_key_t key = k_spin_lock(&causal_lock);
  if (causal.meta.flags & AVAILABLE) inc(&causal.meta.atomic_refreshes);
  k_spin_unlock(&causal_lock, key);
}
void r2_ble_causal_observe(const struct r2_ble_causal_record *input) {
  if (!input)
    return;
  k_spinlock_key_t key = k_spin_lock(&causal_lock);
  struct r2_ble_causal_meta *m = &causal.meta;
  if (!(input->flags & R2_BLE_MANAGED) && !(m->flags & SEEN)) {
    k_spin_unlock(&causal_lock, key);
    return;
  }
  if (!(input->flags & R2_BLE_FOLLOWING) && !(m->flags & SEEN)) {
    k_spin_unlock(&causal_lock, key);
    return;
  }
  inc(&m->observations);
  m->last_seen_us = input->commit_us;
  m->last_seen_tick = input->tick;
  bool available = input->flags & R2_BLE_AVAILABLE, was = m->flags & AVAILABLE,
       seen = m->flags & SEEN;
  const struct r2_ble_causal_record *previous =
      was && !(causal.latest.flags & R2_BLE_AVAILABLE) ? &causal.recovery : &causal.latest;
  bool context = seen && !context_same(input, previous),
       changed = seen && input->why != previous->why;
  unsigned kind = 0, destination = 1;
  if (!seen)
    kind = available ? R2_BLE_ACQUIRED : R2_BLE_ACQUIRING;
  else if (was && !available) {
    kind = R2_BLE_LOSS;
    destination = 0;
  } else if (!was && available) {
    kind = m->flags & USABLE ? R2_BLE_RECOVERY : R2_BLE_ACQUIRED;
    destination = 2;
  } else if (context)
    kind = R2_BLE_CONTEXT_CHANGE;
  else if (changed)
    kind = R2_BLE_CAUSE_CHANGE;
  else {
    inc(&m->repeats);
    k_spin_unlock(&causal_lock, key);
    return;
  }
  if (m->serial == UINT32_MAX) {
    inc(&m->coalesced);
    k_spin_unlock(&causal_lock, key);
    return;
  }
  struct r2_ble_causal_record r = *input;
  r.flags &= 0xffffu;
  r.sequence = ++m->serial;
  if (kind == R2_BLE_LOSS)
    inc(&m->losses);
  if (kind == R2_BLE_RECOVERY)
    inc(&m->recoveries);
  if (context)
    inc(&m->context_changes);
  if (changed)
    inc(&m->cause_changes);
  r.incident = m->losses;
  r.flags |= primary(&r) << R2_BLE_PRIMARY_SHIFT;
  r.flags |= kind << R2_BLE_KIND_SHIFT;
  if (kind == R2_BLE_LOSS) {
    causal.onset = r;
    if (!m->first_sequence) {
      m->first_sequence = r.sequence;
      causal.first = (struct r2_ble_causal_first){.root = r.root,
                                                  .boot = r.boot,
                                                  .detected_us = r.commit_us,
                                                  .tick = r.tick,
                                                  .epoch = r.epoch,
                                                  .source = r.source,
                                                  .train = r.train,
                                                  .capture = r.capture,
                                                  .primary_flags = r.flags,
                                                  .reason = r.reason,
                                                  .caller_pc = r.caller_pc};
    }
  }
  if (available && (kind == R2_BLE_ACQUIRED || kind == R2_BLE_RECOVERY)) {
    causal.recovery = r;
    destination = 2;
  } else if (destination != 0)
    causal.latest = r;
  if (kind == R2_BLE_LOSS) {
    /* latest is independently rotatable; onset remains the first cause. */
    if (m->flags & ((1u << (J_SHIFT + 1)) | (1u << (N_SHIFT + 1))))
      inc(&m->coalesced);
    m->flags &= ~((1u << (J_SHIFT + 1)) | (1u << (N_SHIFT + 1)));
    causal.latest = r;
  }
  uint32_t pending = (1u << (J_SHIFT + destination)) | (1u << (N_SHIFT + destination));
  if (m->flags & pending)
    inc(&m->coalesced);
  m->flags |= SEEN | pending;
  if (available)
    m->flags |= USABLE | AVAILABLE;
  else
    m->flags &= ~AVAILABLE;
  k_spin_unlock(&causal_lock, key);
}
void r2_ble_causal_snapshot(unsigned n, struct r2_ble_causal_record *r,
                            struct r2_ble_causal_first *f, struct r2_ble_causal_meta *m) {
  k_spinlock_key_t k = k_spin_lock(&causal_lock);
  if (r)
    *r = *slot(n);
  if (f)
    *f = causal.first;
  if (m)
    *m = causal.meta;
  k_spin_unlock(&causal_lock, k);
}
static const char *kind_name(unsigned n) {
  switch (n) {
  case R2_BLE_ACQUIRING:
    return "ACQUIRING";
  case R2_BLE_ACQUIRED:
    return "FIRST_ACQUIRED";
  case R2_BLE_LOSS:
    return "LOSS";
  case R2_BLE_CAUSE_CHANGE:
    return "CAUSE_CHANGE";
  case R2_BLE_CONTEXT_CHANGE:
    return "CONTEXT_CHANGE";
  case R2_BLE_RECOVERY:
    return "RECOVERY";
  default:
    return "NONE";
  }
}
static const char *primary_name(unsigned n) {
  switch (n) {
  case R2_BLE_P_NONE:
    return "NONE";
  case R2_BLE_P_CONTROL:
    return "EXPLICIT_CONTROL_RETIREMENT";
  case R2_BLE_P_CAPTURE:
    return "CAPTURE_UNAVAILABLE_OR_DOMAIN";
  case R2_BLE_P_PHYSICAL:
    return "PHYSICAL_MAP_BOUND";
  case R2_BLE_P_DATA:
    return "SIGNED_DATA_PERMISSION_BOUND";
  case R2_BLE_P_OUTPUT:
    return "DELIVERED_OUTPUT_UNAVAILABLE";
  case R2_BLE_P_FIT:
    return "FIT_UNAVAILABLE_OBSERVED";
  case R2_BLE_P_KERNEL:
    return "KERNEL_PERMISSION_BOUND";
  case R2_BLE_P_UTC:
    return "UTC_UNAVAILABLE";
  case R2_BLE_P_CONTROL_PERMISSION:
    return "CONTROL_PERMISSION_REFUSED";
  case R2_BLE_P_MULTIPLE:
    return "HF_AND_CONTROL_ORDER_UNKNOWN";
  default:
    return "UNKNOWN";
  }
}
static void append(char *out, size_t cap, int *n, const char *format, ...) {
  if (*n < 0)
    return;
  va_list args;
  va_start(args, format);
  int extra = vsnprintf(out + (size_t)*n, cap - (size_t)*n, format, args);
  va_end(args);
  if (extra < 0 || (size_t)extra >= cap - (size_t)*n) {
    *n = -1;
    return;
  }
  *n += extra;
}
static void u64(char *s, size_t z, int *n, const char *name, uint64_t v, bool known) {
  if (known)
    append(s, z, n, ",\"%s\":%llu", name, (unsigned long long)v);
  else
    append(s, z, n, ",\"%s\":null", name);
}
static void i64(char *s, size_t z, int *n, const char *name, int64_t v, bool known) {
  if (known)
    append(s, z, n, ",\"%s\":%lld", name, (long long)v);
  else
    append(s, z, n, ",\"%s\":null", name);
}
static int summary_json(char *out, size_t cap, uint64_t boot) {
  struct r2_ble_causal_first f;
  struct r2_ble_causal_meta m;
  r2_ble_causal_snapshot(0, NULL, &f, &m);
  int n = 0;
  append(out, cap, &n,
         "{\"schema\":1,\"source\":\"%s\",\"boot\":\"%016llx\",\"channel\":\"GM7_DIRECT_BLE_"
         "FOLLOWER\",\"page\":10,\"read_only\":true,\"authority\":false,\"observed\":%s,\"ever_"
         "usable\":%s,\"available_at_last_producer\":%s,\"event_sequence\":%u,\"observations\":%u,"
         "\"losses\":%u,\"recoveries\":%u,\"cause_changes\":%u,\"context_changes\":%u,\"repeats\":%"
         "u,\"atomic_refreshes\":%u,\"coalesced\":%u,\"incidents_overwritten\":%u,\"journal_attempts\":%u,\"last_seen_"
         "kernel_us\":%llu,\"last_seen_hf_tick\":%llu,\"last_seen_hf_epoch\":null,\"sequence_"
         "exhausted\":%s,\"first_loss\":{"
         "\"found\":%s,\"sequence\":%u,\"root\":\"%016llx\",\"root_boot\":\"%016llx\",\"source_"
         "epoch\":%u,\"train\":%u,\"capture_gen\":%u,\"detected_kernel_us\":%llu,\"preceding_"
         "paired_hf_tick\":%llu,\"counter_epoch\":%llu,\"primary\":\"%s\",\"caller_pc\":%u",
         R2_SOURCE_SHA, (unsigned long long)boot, m.flags & SEEN ? "true" : "false",
         m.flags & USABLE ? "true" : "false", m.flags & AVAILABLE ? "true" : "false", m.serial,
         m.observations, m.losses, m.recoveries, m.cause_changes, m.context_changes, m.repeats,
         m.atomic_refreshes, m.coalesced, m.losses ? m.losses - 1 : 0, m.journal_attempts,
         (unsigned long long)m.last_seen_us, (unsigned long long)m.last_seen_tick,
         m.serial == UINT32_MAX ? "true" : "false", m.first_sequence ? "true" : "false",
         m.first_sequence, (unsigned long long)f.root, (unsigned long long)f.boot, f.source,
         f.train, f.capture, (unsigned long long)f.detected_us, (unsigned long long)f.tick,
         (unsigned long long)f.epoch,
         primary_name((f.primary_flags >> R2_BLE_PRIMARY_SHIFT) & 255u), f.caller_pc);
  if (f.primary_flags & R2_BLE_REASON_KNOWN)
    append(out, cap, &n, ",\"control_reason\":%u", f.reason);
  else
    append(out, cap, &n, ",\"control_reason\":null");
  append(out, cap, &n, "}}");
  return n < 0 ? -ENOSPC : n;
}
static const struct {
  uint32_t bit;
  const char *name;
} reasons[] = {{R2_FOLLOW_UNSUPPORTED, "OWNER_NOT_DIRECT_FOLLOWER"},
               {R2_FOLLOW_STOPPED, "CONTROL_STOPPED"},
               {R2_FOLLOW_UNMANAGED, "PT_NOT_MANAGED"},
               {R2_FOLLOW_OWNER_ERROR, "PT_OWNER_ERROR"},
               {R2_FOLLOW_BINDING, "ACTUAL_CONTROL_BINDING_REFUSED"},
               {R2_FOLLOW_CAPTURE, "CAPTURE_UNAVAILABLE"},
               {R2_FOLLOW_COUNTER, "CAPTURE_EPOCH_MISMATCH"},
               {R2_FOLLOW_BACKWARD, "TIME_BEFORE_PUBLICATION"},
               {R2_FOLLOW_SCOPE_EXPIRED, "PT_SCOPE_EXPIRED"},
               {R2_FOLLOW_GRANT_EXPIRED, "CONTROL_GRANT_EXPIRED"},
               {R2_FOLLOW_PHYSICAL_MISSING, "NO_PHYSICAL_MAP_BOUND"},
               {R2_FOLLOW_PHYSICAL_EXPIRED, "PHYSICAL_MAP_EXPIRED"},
               {R2_FOLLOW_DATA_MISSING, "NO_SIGNED_DATA_PERMISSION"},
               {R2_FOLLOW_DATA_EXPIRED, "SIGNED_DATA_PERMISSION_EXPIRED"},
               {R2_FOLLOW_FIT, "FIT_UNAVAILABLE_OBSERVED"},
               {R2_FOLLOW_OUTPUT, "DELIVERED_OUTPUT_UNAVAILABLE"},
               {R2_FOLLOW_SERVO_FAULT, "SERVO_FAULT_OBSERVED"},
               {R2_FOLLOW_PT_UTC, "UTC_UNAVAILABLE_AT_PRODUCER"},
               {R2_FOLLOW_EFFECTIVE_EXPIRED, "EFFECTIVE_PERMISSION_EXPIRED"},
               {R2_BLE_WHY_RETIRED, "EXPLICIT_CONTROL_RETIREMENT"},
               {R2_BLE_WHY_SCOPE_DIFFERENT, "PT_CONTROL_SCOPE_DIFFERENT"}};
static int record_json(char *out, size_t cap, uint64_t boot, unsigned page) {
  struct r2_ble_causal_record r;
  r2_ble_causal_snapshot(page == 11 ? 0 : page == 12 ? 1 : 2, &r, NULL, NULL);
  int n = 0;
  append(out, cap, &n,
         "{\"schema\":1,\"source\":\"%s\",\"boot\":\"%016llx\",\"channel\":\"GM7_DIRECT_BLE_"
         "FOLLOWER\",\"page\":%u,\"found\":%s,\"sequence\":%u,\"incident\":%u,\"event\":\"%s\","
         "\"primary\":\"%s\",\"producer_sample_kernel_us\":%llu,\"detected_kernel_us\":%llu,\"BLE_"
         "readable_at_producer\":%s,\"root\":\"%016llx\",\"root_boot\":\"%016llx\",\"source_"
         "epoch\":%u,\"train\":%u,\"capture_gen\":%u,\"physical_until_hf_tick\":%llu,\"data_until_"
         "hf_tick\":%llu,\"last_accepted_hf_tick\":%llu,\"PT_scope_until_kernel_us\":%llu,"
         "\"effective_until_kernel_us\":%llu,\"grant_root\":\"%016llx\",\"grant_boot\":\"%016llx\","
         "\"grant_source_epoch\":%u,\"grant_train\":%u,\"grant_capture_gen\":%u,\"grant_until_"
         "kernel_us\":%llu,\"full_scope_equal\":%s,\"actual_binding_ok\":%s,\"header_root\":\"%"
         "016llx\",\"header_boot\":\"%016llx\",\"header_source_epoch\":%u,\"header_train\":%u,"
         "\"header_capture_gen\":%u,\"header_publication\":%u,\"header_matches_PT\":%s",
         R2_SOURCE_SHA, (unsigned long long)boot, page, r.sequence ? "true" : "false", r.sequence,
         r.incident, kind_name(R2_BLE_KIND(r)), primary_name(R2_BLE_PRIMARY(r)),
         (unsigned long long)r.sample_us, (unsigned long long)r.commit_us,
         r.flags & R2_BLE_AVAILABLE ? "true" : "false", (unsigned long long)r.root,
         (unsigned long long)r.boot, r.source, r.train, r.capture,
         (unsigned long long)r.physical_until, (unsigned long long)r.data_until,
         (unsigned long long)r.last_accepted, (unsigned long long)r.scope_until,
         (unsigned long long)r.effective_until, (unsigned long long)r.grant_root,
         (unsigned long long)r.grant_boot, r.grant_source, r.grant_train, r.grant_capture,
         (unsigned long long)r.grant_until, r.flags & R2_BLE_SCOPE_EQUAL ? "true" : "false",
         r.flags & R2_BLE_BOUND ? "true" : "false", (unsigned long long)r.header_root,
         (unsigned long long)r.header_boot, r.header_source, r.header_train, r.header_capture,
         r.header_publication, r.flags & R2_BLE_HEADER_MATCHES ? "true" : "false");
  bool hf = r.flags & R2_BLE_CAPTURED, header = r.flags & R2_BLE_HEADER_KNOWN,
       matched = r.flags & R2_BLE_MATCHED_KNOWN;
  bool sample_known = r.flags & R2_BLE_SAMPLE_KNOWN;
  u64(out, cap, &n, "sample_hf_tick", r.tick, sample_known);
  u64(out, cap, &n, "counter_epoch", r.epoch, sample_known);
  u64(out, cap, &n, "publication_counter_epoch", r.publication_epoch, r.publication_epoch != 0);
  u64(out, cap, &n, "header_hf_tick", r.header_tick, header);
  u64(out, cap, &n, "header_counter_epoch", r.header_epoch, header);
  u64(out, cap, &n, "header_created_source_ns", r.header_created, header);
  u64(out, cap, &n, "header_expires_source_ns", r.header_expires, header);
  i64(out, cap, &n, "header_UTC_offset_ns", r.header_offset, header);
  u64(out, cap, &n, "matched_created_source_ns", r.matched_created, matched);
  u64(out, cap, &n, "matched_expires_source_ns", r.matched_expires, matched);
  i64(out, cap, &n, "delivered_model_source_ns_at_sample", r.model_source_ns,
      r.flags & R2_BLE_MODEL_KNOWN);
  i64(out, cap, &n, "physical_remaining_us",
      r.physical_until >= r.tick ? (int64_t)((r.physical_until - r.tick) / 16)
                                 : -(int64_t)((r.tick - r.physical_until) / 16),
      hf && r.physical_until);
  i64(out, cap, &n, "data_remaining_us",
      r.data_until >= r.tick ? (int64_t)((r.data_until - r.tick) / 16)
                             : -(int64_t)((r.tick - r.data_until) / 16),
      hf && r.data_until);
  u64(out, cap, &n, "accepted_age_us", (r.tick - r.last_accepted) / 16,
      hf && r.last_accepted && r.tick >= r.last_accepted);
  u64(out, cap, &n, "header_age_us", (r.tick - r.header_tick) / 16,
      sample_known && header && r.epoch == r.header_epoch && r.tick >= r.header_tick);
  append(out, cap, &n, ",\"rms_ns\":%u,\"quality_bins\":%u,\"caller_pc\":%u", r.rms_ns,
         r.quality_bins, r.caller_pc);
  if (r.flags & R2_BLE_REASON_KNOWN)
    append(out, cap, &n, ",\"control_reason\":%u,\"control_generation\":%u", r.reason,
           r.generation);
  else
    append(out, cap, &n, ",\"control_reason\":null,\"control_generation\":null");
  append(out, cap, &n, ",\"cause_bits\":%u,\"causes\":[", r.why);
  bool comma = false;
  for (unsigned i = 0; i < sizeof reasons / sizeof reasons[0]; i++)
    if (r.why & reasons[i].bit) {
      append(out, cap, &n, "%s\"%s\"", comma ? "," : "", reasons[i].name);
      comma = true;
    }
  append(out, cap, &n, "],\"authority\":false}");
  return n < 0 ? -ENOSPC : n;
}
int r2_ble_causal_json(char *out, size_t cap, uint64_t boot, unsigned page) {
  if (page == 10)
    return summary_json(out, cap, boot);
  if (page >= 11 && page <= 13)
    return record_json(out, cap, boot, page);
  return -EINVAL;
}
void r2_ble_causal_poll(uint64_t boot, uint64_t now) {
  struct r2_ble_causal_record r = {0};
  bool journal = false;
  unsigned selected = 3;
  k_spinlock_key_t k = k_spin_lock(&causal_lock);
  struct r2_ble_causal_meta *m = &causal.meta;
  if (!(m->flags & DRAIN_INIT)) {
    m->flags |= DRAIN_INIT | (2u << T_SHIFT);
    m->drain_due_ms = now <= UINT64_MAX - 5000 ? now + 5000 : UINT64_MAX;
  }
  unsigned tokens = (m->flags & T_MASK) >> T_SHIFT;
  if (m->drain_due_ms != UINT64_MAX && now >= m->drain_due_ms) {
    uint64_t refill = (now - m->drain_due_ms) / 5000 + 1;
    tokens = refill >= 2 ? 2 : tokens + (unsigned)refill;
    if (tokens > 2)
      tokens = 2;
    m->drain_due_ms = now <= UINT64_MAX - 5000 ? now + 5000 : UINT64_MAX;
  }
  if (!tokens) {
    k_spin_unlock(&causal_lock, k);
    return;
  }
  for (unsigned phase = 0; phase < 2 && selected == 3; phase++)
    for (unsigned i = 0; i < 3; i++)
      if ((m->flags & (1u << ((phase ? N_SHIFT : J_SHIFT) + i))) && slot(i)->sequence &&
          (!r.sequence || slot(i)->sequence < r.sequence)) {
        r = *slot(i);
        selected = i;
        journal = phase == 0;
      }
  if (selected == 3) {
    m->flags = (m->flags & ~T_MASK) | (tokens << T_SHIFT);
    k_spin_unlock(&causal_lock, k);
    return;
  }
  m->flags = (m->flags & ~T_MASK) | ((tokens - 1) << T_SHIFT);
  if (journal) {
    m->flags &= ~(1u << (J_SHIFT + selected));
    inc(&m->journal_attempts);
  }
  k_spin_unlock(&causal_lock, k);
  if (journal)
    r2_timing_event(R2_TIMING_BLE_CAUSAL, (int)R2_BLE_PRIMARY(r), r.root, R2_BLE_KIND(r),
                    r.sequence, r.why);
  char line[512];
  int n = snprintf(
      line, sizeof line,
      "#R2_BLE_CAUSE "
      "{\"schema\":1,\"source\":\"%s\",\"boot\":\"%016llx\",\"sequence\":%u,\"incident\":%u,"
      "\"event\":\"%s\",\"primary\":\"%s\",\"detected_us\":%llu,\"emitted_ms\":%llu,\"root\":\"%"
      "016llx\",\"root_boot\":\"%016llx\",\"source_epoch\":%u,\"train\":%u,\"why\":%u}\n",
      R2_SOURCE_SHA, (unsigned long long)boot, r.sequence, r.incident, kind_name(R2_BLE_KIND(r)),
      primary_name(R2_BLE_PRIMARY(r)), (unsigned long long)r.commit_us, (unsigned long long)now,
      (unsigned long long)r.root, (unsigned long long)r.boot, r.source, r.train, r.why);
  if (n > 0 && (size_t)n < sizeof line &&
      !r2_console_note(line, (size_t)n, now <= UINT64_MAX - 150 ? now + 150 : UINT64_MAX)) {
    k = k_spin_lock(&causal_lock);
    if (slot(selected)->sequence == r.sequence)
      causal.meta.flags &= ~(1u << (N_SHIFT + selected));
    k_spin_unlock(&causal_lock, k);
  }
}
