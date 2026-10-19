/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "link_quality.h"
/* W12 parent-history method, bounded for T096 SRAM. Advisory only: no cached
 * locator, binding, map, source permission or authority is ever restored. */
#define RC_STREAM_CAPACITY 8u
#define RC_STREAM_RETAIN_S 1800u
#define RC_STREAM_ADMISSION_US UINT64_C(60000000)
/* A relay may first return PENDING while its demanded train starts. Retained
 * two-starts/minute control limits can delay working admission. Keep180s for
 * admission, then300s acquisition; first PA establishment starts that stage.
 * Repeated PENDING/renewals never move either original deadline. */
#define RC_STREAM_RELAY_ADMISSION_US UINT64_C(180000000)
#define RC_STREAM_ACQUIRE_US UINT64_C(300000000)
#define RC_STREAM_GM_ACQUIRE_US UINT64_C(120000000)
#define RC_STREAM_TRIAL_US UINT64_C(600000000)
#define RC_STREAM_GM_TRIAL_US UINT64_C(180000000)
/* Retain a settled GM through one finite recovery minute. Expired timing
 * still withdraws immediately; a failed renewal is not a six-second detour. */
#define RC_STREAM_LOSS_US UINT64_C(60000000)
/* A settled relay also needs time for fresh admission after a train change.
 * Use a five-minute degraded episode; a brief issue does not reject a relay. */
#define RC_STREAM_RELAY_LOSS_US UINT64_C(300000000)
/* A previously usable parent with no PA synchronization is an RF outage,
 * not cold acquisition. Give the old scope one fixed, short recovery grace. */
#define RC_STREAM_OUTAGE_US UINT64_C(10000000)
#define RC_STREAM_WINDOW 8u
struct rc_stream_row {
  uint64_t id;
  uint32_t observed, failed_at, retry_at;
  uint16_t delivery, usable, delivery_samples, usable_samples;
  uint8_t failures, relay;
  uint16_t reserved;
};
struct rc_stream_history {
  struct rc_stream_row row[RC_STREAM_CAPACITY];
  uint64_t at;
  bool clock_seen;
};
struct rc_stream_sample {
  uint64_t owned, usable;
  uint8_t window[RC_STREAM_WINDOW], count, cursor;
  uint16_t total;
  bool previous, known;
  bool unstable;
};
_Static_assert(sizeof(struct rc_stream_row) == 32, "Fixed stream history row");
_Static_assert(sizeof(struct rc_stream_history) +
                       sizeof(struct rc_stream_sample) <=
                   304,
               "Stream SRAM budget");
static inline void rc_stream_advance(struct rc_stream_history *h,
                                     uint64_t now) {
  if (h->clock_seen && (now < h->at || now - h->at >= UINT64_C(1800000000)))
    memset(h, 0, sizeof *h);
  if (!h->clock_seen || now / 1000000 != h->at / 1000000)
    for (unsigned i = 0; i < RC_STREAM_CAPACITY; i++)
      if (h->row[i].id &&
          (uint32_t)(now / 1000000 - h->row[i].observed) >= RC_STREAM_RETAIN_S)
        memset(&h->row[i], 0, sizeof h->row[i]);
  h->clock_seen = true;
  h->at = now;
}
static inline struct rc_stream_row *rc_stream_find(struct rc_stream_history *h,
                                                   uint64_t id, bool relay,
                                                   uint64_t now) {
  rc_stream_advance(h, now);
  if (!id)
    return NULL;
  for (unsigned i = 0; i < RC_STREAM_CAPACITY; i++)
    if (h->row[i].id == id && h->row[i].relay == relay)
      return &h->row[i];
  return NULL;
}
static inline struct rc_stream_row *
rc_stream_obtain(struct rc_stream_history *h, uint64_t id, bool relay,
                 uint64_t now) {
  struct rc_stream_row *r = rc_stream_find(h, id, relay, now);
  if (r)
    return r;
  unsigned at = 0;
  uint32_t s = (uint32_t)(now / 1000000);
  for (unsigned i = 0; i < RC_STREAM_CAPACITY; i++) {
    if (!h->row[i].id) {
      at = i;
      break;
    }
    if ((uint32_t)(s - h->row[i].observed) >
        (uint32_t)(s - h->row[at].observed))
      at = i;
  }
  h->row[at] = (struct rc_stream_row){.id = id, .relay = relay, .observed = s};
  return &h->row[at];
}
static inline void rc_stream_ewma(uint16_t *value, uint16_t *n,
                                  unsigned sample) {
  if (sample > 1000)
    sample = 1000;
  *value = *n ? (uint16_t)((3u * *value + sample) / 4u) : (uint16_t)sample;
  if (*n < UINT16_MAX)
    ++*n;
}
static inline bool rc_stream_blocked(struct rc_stream_history *h, uint64_t id,
                                     bool relay, uint64_t now) {
  struct rc_stream_row *r = rc_stream_find(h, id, relay, now);
  return r && r->failures &&
         (uint32_t)(now / 1000000 - r->failed_at) <
             (uint32_t)(r->retry_at - r->failed_at);
}
static inline void rc_stream_failed(struct rc_stream_history *h, uint64_t id,
                                    bool relay, uint64_t now) {
  if (!id)
    return;
  struct rc_stream_row *r = rc_stream_obtain(h, id, relay, now);
  if (r->failures < 3)
    ++r->failures;
  r->failed_at = (uint32_t)(now / 1000000);
  r->retry_at = r->failed_at + ((relay ? 30u : 300u) << (r->failures - 1));
  /* Acquisition failure is a separate observation, not a fabricated RF sample.
   */
}
static inline void rc_stream_admission_failed(struct rc_stream_history *h,
                                              uint64_t id, bool relay,
                                              uint64_t now) {
  if (!id)
    return;
  struct rc_stream_row *r = rc_stream_obtain(h, id, relay, now);
  if (r->failures < 3)
    ++r->failures;
  r->failed_at = (uint32_t)(now / 1000000);
  r->retry_at = r->failed_at + ((relay ? 30u : 60u) << (r->failures - 1));
}
/* A GM that already supplied sustained usable time can become available again
 * after a source/stream interruption. Use the shorter bounded retry schedule;
 * retain its failure penalty and require fresh admission and observed recovery. */
static inline void rc_stream_gm_recovery_failed(struct rc_stream_history *h,
                                               uint64_t id,uint64_t now) {
  rc_stream_admission_failed(h,id,false,now);
}
/* Aging affects advisory confidence only. Unknown is a prior, never a
 * fabricated observation. Five-minute linear half-life; counts saturate only
 * for storage and are capped at32s when computing effective confidence. */
static inline unsigned rc_stream_confidence(const struct rc_stream_row *r,
                                             unsigned samples, uint64_t now) {
  if (!r || !samples) return 0;
  unsigned age=(uint32_t)(now/1000000-r->observed);
  unsigned weight=(samples<32?samples:32)*1000/32;
  unsigned halves=age/300;
  if (halves>=6) return 0;
  weight >>= halves;
  return weight*(600-age%300)/600;
}
static inline int rc_stream_score(struct rc_stream_history *h, uint64_t id,
                                  bool relay, int rssi, uint64_t now) {
  struct rc_stream_row *r = rc_stream_find(h, id, relay, now);
  if (rssi < -100) rssi = -100;
  if (rssi > -20) rssi = -20;
  int dw=r?rc_stream_confidence(r,r->delivery_samples,now):0;
  int uw=r?rc_stream_confidence(r,r->usable_samples,now):0;
  int delivery=r?((int)r->delivery-750)*dw/1000:0;
  int usable=r?((int)r->usable-750)*uw/1000:0;
  unsigned age=r?(uint32_t)(now/1000000-r->failed_at):0;
  int penalty=r?(int)r->failures*64:0;
  penalty >>= age/300<6?age/300:6;
  return (rssi+100)/2+delivery/8+usable/2+(relay?0:8)-penalty;
}
/* A changed authenticated path keeps a small device prior, not a proven route.
 * It is called once per actual scope change, never by locator reception. */
static inline void rc_stream_scope_changed(struct rc_stream_history *h,
                                            uint64_t id,bool relay,uint64_t now) {
  struct rc_stream_row *r=rc_stream_find(h,id,relay,now);
  if(r){r->delivery_samples/=4;r->usable_samples=0;}
}
/* Advisory selection evidence only. Beacon reception, coarse admission and a
 * single lock sample cannot establish that a GM has supplied usable time. */
static inline bool rc_stream_proven(struct rc_stream_history *h, uint64_t id,
                                     bool relay, uint64_t now) {
  struct rc_stream_row *r = rc_stream_find(h, id, relay, now);
  return r && !r->failures && r->usable_samples >= RC_STREAM_WINDOW &&
         r->usable >= 900 &&
         (uint32_t)(now/1000000-r->observed)<300;
}
/* Eight complete seconds of observed usable time. A lock flicker cannot reset
 * this window. Local pauses provide no loss evidence and restart the window. */
static inline void rc_stream_window(struct rc_stream_sample *s, unsigned good) {
  unsigned value = good / 10;
  if (s->count == RC_STREAM_WINDOW)
    s->total -= s->window[s->cursor];
  else
    ++s->count;
  s->window[s->cursor] = (uint8_t)value;
  s->total += value;
  s->cursor = (s->cursor + 1) % RC_STREAM_WINDOW;
  if (s->count == RC_STREAM_WINDOW) {
    if (s->total < RC_STREAM_WINDOW * 75u)
      s->unstable = true;
    else if (s->total >= RC_STREAM_WINDOW * 90u)
      s->unstable = false;
  }
}
static inline void rc_stream_observe(struct rc_stream_history *h,
                                     struct rc_stream_sample *s,
                                     struct r2_link_quality *q, uint64_t id,
                                     bool relay, bool usable, bool receiving,
                                     uint64_t now) {
  rc_stream_advance(h, now);
  uint64_t elapsed = q->clock_seen && now >= q->at ? now - q->at : 0;
  bool eligible = q->clock_seen && q->listening && receiving && now >= q->at &&
                  elapsed <= 250000;
  if (!receiving || !q->listening || now < q->at ||
      (q->clock_seen && elapsed > 250000)) {
    *s = (struct rc_stream_sample){0};
    q->listened = q->in_sample = 0;
  }
  if (eligible && s->known) {
    s->owned += elapsed;
    if (s->previous && usable)
      s->usable += elapsed;
  }
  uint64_t listened = q->listened + (eligible ? elapsed : 0);
  unsigned received = q->in_sample;
  bool completed = eligible && listened >= R2_LINK_SAMPLE_US;
  r2_link_quality_tick(q, receiving, now);
  if (completed) {
    if (id && listened) {
      struct rc_stream_row *r = rc_stream_obtain(h, id, relay, now);
      r->observed = (uint32_t)(now / 1000000);
      rc_stream_ewma(&r->delivery, &r->delivery_samples,
                     (unsigned)((uint64_t)received * 1000 /
                                (listened / R2_LINK_INTERVAL_US)));
      if (s->owned == listened) {
        unsigned good = (unsigned)(s->usable * 1000 / listened);
        rc_stream_window(s, good);
        rc_stream_ewma(&r->usable, &r->usable_samples, good);
        /* Audible but unusable packets cannot erase a failed-stream penalty. */
        if (good >= 900 && r->usable >= 900 &&
            s->count == RC_STREAM_WINDOW &&
            s->total >= RC_STREAM_WINDOW * 90u) {
          r->failures = 0;
          r->retry_at = 0;
        }
      }
    }
    s->owned = s->usable = 0;
  }
  s->previous = usable;
  s->known = true;
}
