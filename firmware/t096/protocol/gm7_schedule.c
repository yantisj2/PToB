/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_schedule.h"
#include <errno.h>
#include <string.h>
#include <tinycrypt/sha256.h>
static void inc(uint32_t *v) {
  if (*v < UINT32_MAX)
    ++*v;
}
static bool add(uint64_t a, uint64_t b, uint64_t *out) {
  if (a > UINT64_MAX - b)
    return false;
  *out = a + b;
  return true;
}
static bool time_now(struct gm7_scheduler *s, uint64_t now) {
  if (s->failed || now < s->last_now) {
    s->failed = true;
    for (unsigned i = 0; i < GM7_JOB_SLOTS; i++)
      if (s->job[i].state != GM7_JOB_EMPTY)
        s->job[i].state = GM7_JOB_ABORTED;
    return false;
  }
  s->last_now = now;
  return true;
}
static bool status_digest(uint8_t digest[32], uint64_t id, uint64_t boot,
                          uint64_t cycle) {
  uint8_t raw[24];
  const uint64_t values[3] = {id, boot, cycle};
  for (unsigned j = 0; j < 3; j++)
    for (unsigned i = 0; i < 8; i++)
      raw[j * 8 + i] = (uint8_t)(values[j] >> (56 - 8 * i));
  struct tc_sha256_state_struct h;
  static const uint8_t domain[] = "GM7v2-slot";
  if (!id || !boot || !tc_sha256_init(&h) ||
      !tc_sha256_update(&h, domain, sizeof domain) ||
      !tc_sha256_update(&h, raw, sizeof raw) || !tc_sha256_final(digest, &h))
    return false;
  return true;
}
int gm7_status_jitter(uint64_t id, uint64_t boot, uint64_t cycle) {
  uint8_t digest[32];
  if (!status_digest(digest, id, boot, cycle))
    return 251;
  return (int)(((unsigned)digest[0] * 256 + digest[1]) % 501) - 250;
}
unsigned gm7_status_slot(uint64_t id, uint64_t boot, uint64_t cycle) {
  uint8_t digest[32];
  if (!status_digest(digest, id, boot, cycle))
    return 8;
  /* Modulo8 of the first BE u64 is its low three bits. */
  return digest[7] & 7u;
}
int gm7_status_offset(uint32_t *out, uint64_t id, uint64_t boot, uint64_t cycle,
                      int jitter) {
  if (!out || jitter < -250 || jitter > 250)
    return -EINVAL;
  unsigned slot = gm7_status_slot(id, boot, cycle);
  if (slot >= 8)
    return -EINVAL;
  *out = (uint32_t)(90000 + (int)slot * 2500 + jitter);
  return 0;
}
uint32_t gm7_discovery_offset(uint64_t id, uint64_t boot, uint64_t frame) {
  uint8_t digest[32];
  if (!status_digest(digest, id, boot, frame))
    return 0;
  return 45000u + (digest[8] % 3u) * 5000u;
}
int gm7_response_window(struct gm7_window *w, uint64_t request_rx,
                        uint64_t frame_start, uint64_t now,
                        uint32_t enclosure) {
  if (!w || enclosure > 2000 || request_rx > now ||
      now - request_rx >= GM7_FRAME_US || now > UINT64_MAX - 3000 ||
      frame_start > now || now - frame_start >= GM7_FRAME_US)
    return -EINVAL;
  /* One secondary owner. A received request has priority over a discovery
   * copy/diagnostic not yet queued. Select using this receiver's actual phase;
   * no assumption about the requester's UTC or frame is authority. */
  const uint32_t offsets[] = {43000, 48000, 53000};
  for (unsigned next = 0; next < 2; next++) {
    for (unsigned i = 0; i < 3; i++) {
      uint64_t nominal, end;
      if (!add(frame_start, next * GM7_FRAME_US + offsets[i], &nominal) ||
          !add(nominal, 4000, &end))
        return -EOVERFLOW;
      if (nominal < now + 3000)
        continue;
      /* Leave2ms for complete REQUEST airtime before its captured AUX END.
       * Requester separately enforces its original actual-TX250ms deadline. */
      if (end < request_rx || end - request_rx > GM7_FRAME_US - 2000)
        return -ETIME;
      *w = (struct gm7_window){.nominal = nominal,
          .latest_start = nominal + 2000, .latest_end = end,
          .enclosure_us = enclosure, .channel = 37, .physical = true};
      return 0;
    }
  }
  return -ETIME;
}
int gm7_complete_airtime(uint32_t *out, size_t ad, unsigned primary,
                         unsigned auxiliary, unsigned channels, unsigned phy) {
  if (!out || !ad || ad > 191 || primary > 37 || auxiliary > 10 ||
      ad + auxiliary > 255 || channels != 1 || phy != 2)
    return -EINVAL;
  /* LE1M primary:1preamble+4AA+2header+payload+3CRC. LE2M AUX:
   *2preamble+4AA+2header+extended-header+AD+3CRC;300us minimum
   * complete inter-PDU gap. The actual bounded AUX placement is enforced too.
   * */
  uint32_t total =
      (10 + primary) * 8 + 300 + (uint32_t)(11 + auxiliary + ad) * 4;
  if (total > GM7_JOB_AIRTIME_US)
    return -ERANGE;
  *out = total;
  return 0;
}
void gm7_scheduler_init(struct gm7_scheduler *s, uint64_t now) {
  if (s)
    *s = (struct gm7_scheduler){.last_now = now, .generation = 1};
}
int gm7_scheduler_submit(struct gm7_scheduler *s, unsigned slot,
                         const struct gm7_packet *p,
                         const struct r2_group_config *g, struct gm7_window w,
                         uint64_t source_until, uint64_t request_deadline,
                         uint8_t handle, uint64_t now, uint64_t *ticket) {
  if (!s || !p || !ticket || slot >= GM7_JOB_SLOTS || !time_now(s, now) ||
      gm7_validate(p))
    return -EINVAL;
  struct gm7_job *j = &s->job[slot];
  if (j->state != GM7_JOB_EMPTY)
    return -EBUSY;
  if ((slot == GM7_PRIMARY && p->kind != GM7_ANNOUNCE &&
       p->kind != GM7_WITHDRAW) ||
      (slot == GM7_SECONDARY && p->kind != GM7_ANCHOR && p->kind != GM7_ROLE &&
       p->kind != GM7_GNSS && p->kind != GM7_ANNOUNCE) ||
      (slot == GM7_STANDBY && p->kind != GM7_STATUS && p->kind != GM7_REQUEST))
    return -EPROTO;
  /* The discovery copy carries the exact ordinary primary body, not a newly
   * sampled announcement and never an extra YIELD/event copy. Its primary
   * buffer remains owned through this secondary reservation. */
  if (slot == GM7_SECONDARY && p->kind == GM7_ANNOUNCE) {
    const struct gm7_job *primary = &s->job[GM7_PRIMARY];
    uint8_t encoded[GM7_MAX_AD_BYTES];
    if ((p->flags & GM7_YIELD) || primary->kind != GM7_ANNOUNCE ||
        (primary->state == GM7_JOB_EMPTY ||
         primary->state == GM7_JOB_ABORTED) ||
        primary->generation != s->generation ||
        w.nominal < primary->window.nominal ||
        (w.nominal - primary->window.nominal != 7000 &&
         w.nominal - primary->window.nominal != 12000 &&
         w.nominal - primary->window.nominal != 17000) ||
        gm7_ad_encode(encoded, sizeof encoded, p, g) ||
        primary->length != gm7_wire_size(p->kind) + 4 ||
        memcmp(encoded, primary->ad, primary->length))
      return -EPROTO;
  }

  if (!w.physical || w.enclosure_us > 2000 || w.channel != 37 ||
      w.latest_start < w.nominal || w.latest_start - w.nominal > 2000 ||
      w.latest_end < w.latest_start || w.latest_end - w.nominal > 4000 ||
      now > w.latest_start)
    return -ETIME;
  bool positive = p->kind == GM7_ANNOUNCE || p->kind == GM7_ANCHOR ||
                  (p->kind == GM7_STATUS &&
                   (p->flags & (GM7_ELIGIBLE | GM7_SHADOW | GM7_HOT)));
  if (positive && (!source_until || source_until <= w.latest_end))
    return -ESTALE;
  if (request_deadline && w.latest_end > request_deadline)
    return -ETIME;
  if (s->next_ticket == UINT64_MAX || s->next_buffer == UINT32_MAX) {
    s->failed = true;
    return -EOVERFLOW;
  }
  *j = (struct gm7_job){
      .window = w,
      .ticket = ++s->next_ticket,
      .sequence = p->kind <= GM7_WITHDRAW  ? p->body.control.sequence
                  : p->kind == GM7_REQUEST ? p->body.request.request_sequence
                  : p->kind == GM7_ANCHOR  ? p->body.anchor.response_sequence
                  : p->kind == GM7_ROLE    ? p->body.role.sequence
                                           : p->body.gnss.sequence,
      .source_until = source_until,
      .request_deadline = request_deadline,
      .generation = s->generation,
      .buffer_version = ++s->next_buffer,
      .length = (uint16_t)(gm7_wire_size(p->kind) + 4),
      .kind = p->kind,
      .opportunity = (uint8_t)slot,
      .handle = handle,
      .state = GM7_JOB_QUEUED,
      .positive = positive,
      .negative = p->kind == GM7_WITHDRAW};
  int rc = gm7_ad_encode(j->ad, sizeof j->ad, p, g);
  if (rc) {
    memset(j, 0, sizeof *j);
    return rc;
  }
  unsigned pending = 0;
  for (unsigned i = 0; i < GM7_JOB_SLOTS; i++)
    pending += s->job[i].state != GM7_JOB_EMPTY;
  if (pending > s->queue_peak)
    s->queue_peak = pending;
  *ticket = j->ticket;
  return 0;
}
int gm7_scheduler_dispatch(struct gm7_scheduler *s, unsigned slot,
                           bool source_live, uint64_t now) {
  if (!s || slot >= GM7_JOB_SLOTS || !time_now(s, now))
    return -EINVAL;
  struct gm7_job *j = &s->job[slot];
  if (j->state != GM7_JOB_QUEUED ||
      (j->positive && j->generation != s->generation))
    return -ESTALE;
  if (now < j->window.nominal)
    return -EAGAIN;
  if (now > j->window.latest_start ||
      (j->positive && (!source_live || now >= j->source_until)) ||
      (j->request_deadline && now >= j->request_deadline)) {
    j->state = GM7_JOB_ABORTED;
    inc(&s->late);
    return -ETIME;
  }
  j->state = GM7_JOB_DISPATCHED;
  return 0;
}
static bool matches(struct gm7_scheduler *s, struct gm7_job *j, uint64_t ticket,
                    uint32_t generation, uint32_t version) {
  if (!s->failed && j->ticket == ticket && j->generation == generation &&
      (!j->positive || generation == s->generation) &&
      j->buffer_version == version)
    return true;
  inc(&s->stale_callbacks);
  return false;
}
bool gm7_scheduler_aux_allowed(struct gm7_scheduler *s, unsigned slot,
                               uint64_t ticket, uint32_t generation,
                               uint32_t version, uint64_t now,
                               uint32_t remaining, bool source_live) {
  if (!s || slot >= GM7_JOB_SLOTS || s->failed || remaining > 2000)
    return false;
  struct gm7_job *j = &s->job[slot];
  uint64_t end;
  if (!matches(s, j, ticket, generation, version) ||
      j->state != GM7_JOB_PRIMARY || !add(now, remaining, &end) ||
      end > j->window.latest_end ||
      (j->positive && (!source_live || end >= j->source_until)) ||
      (j->request_deadline && end > j->request_deadline)) {
    if (j->state != GM7_JOB_EMPTY)
      j->state = GM7_JOB_ABORTED;
    return false;
  }
  j->aux_seen = true;
  return true;
}
int gm7_scheduler_started(struct gm7_scheduler *s, unsigned slot,
                          uint64_t ticket, uint32_t generation,
                          uint32_t version, uint64_t start) {
  if (!s || slot >= GM7_JOB_SLOTS)
    return -EINVAL;
  struct gm7_job *j = &s->job[slot];
  if (!matches(s, j, ticket, generation, version) ||
      j->state != GM7_JOB_DISPATCHED)
    return -ESTALE;
  /* Captured original start can precede callback processing. It is checked
   * against its reserved window, never used as the global owner clock. */
  if (start < j->window.nominal || start > j->window.latest_start) {
    j->state = GM7_JOB_ABORTED;
    inc(&s->missed);
    return -ETIME;
  }
  j->first_start = start;
  j->state = GM7_JOB_PRIMARY;
  return 0;
}
int gm7_scheduler_complete(struct gm7_scheduler *s, unsigned slot,
                           uint64_t ticket, uint32_t generation,
                           uint32_t version, uint64_t end, bool complete) {
  if (!s || slot >= GM7_JOB_SLOTS)
    return -EINVAL;
  struct gm7_job *j = &s->job[slot];
  if (!matches(s, j, ticket, generation, version) ||
      j->state != GM7_JOB_PRIMARY)
    return -ESTALE;
  j->final_end = end;
  if (!complete || !j->aux_seen || end < j->first_start ||
      end - j->first_start > 2000 || end > j->window.latest_end ||
      (j->positive && end >= j->source_until) ||
      (j->request_deadline && end > j->request_deadline)) {
    j->state = GM7_JOB_ABORTED;
    inc(&s->missed);
    return -ETIME;
  }
  j->state = GM7_JOB_COMPLETE;
  inc(&s->completed);
  return 0;
}
void gm7_scheduler_cancel_positive(struct gm7_scheduler *s, uint64_t now) {
  if (!s || !time_now(s, now))
    return;
  /* Keep valid negative jobs, while invalidating every queued/inflight positive
   * ticket. Generation is per cancellation, so late AUX cannot regain a job. */
  if (s->generation == UINT32_MAX) {
    s->failed = true;
    return;
  }
  ++s->generation;
  for (unsigned i = 0; i < GM7_JOB_SLOTS; i++) {
    struct gm7_job *j = &s->job[i];
    if (j->positive && j->state != GM7_JOB_EMPTY) {
      j->state = GM7_JOB_ABORTED;
      inc(&s->aborted);
    }
  }
}
void gm7_scheduler_abort(struct gm7_scheduler *s, unsigned slot,
                         uint64_t ticket, uint64_t now) {
  if (!s || slot >= GM7_JOB_SLOTS || !time_now(s, now))
    return;
  struct gm7_job *j = &s->job[slot];
  if (j->ticket == ticket && j->state != GM7_JOB_EMPTY &&
      j->state != GM7_JOB_ABORTED) {
    j->state = GM7_JOB_ABORTED;
    inc(&s->aborted);
  }
}
int gm7_scheduler_release(struct gm7_scheduler *s, unsigned slot,
                          uint64_t ticket, bool drained, uint64_t now) {
  if (!s || slot >= GM7_JOB_SLOTS || !time_now(s, now))
    return -EINVAL;
  struct gm7_job *j = &s->job[slot];
  if (j->ticket != ticket)
    return -ESTALE;
  if (!drained || (j->state != GM7_JOB_COMPLETE && j->state != GM7_JOB_ABORTED))
    return -EBUSY;
  memset(j, 0, sizeof *j);
  return 0;
}
