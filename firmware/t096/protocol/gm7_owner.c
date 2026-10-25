/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_owner.h"
#include <errno.h>
#include <string.h>
static void count(uint32_t *v) {
  if (*v < UINT32_MAX)
    ++*v;
}
static bool add(uint64_t a, uint64_t b, uint64_t *v) {
  if (a > UINT64_MAX - b)
    return false;
  *v = a + b;
  return true;
}
static struct gm7_quality quality(const struct gm7_control *c) {
  return (struct gm7_quality){c->priority1, c->clock_class, c->accuracy,
                              c->priority2, c->variance};
}
int gm7_quality_compare(struct gm7_quality a, struct gm7_quality b) {
#define Q(f)                                                                   \
  do {                                                                         \
    if (a.f != b.f)                                                            \
      return a.f < b.f ? -1 : 1;                                               \
  } while (0)
  Q(priority1);
  Q(clock_class);
  Q(accuracy);
  Q(variance);
  Q(priority2);
#undef Q
  return 0;
}
static struct gm7_scope scope_of(const struct gm7_control *c, uint32_t token) {
  return (struct gm7_scope){c->birth_us,   c->founder,     c->term,
                            c->active,     c->active_boot, token,
                            c->source_gen, c->capture_gen, c->train_gen};
}
bool gm7_scope_same(struct gm7_scope a, struct gm7_scope b) {
  return a.birth == b.birth && a.founder == b.founder && a.term == b.term &&
         a.active == b.active && a.boot == b.boot &&
         a.timing_epoch == b.timing_epoch && a.source_gen == b.source_gen &&
         a.capture_gen == b.capture_gen && a.train == b.train;
}
/* Gregorian calendar using bounded2000..2099 day subtraction. No libc timezone,
 * UTC smear, leap-second60 label or guessed predictive leap metadata. */
bool gm7_calendar_clear(uint64_t utc) {
  if (!gm7_utc_valid(utc))
    return false;
  uint64_t seconds = utc / 1000000, day = seconds / 86400;
  unsigned within = (unsigned)(seconds % 86400), year = 1970, month = 0;
  for (;; year++) {
    unsigned days =
        (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
    if (day < days)
      break;
    day -= days;
  }
  const unsigned lengths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  for (; month < 12; month++) {
    unsigned days = lengths[month] + (month == 1 && year % 4 == 0 &&
                                      (year % 100 != 0 || year % 400 == 0));
    if (day < days) {
      if ((day == 0 && within < 60) || (day + 1 == days && within >= 86340))
        return false;
      return true;
    }
    day -= days;
  }
  return false;
}
static void disarm(struct gm7_owner *o, unsigned reason) {
  if (o->reserved)
    gm7_owner_reservation_cancel(o, o->now);
  o->takeover_due = false;
  if (o->armed) {
    o->armed = false;
    o->armed_since = 0;
    count(&o->transitions);
    o->reason = reason;
  }
  if (!o->serving)
    o->role = o->gm_capable ? GM7_COLD : GM7_RECEIVER;
}
static void cancel_positive(struct gm7_owner *o) {
  o->reserved = false;
  if (o->event.valid && !o->event.negative)
    o->event.valid = false;
  if (o->event_generation == UINT32_MAX) {
    o->failed = true;
    o->reason = GM7_R_EXHAUSTED;
  } else
    ++o->event_generation;
  o->yield_deadline = 0;
  o->yield_ready = false;
}
static bool clock_now(struct gm7_owner *o, uint64_t now) {
  if (!o || o->failed || now < o->now || now > INT64_MAX) {
    if (o) {
      o->failed = true;
      o->serving = o->eligible = o->armed = o->prepared = o->takeover_due =
          false;
      o->armed_since = o->active_since = 0;
      o->event.valid = o->reserved = false;
      o->scope = (struct gm7_scope){0};
      o->selected = (struct gm7_control){0};
      o->selected_until = o->selected_source_until = 0;
      memset(o->peer, 0, sizeof o->peer);
      memset(o->requester.pending, 0, sizeof o->requester.pending);
      o->requester.failed = true;
      o->reason = GM7_R_TIMEBASE;
      o->role = o->gm_capable ? GM7_COLD : GM7_RECEIVER;
    }
    return false;
  }
  o->now = now;
  gm7_requester_expire(&o->requester, now);
  return true;
}
int gm7_owner_init(struct gm7_owner *o, const struct r2_group_config *g,
                   uint64_t identity, uint64_t boot, bool gm, uint64_t now) {
  if (!o)
    return -EINVAL;
  *o = (struct gm7_owner){.now = now,
                          .boot_at = now,
                          .gm_capable = gm,
                          .role = gm ? GM7_COLD : GM7_RECEIVER,
                          .source = {.quality = {128, 255, 255, 128, 65535}}};
  return gm7_requester_init(&o->requester, g, identity, boot, now);
}
static int lineage_order(struct gm7_scope a, struct gm7_scope b) {
  if (a.birth != b.birth)
    return a.birth < b.birth ? -1 : 1;
  if (a.founder != b.founder)
    return a.founder < b.founder ? -1 : 1;
  if (a.term != b.term)
    return a.term > b.term ? -1 : 1;
  if (a.active != b.active)
    return a.active < b.active ? -1 : 1;
  return a.boot == b.boot ? 0 : a.boot < b.boot ? -1 : 1;
}
static struct gm7_retained *floor_for(struct gm7_owner *o, uint64_t birth,
                                      uint64_t founder, bool allocate) {
  struct gm7_retained *free = NULL;
  for (unsigned i = 0; i < GM7_RETAINED; i++) {
    struct gm7_retained *r = &o->retained[i];
    if (r->used && !r->terminal && r->birth == birth && r->founder == founder)
      return r;
    if (!r->used && !free)
      free = r;
  }
  if (allocate && free) {
    *free =
        (struct gm7_retained){.birth = birth, .founder = founder, .used = true};
    return free;
  }
  if (allocate) {
    count(&o->overflow);
    o->blockers |= GM7_B_RESOURCE;
    o->reason = GM7_R_RESOURCE;
  }
  return NULL;
}
static bool fenced(const struct gm7_owner *o, const struct gm7_control *c) {
  for (unsigned i = 0; i < GM7_RETAINED; i++) {
    const struct gm7_retained *r = &o->retained[i];
    if (!r->used || r->birth != c->birth_us || r->founder != c->founder)
      continue;
    if (!r->terminal && c->term < r->max_term)
      return true;
    if (r->terminal && c->term == r->term && c->active == r->active &&
        c->active_boot == r->boot)
      return true;
  }
  return false;
}
static bool terminal(struct gm7_owner *o, const struct gm7_control *c) {
  for (unsigned i = 0; i < GM7_RETAINED; i++) {
    struct gm7_retained *r = &o->retained[i];
    if (r->used && r->terminal && r->birth == c->birth_us &&
        r->founder == c->founder && r->term == c->term &&
        r->active == c->active && r->boot == c->active_boot)
      return true;
  }
  for (unsigned i = 0; i < GM7_RETAINED; i++)
    if (!o->retained[i].used) {
      o->retained[i] = (struct gm7_retained){.birth = c->birth_us,
                                             .founder = c->founder,
                                             .term = c->term,
                                             .active = c->active,
                                             .boot = c->active_boot,
                                             .used = true,
                                             .terminal = true};
      return true;
    }
  count(&o->overflow);
  o->blockers |= GM7_B_RESOURCE;
  o->reason = GM7_R_RESOURCE;
  return false;
}
static struct gm7_peer *peer_for(struct gm7_owner *o, uint64_t id,
                                 uint64_t boot, bool allocate) {
  struct gm7_peer *free = NULL;
  for (unsigned i = 0; i < GM7_PEERS; i++) {
    struct gm7_peer *p = &o->peer[i];
    if (p->used && p->identity == id && p->boot == boot)
      return p;
    if (!p->used && !free)
      free = p;
  }
  if (allocate && free) {
    *free = (struct gm7_peer){.identity = id, .boot = boot, .used = true};
    return free;
  }
  if (allocate) {
    count(&o->overflow);
    o->blockers |= GM7_B_RESOURCE;
  }
  return NULL;
}
static bool peer_current(const struct gm7_peer *p, uint64_t now) {
  return p->used && !p->quarantined && p->kind &&
         now < p->session.control_until && gm7_session_live(&p->session, now);
}
static void retire_peers(struct gm7_owner *o) {
  for (unsigned i = 0; i < GM7_PEERS; i++) {
    struct gm7_peer *p = &o->peer[i];
    if (!p->used || p->radio_refs)
      continue;
    bool pending = false;
    for (unsigned j = 0; j < GM7_PENDING; j++)
      if (o->requester.pending[j].used &&
          o->requester.pending[j].request.target == p->identity &&
          o->requester.pending[j].request.target_boot == p->boot) {
        pending = true;
        break;
      }
    bool selected = o->scope.active == p->identity &&
                    o->scope.boot == p->boot &&
                    (o->armed || o->now < o->selected_until ||
                     o->now < o->selected_source_until ||
                     (o->now >= o->selected_original &&
                      o->now - o->selected_original < GM7_ARM_RETAIN_US));
    if (pending || selected || o->now < p->retry_at ||
        o->now < p->session.control_until)
      continue;
    bool replacement = false;
    for (unsigned j = 0; j < GM7_PEERS; j++)
      if (j != i && o->peer[j].identity == p->identity &&
          o->peer[j].boot != p->boot && peer_current(&o->peer[j], o->now)) {
        replacement = true;
        break;
      }
    bool raw_expired = !p->session.valid && o->now >= p->locator_first &&
                       o->now - p->locator_first >= 4500000;
    bool session_expired = !gm7_session_live(&p->session, o->now) &&
                           o->now >= p->locator_at &&
                           o->now - p->locator_at >= 4500000;
    if (raw_expired || session_expired ||
        (replacement && o->now >= p->session.source_until))
      memset(p, 0, sizeof *p);
  }
}
int gm7_owner_peer_pin(struct gm7_owner *o, uint64_t id, uint64_t boot,
                       bool pin, uint64_t now) {
  if (!clock_now(o, now))
    return -ESTALE;
  struct gm7_peer *p = peer_for(o, id, boot, false);
  if (!p)
    return -ENOENT;
  if (pin) {
    if (p->radio_refs == UINT16_MAX)
      return -EOVERFLOW;
    ++p->radio_refs;
  } else {
    if (!p->radio_refs)
      return -EINVAL;
    --p->radio_refs;
  }
  return 0;
}
static void quarantine(struct gm7_owner *o) {
  for (unsigned i = 0; i < GM7_PEERS; i++)
    o->peer[i].quarantined = false;
  for (unsigned i = 0; i < GM7_PEERS; i++)
    for (unsigned j = i + 1; j < GM7_PEERS; j++) {
      struct gm7_peer *a = &o->peer[i], *b = &o->peer[j];
      if (a->used && b->used && a->identity == b->identity &&
          a->boot != b->boot && a->kind && b->kind &&
          o->now < a->session.control_until &&
          o->now < b->session.control_until &&
          gm7_session_live(&a->session, o->now) &&
          gm7_session_live(&b->session, o->now))
        a->quarantined = b->quarantined = true;
    }
}
static bool selected_same(const struct gm7_owner *o,
                          const struct gm7_control *c) {
  return o->selected.birth_us == c->birth_us &&
         o->selected.founder == c->founder && o->selected.term == c->term &&
         o->selected.active == c->active &&
         o->selected.active_boot == c->active_boot;
}
static void select_peer(struct gm7_owner *o, const struct gm7_peer *p) {
  struct gm7_scope next =
      scope_of(&p->session.control, p->session.descriptor.timing_epoch);
  if (!gm7_scope_same(o->scope, next)) {
    /* Stop losing output and pending positive work BEFORE publishing follow. */
    o->serving = false;
    o->active_since = 0;
    disarm(o, GM7_R_SCOPE);
    cancel_positive(o);
    o->prepared = false;
    o->prepared_since = 0;
    o->remote_yield_sequence = o->remote_yield_after = 0;
    o->withdrawn = o->takeover_due = o->initial_due = false;
    o->nominee = o->nominee_boot = 0;
    o->scope = next;
    count(&o->transitions);
    o->reason = GM7_R_MERGE;
  }
  o->selected = p->session.control;
  o->selected_original = p->original;
  o->selected_until = p->session.control_until;
  o->selected_source_until = p->session.source_until;
  o->quiet = false;
  o->takeover_due = false;
  if (o->armed && (p->session.control.hot != o->requester.identity ||
                   p->session.control.hot_boot != o->requester.boot))
    disarm(o, GM7_R_NOMINATION);
}
void gm7_owner_clock(struct gm7_owner *o, gm7_now_fn fn, void *context) {
  if (o) {
    o->clock_read = fn;
    o->clock_context = context;
  }
}
int gm7_owner_receive(struct gm7_owner *o, const uint8_t *ad, size_t size,
                      const uint8_t address[6], uint64_t rx, uint64_t now) {
  if (!clock_now(o, now) || rx > now || o->stopped)
    return -ESTALE;
  struct gm7_packet packet = {0};
  int rc = gm7_ad_decode(&packet, ad, size, &o->requester.group);
  if (rc) {
    count(&o->drops);
    return rc;
  }
  if (o->clock_read) {
    now = o->clock_read(o->clock_context);
    if (!clock_now(o, now) || rx > now)
      return -ESTALE;
  }
  uint64_t id = 0, boot = 0;
  if (packet.kind <= GM7_WITHDRAW) {
    id = packet.body.control.sender;
    boot = packet.body.control.boot;
  } else if (packet.kind == GM7_ANCHOR) {
    id = packet.body.anchor.responder;
    boot = packet.body.anchor.responder_boot;
  } else if (packet.kind == GM7_ROLE) {
    id = packet.body.role.sender;
    boot = packet.body.role.boot;
  } else if (packet.kind == GM7_GNSS) {
    id = packet.body.gnss.sender;
    boot = packet.body.gnss.boot;
  } else
    return -EAGAIN; /* Server REQUEST is dispatched by bounded radio owner. */
  if (id == o->requester.identity)
    return -EADDRINUSE;
  retire_peers(o);
  struct gm7_peer *p = peer_for(o, id, boot, packet.kind <= GM7_STATUS);
  if (!p)
    return -ENOSPC;
  if (packet.kind <= GM7_STATUS) {
    if (!p->locator_seen)
      p->locator_first = rx;
    p->locator_seen = true;
    p->locator_at = rx;
    if (address)
      memcpy(p->address, address, 6);
  }
  if (packet.kind == GM7_ANCHOR) {
    rc = gm7_anchor_accept(&o->requester, &p->session, &packet, rx, now);
    if (rc) {
      if (!p->session.valid) {
        p->kind = 0;
        p->session.control_until = 0;
        if (o->scope.active == id && o->scope.boot == boot) {
          o->selected_source_until = 0;
          disarm(o, GM7_R_SCOPE);
        }
      }
      count(&o->drops);
    } else if (p->kind) {
      /* A different ANCHOR scope has no accepted control, even though its
       * canonical lineage was seeded for subsequent scope comparisons. */
      bool changed = !p->session.control_until;
      if (changed) {
        p->reported = (struct gm7_reported_role){0};
        p->kind = 0;
        p->session.control_until = 0;
        if (o->scope.active == id && o->scope.boot == boot) {
          o->selected_source_until = 0;
          disarm(o, GM7_R_SCOPE);
          o->prepared = false;
          o->prepared_since = 0;
        }
      }
    }
    return rc;
  }
  /* Preserve the small descriptive ROLE facts for updated GM peer screens.
   * Remote GNSS remains in the unchanged GS/GD PT cache. Neither is authority.
   */
  if (packet.kind == GM7_ROLE || packet.kind == GM7_GNSS) {
    uint64_t until;
    rc = gm7_session_diagnostic(&p->session, &packet, rx, now, &until);
    if (!rc && packet.kind == GM7_ROLE) {
      const struct gm7_role *r = &packet.body.role;
      p->reported = (struct gm7_reported_role){r->sampled_mono_us,
                                               until,
                                               r->hot_duration_ms,
                                               r->active_duration_ms,
                                               r->pps_duration_ms,
                                               r->blockers,
                                               r->role,
                                               r->known};
    }
    return rc;
  }
  if (packet.kind == GM7_ANNOUNCE && o->selected.yield_utc_us &&
      selected_same(o, &packet.body.control) &&
      packet.body.control.sequence != o->selected.sequence)
    return -ESTALE;
  if (packet.kind <= GM7_WITHDRAW && (packet.flags & GM7_ELIGIBLE) &&
      packet.body.control.clock_class != 1 &&
      packet.body.control.clock_class != 2)
    return -EINVAL;
  const struct gm7_control prior = p->session.control;
  uint16_t prior_flags = p->flags;
  uint64_t original, until;
  rc = gm7_session_control(&p->session, &packet, rx, now, &original, &until);
  if (rc) {
    if (rc == -EALREADY)
      count(&o->duplicates);
    else
      count(&o->drops);
    if (rc == -EXDEV) {
      p->reported = (struct gm7_reported_role){0};
      gm7_session_clear(&p->session);
      p->kind = 0;
      p->session.control_until = 0;
      if (o->scope.active == id && o->scope.boot == boot) {
        o->selected_source_until = 0;
        disarm(o, GM7_R_SCOPE);
        o->prepared = false;
        o->prepared_since = 0;
      }
    }
    return rc;
  }
  const struct gm7_control *c = &packet.body.control;
  if (o->reserved)
    gm7_owner_reservation_cancel(o, now);
  if (packet.kind == GM7_WITHDRAW || prior.birth_us != c->birth_us ||
      prior.founder != c->founder || prior.term != c->term ||
      prior.active != c->active || prior.active_boot != c->active_boot ||
      prior.source_gen != c->source_gen ||
      prior.capture_gen != c->capture_gen || prior.train_gen != c->train_gen ||
      prior.hot != c->hot || prior.hot_boot != c->hot_boot ||
      ((prior_flags ^ packet.flags) & (GM7_HOT | GM7_SHADOW)))
    p->reported = (struct gm7_reported_role){0};
  if (packet.kind == GM7_WITHDRAW) {
    if (!selected_same(o, c) ||
        (c->hot &&
         (c->hot != o->selected.hot || c->hot_boot != o->selected.hot_boot)) ||
        c->train_gen != o->selected.train_gen || !terminal(o, c))
      return -ESTALE;
    o->selected_source_until = o->selected_until = now;
    o->withdrawn = true;
    o->takeover_due = o->armed;
    cancel_positive(o);
    o->reason = GM7_R_WITHDRAW;
  } else if (packet.kind == GM7_ANNOUNCE) {
    if (fenced(o, c))
      return -ESTALE;
    struct gm7_retained *floor = floor_for(o, c->birth_us, c->founder, true);
    if (!floor)
      return -ENOSPC;
    if (c->term < floor->max_term)
      return -ESTALE;
    if (c->term > floor->max_term)
      floor->max_term = c->term;
    if (o->serving &&
        lineage_order(scope_of(c, p->session.descriptor.timing_epoch),
                      o->scope) < 0) {
      o->serving = false;
      o->active_since = 0;
      cancel_positive(o);
      disarm(o, GM7_R_MERGE);
      o->reason = GM7_R_MERGE;
    }
  }
  p->session.control = *c;
  p->flags = packet.flags;
  p->kind = packet.kind;
  p->original = original;
  p->session.control_until = until;
  quarantine(o);
  if (packet.kind == GM7_ANNOUNCE) {
    o->initial_due = false;
    /* A newly accepted current contact cancels a reserved blackout action
     * immediately, even before the next full-inventory ranking update. */
    if (selected_same(o, c))
      select_peer(o, p);
  }
  if (p->quarantined && p->identity == o->scope.active &&
      p->boot == o->scope.boot)
    disarm(o, GM7_R_SCOPE);

  /* Ranking is recomputed from the complete current inventory in update(),
   * avoiding receive-order selection from partial simultaneous cohorts. */
  return 0;
}
static bool source_current(const struct gm7_owner *o, uint64_t now) {
  return o->source_observing && o->source.gnss_healthy &&
         o->source.pps_healthy && o->source.utc_available &&
         now >= o->source.sampled_us && now < o->source.until_us;
}
static bool project(const struct gm7_source *s, uint64_t now, uint64_t *utc) {
  return s->utc_available && now >= s->sampled_us && now < s->until_us &&
         add(s->utc_us, now - s->sampled_us, utc) && gm7_utc_valid(*utc);
}
static uint64_t
backoff(const struct gm7_owner
            *o) { /* Stable boot-derived election delay, never nonce entropy. */
  return (o->requester.boot ^ (o->requester.boot >> 29) ^
          (o->requester.identity >> 17)) %
         500001;
}
static bool candidate_better(const struct gm7_peer *p,
                             const struct gm7_owner *o) {
  int q = gm7_quality_compare(quality(&p->session.control), o->source.quality);
  return q < 0 || (q == 0 && (p->identity < o->requester.identity ||
                              (p->identity == o->requester.identity &&
                               p->boot < o->requester.boot)));
}
static bool remote_prepared(const struct gm7_owner *o,
                            const struct gm7_peer *p) {
  const struct gm7_control *c = &p->session.control;
  return peer_current(p, o->now) && p->kind == GM7_STATUS &&
         (p->flags & (GM7_ELIGIBLE | GM7_SHADOW | GM7_CAPABLE)) ==
             (GM7_ELIGIBLE | GM7_SHADOW | GM7_CAPABLE) &&
         c->birth_us == o->scope.birth && c->founder == o->scope.founder &&
         c->term == o->scope.term && c->active == o->scope.active &&
         c->active_boot == o->scope.boot && c->train_gen == o->scope.train &&
         c->shadow_gen;
}
static void nominate(struct gm7_owner *o) {
  const struct gm7_peer *best = NULL, *current = NULL;
  for (unsigned i = 0; i < GM7_PEERS; i++) {
    const struct gm7_peer *p = &o->peer[i];
    if (!remote_prepared(o, p))
      continue;
    if (p->identity == o->nominee && p->boot == o->nominee_boot)
      current = p;
    int q = best ? gm7_quality_compare(quality(&p->session.control),
                                       quality(&best->session.control))
                 : -1;
    if (!best || q < 0 ||
        (q == 0 && (p->identity < best->identity ||
                    (p->identity == best->identity && p->boot < best->boot))))
      best = p;
  }
  if (!best) {
    o->yield_ready = false;
    o->nominee = o->nominee_boot = o->nominee_better_since =
        o->nominee_better_id = o->quality_better_since = 0;
    return;
  }
  if (!current) {
    o->nominee = best->identity;
    o->nominee_boot = best->boot;
    o->nominee_better_since = o->nominee_better_id = 0;
  } else if (best != current &&
             gm7_quality_compare(quality(&best->session.control),
                                 quality(&current->session.control)) < 0) {
    if ((o->nominee_better_id != best->identity ||
         o->nominee_better_boot != best->boot)) {
      o->nominee_better_id = best->identity;
      o->nominee_better_boot = best->boot;
      o->nominee_better_since = o->now;
    } else if (o->now - o->nominee_better_since >= 10000000) {
      o->nominee = best->identity;
      o->nominee_boot = best->boot;
      o->nominee_better_since = o->nominee_better_id = 0;
    }
  } else
    o->nominee_better_since = o->nominee_better_id = 0;
  const struct gm7_peer *chosen =
      peer_for(o, o->nominee, o->nominee_boot, false);
  bool better = chosen && remote_prepared(o, chosen) &&
                (chosen->flags & GM7_HOT) &&
                gm7_quality_compare(quality(&chosen->session.control),
                                    o->source.quality) < 0;
  if (!better) {
    o->quality_better_since = o->quality_better_id = o->quality_better_boot = 0;
    o->yield_ready = false;
    return;
  }
  if (o->quality_better_id != chosen->identity ||
      o->quality_better_boot != chosen->boot) {
    o->quality_better_id = chosen->identity;
    o->quality_better_boot = chosen->boot;
    o->quality_better_since = o->now;
  }
  o->yield_ready = !o->event.valid && !o->yield_deadline &&
                   o->now - o->quality_better_since >= 30000000 &&
                   o->now - o->active_since >= 30000000;
  uint64_t utc;
  if (!project(&o->source, o->now, &utc) || o->source.leap_ambiguous ||
      !gm7_calendar_clear(utc) || utc > GM7_UTC_MAX - 750000 ||
      o->now > INT64_MAX - 750000)
    o->yield_ready = false;
}
void gm7_owner_update(struct gm7_owner *o, const struct gm7_source *source,
                      const struct gm7_preparation *prep, uint64_t now) {
  if (!clock_now(o, now) || !source || !prep || o->stopped)
    return;
  retire_peers(o);
  const struct gm7_source previous = o->source;
  bool physical_change = previous.source_gen != source->source_gen ||
                         previous.capture_gen != source->capture_gen ||
                         previous.association_gen != source->association_gen;
  uint64_t utc = 0;
  bool utc_valid = project(source, now, &utc);
  bool healthy = o->gm_capable && source->gnss_healthy && source->pps_healthy &&
                 utc_valid && source->source_gen && source->capture_gen &&
                 source->association_gen && !source->leap_ambiguous;
  /* The existing GNSS parser/source owner fences large discontinuities. Retain
   * an additional same-scope monotonic UTC guard here; UTC is not a boot clock.
   */
  if (healthy && !physical_change && o->last_utc_at && now >= o->last_utc_at &&
      utc < o->last_utc)
    healthy = false;
  if (o->reserved && (!healthy || physical_change || !source->physical_ready ||
                      source->source_gen != o->source.source_gen ||
                      source->capture_gen != o->source.capture_gen ||
                      source->until_us <= o->reserved_end))
    gm7_owner_reservation_cancel(o, now);
  o->source = *source;
  o->preparation = *prep;
  o->source.quality.clock_class = source->commissioned_utc_association ? 1 : 2;
  if (!source->measured_quality) {
    o->source.quality.accuracy = 255;
    o->source.quality.variance = 65535;
  }
  if (source->pps_healthy) {
    if (!o->pps_observing || previous.capture_gen != source->capture_gen) {
      o->pps_since = now;
      o->pps_observing = true;
    }
  } else {
    o->pps_observing = false;
    o->pps_since = 0;
  }
  if (!healthy) {
    o->source_observing = o->eligible = false;
    o->source_since = 0;
  } else {
    if (!o->source_observing || physical_change ||
        now >= o->source_previous_until) {
      o->source_since = now;
      o->source_observing = true;
    }
    o->eligible = now - o->source_since >= GM7_DWELL_US;
    o->last_utc = utc;
    o->last_utc_at = now;
  }
  o->source_previous_until = source->until_us;
  o->blockers = 0;
  if (!healthy)
    o->blockers |= GM7_B_SOURCE;
  if (!o->eligible)
    o->blockers |= GM7_B_DWELL;
  if (!utc_valid)
    o->blockers |= GM7_B_UTC;
  if (source->leap_ambiguous || !gm7_calendar_clear(utc))
    o->blockers |= GM7_B_CALENDAR;
  if (o->serving &&
      (!o->eligible || !source->physical_ready || physical_change))
    gm7_owner_withdraw(o, GM7_R_SOURCE, now);
  if (o->event.valid && o->event.negative && now >= o->event.deadline)
    o->event.valid = false;
  if (o->yield_deadline && now >= o->yield_deadline) {
    o->serving = false;
    o->active_since = 0;
    terminal(o, &o->event.packet.body.control);
    cancel_positive(o);
    o->reason = GM7_R_YIELD;
    o->quiet = true;
    o->quiet_since = now;
    disarm(o, GM7_R_YIELD);
  }
  quarantine(o);
  struct gm7_peer *selected_peer =
      peer_for(o, o->scope.active, o->scope.boot, false);
  if (selected_peer && selected_peer->quarantined) {
    o->selected_source_until = now;
    disarm(o, GM7_R_SCOPE);
    o->takeover_due = false;
  }
  const struct gm7_peer *best = NULL;
  for (unsigned i = 0; i < GM7_PEERS; i++) {
    const struct gm7_peer *p = &o->peer[i];
    if (!peer_current(p, now) || p->kind != GM7_ANNOUNCE ||
        now >= p->session.source_until || fenced(o, &p->session.control))
      continue;
    if (!best ||
        lineage_order(
            scope_of(&p->session.control, p->session.descriptor.timing_epoch),
            scope_of(&best->session.control,
                     best->session.descriptor.timing_epoch)) < 0)
      best = p;
  }
  if (best && (!o->serving ||
               lineage_order(scope_of(&best->session.control,
                                      best->session.descriptor.timing_epoch),
                             o->scope) < 0))
    select_peer(o, best);
  if (o->serving) {
    o->role = GM7_ACTIVE;
    o->initial_due = o->takeover_due = false;
    nominate(o);
    return;
  }
  bool scope = gm7_scope_same(prep->scope, o->scope) && o->scope.active &&
               o->scope.active != o->requester.identity;
  bool physical = scope && prep->map_valid && prep->fit_valid && prep->locked &&
                  prep->correction_age_us <= 250000 && prep->phase_available &&
                  prep->phase_enclosure_us <= 2000 && !prep->health_lost &&
                  now < o->selected_source_until;
  if (!prep->map_valid)
    o->blockers |= GM7_B_MAP;
  if (!prep->fit_valid || !prep->locked)
    o->blockers |= GM7_B_LOCK;
  if (prep->correction_age_us > 250000)
    o->blockers |= GM7_B_CORRECTION;
  if (!scope)
    o->blockers |= GM7_B_SCOPE;
  if (!prep->phase_available || prep->phase_enclosure_us > 2000)
    o->blockers |= GM7_B_PHASE;
  if (physical && o->eligible) {
    if (!o->prepared_since)
      o->prepared_since = now;
    o->prepared = now - o->prepared_since >= GM7_PREPARE_US;
    if (o->prepared && prep->shadow_gen)
      o->shadow_gen = prep->shadow_gen;
  } else {
    o->prepared = false;
    o->prepared_since = 0;
  }
  if (!o->prepared)
    o->blockers |= GM7_B_PREPARE;
  bool nominated = o->selected.hot == o->requester.identity &&
                   o->selected.hot_boot == o->requester.boot;
  if (!nominated)
    o->blockers |= GM7_B_NOMINATION;
  if (o->armed) {
    bool retained = scope && prep->retained_prediction &&
                    prep->retained_enclosure_us <= 8000 &&
                    now >= o->selected_original &&
                    now - o->selected_original < GM7_ARM_RETAIN_US;
    if (!o->eligible || !nominated || prep->health_lost || !retained ||
        physical_change) {
      disarm(o, !o->eligible ? GM7_R_SOURCE : GM7_R_PREPARE);
      o->takeover_due = false;
    }
  } else if (o->eligible && o->prepared && o->shadow_gen && nominated &&
             now < o->selected_until) {
    o->armed = true;
    o->armed_since = now;
    o->role = GM7_ARMED;
    o->reason = GM7_R_NOMINATION;
    count(&o->transitions);
  }
  o->takeover_due = false;
  if (o->armed) {
    o->role = GM7_ARMED;
    if (o->withdrawn || (now >= o->selected_original &&
                         now - o->selected_original >= GM7_BLACKOUT_US))
      o->takeover_due = true;
    if (o->selected.yield_utc_us) {
      /* Offset freshness is broad. Timed YIELD requires the separately accepted
       * physical phase and its immutable source-relative deadline. */
      if (!o->remote_yield_sequence && scope && prep->phase_available &&
          prep->phase_enclosure_us <= 2000 && prep->yield_phase_valid &&
          prep->yield_sequence == o->selected.sequence &&
          prep->yield_after_us >= prep->yield_before_us &&
          prep->yield_after_us - prep->yield_before_us <= 4000) {
        o->remote_yield_sequence = o->selected.sequence;
        o->remote_yield_after = prep->yield_after_us;
      }
      if (o->remote_yield_sequence == o->selected.sequence &&
          o->remote_yield_after && now >= o->remote_yield_after)
        o->takeover_due = true;
    }
  } else
    o->role = o->gm_capable ? (physical ? GM7_WARM : GM7_COLD) : GM7_RECEIVER;
  bool incumbent = best != NULL;
  if (incumbent || o->armed) {
    o->quiet = false;
    o->initial_due = false;
    return;
  }
  if (!o->eligible || !source->physical_ready ||
      (o->blockers & GM7_B_CALENDAR)) {
    o->quiet = false;
    o->initial_due = false;
    return;
  }
  if (!o->quiet) {
    o->quiet = true;
    o->quiet_since = o->selected.active ? o->selected_original : now;
  }
  bool rank = true;
  for (unsigned i = 0; i < GM7_PEERS; i++) {
    const struct gm7_peer *p = &o->peer[i];
    if (peer_current(p, now) && p->kind == GM7_STATUS &&
        (p->flags & (GM7_ELIGIBLE | GM7_CAPABLE)) ==
            (GM7_ELIGIBLE | GM7_CAPABLE) &&
        candidate_better(p, o)) {
      rank = false;
      break;
    }
  }
  uint64_t delay = GM7_QUIET_US + backoff(o);
  o->initial_due =
      rank && now >= o->quiet_since && now - o->quiet_since >= delay;
}
bool gm7_owner_output(const struct gm7_owner *o, uint64_t now) {
  return o && !o->failed && !o->stopped && o->serving && o->eligible &&
         source_current(o, now) && o->source.physical_ready &&
         now >= o->active_since &&
         (!o->yield_deadline || now < o->yield_deadline);
}
/* A conditional reservation may anticipate an immutable not-before edge.
 * It does not advance the owner clock or grant current output authority. */
static bool hot_cause_at(const struct gm7_owner *o, uint64_t action) {
  bool fixed_yield = o->selected.yield_utc_us &&
                     o->remote_yield_sequence == o->selected.sequence &&
                     o->remote_yield_after && action >= o->remote_yield_after;
  bool silence = action >= o->selected_original &&
                 action - o->selected_original >= GM7_BLACKOUT_US;
  return o->armed && o->selected.hot == o->requester.identity &&
         o->selected.hot_boot == o->requester.boot &&
         o->preparation.retained_prediction &&
         o->preparation.retained_enclosure_us <= 8000 &&
         !o->preparation.health_lost &&
         gm7_scope_same(o->preparation.scope, o->scope) &&
         action >= o->selected_original &&
         action - o->selected_original < GM7_ARM_RETAIN_US &&
         (o->withdrawn || silence || fixed_yield);
}
bool gm7_owner_takeover_at(const struct gm7_owner *o, uint64_t nominal,
                           uint64_t end, uint64_t now) {
  uint64_t bound;
  return o && !o->failed && !o->stopped && !o->serving && !o->reserved &&
         o->gm_capable && now >= o->now && nominal >= now &&
         nominal - now <= 250000 && end >= nominal && end - nominal <= 4000 &&
         o->eligible && source_current(o, now) && o->source.physical_ready &&
         o->source.until_us > end && hot_cause_at(o, nominal) &&
         add(o->selected_original, GM7_BLACKOUT_BOUND_US, &bound) &&
         end <= bound;
}
static int activate(struct gm7_owner *o, uint64_t nominal, uint64_t complete,
                    uint64_t now, bool reserve) {
  if (!clock_now(o, now) || o->reserved || o->stopped || !o->gm_capable ||
      !o->eligible || !source_current(o, now) || !o->source.physical_ready ||
      o->source.until_us <= complete ||
      (!o->initial_due &&
       !(reserve ? gm7_owner_takeover_at(o, nominal, complete, now)
                 : o->takeover_due)))
    return -EACCES;
  if ((!reserve && (now < nominal || now - nominal > 2000)) ||
      (reserve && (nominal < now || nominal - now > 250000)) ||
      complete < nominal || complete < now || complete - nominal > 4000) {
    count(&o->late_windows);
    return -ETIME;
  }
  uint64_t action = reserve ? nominal : now;
  bool fixed_yield = o->selected.yield_utc_us &&
                     o->remote_yield_sequence == o->selected.sequence &&
                     o->remote_yield_after && action >= o->remote_yield_after;
  bool hot_takeover = hot_cause_at(o, action);
  if (!o->initial_due && !hot_takeover) {
    o->takeover_due = false;
    return -EACCES;
  }
  /* Receive may have published a fresh ACTIVE since update(). Never activate
   * from a cached due bit over live authority. The sole exception is the
   * exact incumbent's already elapsed physical, immutable YIELD deadline. */
  for (unsigned i = 0; i < GM7_PEERS; i++) {
    const struct gm7_peer *p = &o->peer[i];
    if (o->initial_due && peer_current(p, now) && p->kind == GM7_STATUS &&
        (p->flags & (GM7_ELIGIBLE | GM7_CAPABLE)) ==
            (GM7_ELIGIBLE | GM7_CAPABLE) &&
        candidate_better(p, o)) {
      o->initial_due = false;
      return -EACCES;
    }
    if (p->used && p->identity == o->scope.active && p->boot == o->scope.boot &&
        p->quarantined)
      return -EACCES;
    bool conditional_incumbent =
        reserve && hot_takeover &&
        gm7_scope_same(
            scope_of(&p->session.control, p->session.descriptor.timing_epoch),
            o->scope) &&
        nominal >= p->session.source_until;
    if (peer_current(p, now) && p->kind == GM7_ANNOUNCE &&
        now < p->session.source_until && !conditional_incumbent &&
        !fenced(o, &p->session.control) &&
        !(fixed_yield && selected_same(o, &p->session.control) &&
          p->session.control.sequence == o->remote_yield_sequence))
      return -EACCES;
  }

  struct gm7_scope next = o->scope;
  bool inherit = next.birth && next.founder && next.term;
  if (hot_takeover) {
    uint64_t bound;
    if (!add(o->selected_original, GM7_BLACKOUT_BOUND_US, &bound) ||
        complete > bound) {
      count(&o->missed_bounds);
      o->reason = GM7_R_MISSED_BOUND;
      disarm(o, GM7_R_MISSED_BOUND);
      o->takeover_due = false;
      o->quiet = true;
      o->quiet_since = now;
      return -ETIME;
    }
  }
  if (inherit) {
    struct gm7_retained *known = floor_for(o, next.birth, next.founder, false);
    if (known && known->max_term > next.term)
      next.term = known->max_term;
    if (next.term == UINT64_MAX) {
      o->failed = true;
      o->reason = GM7_R_EXHAUSTED;
      return -EOVERFLOW;
    }
    ++next.term;
  } else {
    uint64_t utc;
    if (!project(&o->source, now, &utc) || o->source.leap_ambiguous ||
        !gm7_calendar_clear(utc))
      return -EACCES;
    next = (struct gm7_scope){
        .birth = utc, .founder = o->requester.identity, .term = 1};
  }
  if (o->timing_epoch == UINT32_MAX || o->train == UINT32_MAX) {
    o->failed = true;
    o->reason = GM7_R_EXHAUSTED;
    return -EOVERFLOW;
  }
  struct gm7_retained *floor = floor_for(o, next.birth, next.founder, true);
  if (!floor)
    return -ENOSPC;
  if (next.term <= floor->max_term && inherit) {
    o->failed = true;
    o->reason = GM7_R_EXHAUSTED;
    return -ESTALE;
  }
  if (!reserve)
    floor->max_term = next.term;
  next.active = o->requester.identity;
  next.boot = o->requester.boot;
  next.source_gen = o->source.source_gen;
  next.capture_gen = o->source.capture_gen;
  next.timing_epoch = ++o->timing_epoch;
  next.train = ++o->train;
  if (reserve) {
    o->reserved_scope = next;
    o->reserved_nominal = nominal;
    o->reserved_end = complete;
    o->reserved_sequence = 0;
    o->reserved = true;
    return 0;
  }
  o->scope = next;
  o->serving = true;
  o->active_since = reserve ? nominal : now;
  o->role = GM7_ACTIVE;
  o->reason = inherit ? GM7_R_TAKEOVER : GM7_R_INITIAL;
  o->armed = false;
  o->armed_since = 0;
  o->prepared = false;
  o->prepared_since = 0;
  o->selected = (struct gm7_control){0};
  o->selected_until = o->selected_source_until = 0;
  o->nominee = o->nominee_boot = 0;
  o->quiet = o->takeover_due = o->initial_due = o->withdrawn = false;
  cancel_positive(o);
  count(&o->transitions);
  /* All old-source receive permissions and queued associations are invalidated
   * by this new scope; estimator transfer requires the physical adapter's own
   * bounded handoff and cannot move old root/capture/train labels. */
  return 0;
}
int gm7_owner_activate(struct gm7_owner *o, uint64_t nominal, uint64_t complete,
                       uint64_t now) {
  return activate(o, nominal, complete, now, false);
}
int gm7_owner_reserve(struct gm7_owner *o, uint64_t nominal, uint64_t complete,
                      uint64_t now) {
  return activate(o, nominal, complete, now, true);
}
static int control(struct gm7_owner *o, struct gm7_packet *p, uint64_t now,
                   bool planned) {
  if (!p || !clock_now(o, now) || o->stopped)
    return -ESTALE;
  const bool serving = o->serving || planned;
  const struct gm7_scope *scope = planned ? &o->reserved_scope : &o->scope;
  if (!planned && o->event.valid) {
    *p = o->event.packet;
    return 0;
  }
  if (o->sequence == UINT64_MAX) {
    o->failed = true;
    o->serving = false;
    o->reason = GM7_R_EXHAUSTED;
    return -EOVERFLOW;
  }
  *p = (struct gm7_packet){.group = o->requester.group.id,
                           .key_epoch = o->requester.group.epoch,
                           .kind = serving ? GM7_ANNOUNCE : GM7_STATUS};
  struct gm7_control *c = &p->body.control;
  c->sender = o->requester.identity;
  c->boot = o->requester.boot;
  c->sequence = ++o->sequence;
  c->sent_mono_us = now;
  c->max_age_us = serving ? 2750000 : 4500000;
  (void)project(&o->source, now, &c->sent_utc_us);
  c->source_gen = o->source.source_gen;
  c->capture_gen = o->source.capture_gen;
  c->priority1 = o->source.quality.priority1;
  c->clock_class = o->eligible ? o->source.quality.clock_class : 255;
  c->accuracy = o->source.quality.accuracy;
  c->priority2 = o->source.quality.priority2;
  c->variance = o->source.quality.variance;
  if (o->eligible)
    p->flags |= GM7_ELIGIBLE;
  if (serving) {
    if (!(planned ? (o->reserved && source_current(o, now) && o->eligible &&
                     o->source.physical_ready &&
                     o->source.until_us > o->reserved_end)
                  : gm7_owner_output(o, now)))
      return -ESTALE;
    c->birth_us = scope->birth;
    c->founder = scope->founder;
    c->term = scope->term;
    c->active = scope->active;
    c->active_boot = scope->boot;
    c->train_gen = scope->train;
    c->hot = o->nominee;
    c->hot_boot = o->nominee_boot;
    uint64_t remaining = o->source.until_us - now;
    c->source_valid_us = remaining > 1500000 ? 1500000 : (uint32_t)remaining;
  } else {
    if (o->gm_capable)
      p->flags |= GM7_CAPABLE;
    if (o->scope.active && o->scope.active != o->requester.identity &&
        now >= o->selected_original &&
        now - o->selected_original < GM7_ARM_RETAIN_US) {
      c->birth_us = o->scope.birth;
      c->founder = o->scope.founder;
      c->term = o->scope.term;
      c->active = o->scope.active;
      c->active_boot = o->scope.boot;
      if ((o->prepared || o->armed) && o->shadow_gen) {
        p->flags |= GM7_SHADOW;
        c->shadow_gen = o->shadow_gen;
        c->train_gen = o->scope.train;
      }
      if (o->armed) {
        p->flags |= GM7_HOT;
        c->hot = c->sender;
        c->hot_boot = c->boot;
      }
    }
  }
  if (!planned && o->serving && o->yield_ready) {
    if (c->sent_utc_us > GM7_UTC_MAX - 750000 || now > INT64_MAX - 750000)
      return -EOVERFLOW;
    p->flags |= GM7_YIELD;
    c->yield_utc_us = c->sent_utc_us + 750000;
    o->event = (struct gm7_event){.packet = *p,
                                  .first = now,
                                  .deadline = now + 750000,
                                  .generation = o->event_generation,
                                  .valid = true};
    o->yield_deadline = o->event.deadline;
    o->yield_ready = false;
    o->reason = GM7_R_YIELD;
  }
  if (planned)
    o->reserved_sequence = c->sequence;
  return gm7_validate(p);
}
int gm7_owner_control(struct gm7_owner *o, struct gm7_packet *p, uint64_t now) {
  return control(o, p, now, false);
}
int gm7_owner_planned_control(struct gm7_owner *o, struct gm7_packet *p,
                              uint64_t now) {
  if (!o || !o->reserved || o->reserved_sequence || now >= o->reserved_nominal)
    return -EACCES;
  return control(o, p, now, true);
}
void gm7_owner_reservation_cancel(struct gm7_owner *o, uint64_t now) {
  if (!clock_now(o, now) || !o->reserved)
    return;
  if (o->reservation_fence && o->reservation_fence(o->reservation_context)) {
    gm7_owner_reservation_failed(o, true, now);
    return;
  }
  struct gm7_retained *provisional =
      floor_for(o, o->reserved_scope.birth, o->reserved_scope.founder, false);
  if (provisional && !provisional->max_term)
    *provisional = (struct gm7_retained){0};
  o->reserved = false;
  o->reserved_sequence = 0;
  cancel_positive(o); /* tickets, sequences and timing tokens remain consumed */
}
void gm7_owner_reservation_failed(struct gm7_owner *o, bool exposed,
                                  uint64_t now) {
  if (!clock_now(o, now) || !o->reserved)
    return;
  if (!exposed) {
    gm7_owner_reservation_cancel(o, now);
    return;
  }
  struct gm7_retained *floor =
      floor_for(o, o->reserved_scope.birth, o->reserved_scope.founder, true);
  if (floor && floor->max_term < o->reserved_scope.term)
    floor->max_term = o->reserved_scope.term;
  o->scope = o->reserved_scope;
  o->reserved = false;
  o->serving = true;
  /* This temporary owner transition exists solely to generate the terminal
   * negative. No UI, G1, JOIN or output reader observes a serving interval. */
  gm7_owner_withdraw(o, GM7_R_MISSED_BOUND, now);
  count(&o->missed_bounds);
}
int gm7_owner_reservation_commit(struct gm7_owner *o, uint64_t first,
                                 uint64_t end, uint64_t now) {
  if (!clock_now(o, now) || !o->reserved || !o->reserved_sequence ||
      first < o->reserved_nominal || first - o->reserved_nominal > 2000 ||
      end < first || end - first > 2000 || end > o->reserved_end || end > now ||
      !o->eligible || !source_current(o, now) || !o->source.physical_ready ||
      (!o->initial_due && !hot_cause_at(o, first)))
    return -ESTALE;
  struct gm7_retained *floor =
      floor_for(o, o->reserved_scope.birth, o->reserved_scope.founder, true);
  if (!floor || floor->max_term >= o->reserved_scope.term)
    return -ESTALE;
  floor->max_term = o->reserved_scope.term;
  o->scope = o->reserved_scope;
  o->reserved = false;
  o->serving = true;
  o->active_since = now;
  o->first_announce_start = first;
  o->first_announce_end = end;
  o->role = GM7_ACTIVE;
  o->reason = o->scope.term == 1 ? GM7_R_INITIAL : GM7_R_TAKEOVER;
  o->armed = o->prepared = false;
  o->armed_since = o->prepared_since = 0;
  o->selected = (struct gm7_control){0};
  o->selected_until = o->selected_source_until = 0;
  o->nominee = o->nominee_boot = 0;
  o->quiet = o->takeover_due = o->initial_due = o->withdrawn = false;
  cancel_positive(o);
  count(&o->transitions);
  return 0;
}
void gm7_owner_withdraw(struct gm7_owner *o, unsigned reason, uint64_t now) {
  if (!clock_now(o, now))
    return;
  bool was_serving = o->serving;
  /* Withdrawal is synchronous owner truth; no advertiser queue/copy/result can
   * defer output stop. Cancel the complete positive generation first. */
  o->serving = false;
  o->active_since = 0;
  disarm(o, reason);
  cancel_positive(o);
  o->initial_due = o->takeover_due = false;
  o->reason = reason;
  if (!was_serving || o->sequence == UINT64_MAX || !o->scope.birth)
    return;
  struct gm7_packet p = {.group = o->requester.group.id,
                         .key_epoch = o->requester.group.epoch,
                         .kind = GM7_WITHDRAW};
  struct gm7_control *c = &p.body.control;
  c->sender = c->active = o->requester.identity;
  c->boot = c->active_boot = o->requester.boot;
  c->sequence = ++o->sequence;
  c->sent_mono_us = now;
  c->max_age_us = 1000000;
  c->source_gen = o->scope.source_gen;
  c->capture_gen = o->scope.capture_gen;
  c->train_gen = o->scope.train;
  c->birth_us = o->scope.birth;
  c->founder = o->scope.founder;
  c->term = o->scope.term;
  c->hot = o->nominee;
  c->hot_boot = o->nominee_boot;
  c->priority1 = o->source.quality.priority1;
  c->clock_class = o->source.quality.clock_class;
  c->accuracy = o->source.quality.accuracy;
  c->priority2 = o->source.quality.priority2;
  c->variance = o->source.quality.variance;
  (void)project(&o->source, now, &c->sent_utc_us);
  uint64_t deadline;
  if (gm7_validate(&p) || !add(now, 1000000, &deadline)) {
    o->failed = true;
    return;
  }
  (void)terminal(o, c);
  o->event = (struct gm7_event){.packet = p,
                                .first = UINT64_MAX,
                                .deadline = deadline,
                                .generation = o->event_generation,
                                .valid = true,
                                .negative = true};
  count(&o->transitions);
}
int gm7_owner_event_align(struct gm7_owner *o, uint64_t first, uint64_t now) {
  if (!clock_now(o, now) || !o->event.valid || o->event.sent)
    return -ESTALE;
  uint64_t original = o->event.packet.body.control.sent_mono_us;
  if (first < original) {
    if (original - first > 2000)
      return -ETIME;
  } else if (first - original > 250000)
    return -ETIME;
  if (first > o->event.deadline || o->event.deadline - first < 504000)
    return -ETIME;
  if (o->event.first != UINT64_MAX && o->event.first != original &&
      o->event.first != first)
    return -EALREADY;
  o->event.first = first;
  return 0;
}
void gm7_owner_stop(struct gm7_owner *o, uint64_t now) {
  if (!clock_now(o, now))
    return;
  gm7_owner_withdraw(o, GM7_R_STOP, now);
  o->stopped = true;
  memset(o->requester.pending, 0, sizeof o->requester.pending);
}
int gm7_owner_next_request_at(struct gm7_owner *o, struct gm7_packet *out,
                              uint64_t now, uint64_t planned_tx,
                              gm7_random_fn random, void *context) {
  if (!clock_now(o, now) || o->stopped || planned_tx < now ||
      planned_tx - now > 250000)
    return -ESTALE;
  /* Fair round-robin by stable full identity, not quality veto. Raw locators
   * never reset quiet time or enter candidate ranking. */
  struct gm7_peer *best = NULL;
  for (unsigned i = 0; i < GM7_PEERS; i++) {
    struct gm7_peer *p = &o->peer[i];
    if (!p->used || !p->locator_seen || planned_tx < p->retry_at ||
        now < p->locator_at || now - p->locator_at >= 4500000 ||
        !gm7_session_due(&p->session, now))
      continue;
    if (!best || p->retry_at < best->retry_at ||
        (p->retry_at == best->retry_at &&
         (p->identity < best->identity ||
          (p->identity == best->identity && p->boot < best->boot))))
      best = p;
  }
  if (!best)
    return -EAGAIN;
  return gm7_owner_request_peer(o, out, best->identity, best->boot, now,
                                planned_tx, random, context);
}
int gm7_owner_request_peer(struct gm7_owner *o, struct gm7_packet *out,
                           uint64_t peer, uint64_t boot, uint64_t now,
                           uint64_t planned_tx, gm7_random_fn random,
                           void *context) {
  if (!clock_now(o, now) || o->stopped || planned_tx < now ||
      planned_tx - now > 250000)
    return -ESTALE;
  struct gm7_peer *best = peer_for(o, peer, boot, false);
  if (!best || !best->locator_seen || planned_tx < best->retry_at ||
      now < best->locator_at || now - best->locator_at >= 4500000 ||
      !gm7_session_due(&best->session, now))
    return -EAGAIN;
  static const uint64_t cooldown = 2000000;
  int rc = gm7_request_begin(&o->requester, out, best->identity, best->boot,
                             now, random, context);
  if (rc != -ENOSPC) {
    if (!add(planned_tx, cooldown, &best->retry_at)) {
      o->failed = true;
      return -EOVERFLOW;
    }
  }
  if (o->requester.failed) {
    o->failed = true;
    o->serving = false;
    disarm(o, GM7_R_TIMEBASE);
    o->event.valid = false;
  }
  return rc;
}
int gm7_owner_next_request(struct gm7_owner *o, struct gm7_packet *out,
                           uint64_t now, gm7_random_fn random, void *context) {
  return gm7_owner_next_request_at(o, out, now, now, random, context);
}
int gm7_owner_anchor(struct gm7_owner *o, struct gm7_packet *out,
                     const struct gm7_packet *request, uint64_t received,
                     uint64_t now) {
  if (!out || !request || !clock_now(o, now) || o->stopped ||
      gm7_validate(request) || request->kind != GM7_REQUEST ||
      request->group != o->requester.group.id ||
      request->key_epoch != o->requester.group.epoch ||
      request->body.request.target != o->requester.identity ||
      request->body.request.target_boot != o->requester.boot ||
      received > now || now - received >= GM7_RTT_US || !o->eligible ||
      !source_current(o, now))
    return -EACCES;
  if (o->response_sequence == UINT64_MAX) {
    o->failed = true;
    o->serving = false;
    return -EOVERFLOW;
  }
  *out = (struct gm7_packet){.group = o->requester.group.id,
                             .key_epoch = o->requester.group.epoch,
                             .kind = GM7_ANCHOR,
                             .flags = GM7_A_INDEPENDENT};
  struct gm7_anchor *a = &out->body.anchor;
  const struct gm7_request *r = &request->body.request;
  a->requester = r->requester;
  a->requester_boot = r->requester_boot;
  a->nonce_hi = r->nonce_hi;
  a->nonce_lo = r->nonce_lo;
  a->responder = o->requester.identity;
  a->responder_boot = o->requester.boot;
  a->response_sequence = ++o->response_sequence;
  a->sampled_mono_us = now;
  a->source_gen = o->source.source_gen;
  a->capture_gen = o->source.capture_gen;
  a->control_sequence = o->sequence ? o->sequence : 1;
  a->utc_error_us = UINT32_MAX;
  if (project(&o->source, now, &a->utc_us))
    out->flags |= GM7_A_UTC;
  uint64_t left = o->source.until_us - now;
  a->valid_us = left > 1500000 ? 1500000 : (uint32_t)left;
  if (gm7_owner_output(o, now)) {
    out->flags |= GM7_A_SERVING;
    a->birth_us = o->scope.birth;
    a->founder = o->scope.founder;
    a->term = o->scope.term;
    a->active = o->scope.active;
    a->active_boot = o->scope.boot;
    a->timing_epoch = o->scope.timing_epoch;
    a->train_gen = o->scope.train;
  }
  return gm7_validate(out);
}
bool gm7_candidate_mesh_current(const struct gm7_peer *p,
                                const struct gm7_scope *root, uint64_t now) {
  if (!p || !root || !root->timing_epoch || !root->capture_gen ||
      !root->train || !peer_current(p, now) || p->kind != GM7_STATUS ||
      !(p->flags & GM7_SHADOW))
    return false;
  const struct gm7_control *c = &p->session.control;
  /* Sender-local source/capture generations retain their own meaning. This
   * boolean is receiver-derived association, never a substituted wire token. */
  return c->birth_us == root->birth && c->founder == root->founder &&
         c->term == root->term && c->active == root->active &&
         c->active_boot == root->boot && c->train_gen == root->train &&
         c->shadow_gen && c->source_gen == p->session.descriptor.source_gen &&
         c->capture_gen == p->session.descriptor.capture_gen;
}

bool gm7_peer_role(const struct gm7_owner *o, unsigned index, uint64_t now,
                   struct gm7_reported_role *out, bool *current) {
  if (!o || !out || !current || index >= GM7_PEERS || o->failed || o->stopped ||
      now < o->now)
    return false;
  const struct gm7_peer *p = &o->peer[index];
  if (!p->used || !p->reported.until || now >= p->reported.until ||
      !gm7_session_live(&p->session, now))
    return false;
  *out = p->reported;
  *current = false;
  bool physical = o->serving ||
                  (gm7_scope_same(o->preparation.scope, o->scope) &&
                   o->preparation.phase_available && (o->prepared || o->armed));
  if (physical && peer_current(p, now)) {
    if (p->kind == GM7_STATUS)
      *current = gm7_candidate_mesh_current(p, &o->scope, now);
    else if (p->kind == GM7_ANNOUNCE)
      *current = gm7_scope_same(scope_of(&p->session.control,
                                         p->session.descriptor.timing_epoch),
                                o->scope) &&
                 now < p->session.source_until;
  }
  if (*current && p->reported.role == GM7_W_HOT) {
    uint64_t nominee = o->serving ? o->nominee : o->selected.hot;
    uint64_t nominee_boot = o->serving ? o->nominee_boot : o->selected.hot_boot;
    if (p->kind != GM7_STATUS ||
        (p->flags & (GM7_ELIGIBLE | GM7_SHADOW | GM7_HOT)) !=
            (GM7_ELIGIBLE | GM7_SHADOW | GM7_HOT) ||
        p->identity != nominee || p->boot != nominee_boot)
      *current = false;
  }
  return true;
}

void gm7_owner_reservation_guard(struct gm7_owner *o,
                                 gm7_reservation_fence_fn fn, void *context) {
  if (o) {
    o->reservation_fence = fn;
    o->reservation_context = context;
  }
}
