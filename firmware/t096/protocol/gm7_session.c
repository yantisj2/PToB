/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_session.h"
#include <errno.h>
#include <string.h>
#include <tinycrypt/hmac.h>
static bool plus(uint64_t a, uint64_t b, uint64_t *v) {
  if (a > UINT64_MAX - b)
    return false;
  *v = a + b;
  return true;
}
void gm7_session_clear(struct gm7_session *s) {
  if (!s)
    return;
  /* Retire authority while retaining same-boot replay watermarks. A fresh
   * nonce cannot make a previously accepted stream counter new again. */
  struct gm7_session old = *s;
  memset(s, 0, sizeof *s);
  s->group = old.group;
  s->key_epoch = old.key_epoch;
  s->peer = old.peer;
  s->peer_boot = old.peer_boot;
  s->local_boot = old.local_boot;
  s->floor = old.floor;
  s->last_sequence = old.last_sequence;
  s->last_sent = old.last_sent;
  s->last_diag[0] = old.last_diag[0];
  s->last_diag[1] = old.last_diag[1];
}
static bool requester_time(struct gm7_requester *r, uint64_t now) {
  if (r->failed || now < r->last_now || now > INT64_MAX) {
    r->failed = true;
    memset(r->pending, 0, sizeof r->pending);
    return false;
  }
  r->last_now = now;
  return true;
}
int gm7_requester_init(struct gm7_requester *r, const struct r2_group_config *g,
                       uint64_t identity, uint64_t boot, uint64_t now) {
  if (!r || !r2_group_valid(g) || !identity || !boot || now > INT64_MAX)
    return -EINVAL;
  *r = (struct gm7_requester){
      .group = *g, .identity = identity, .boot = boot, .last_now = now};
  return 0;
}
void gm7_requester_expire(struct gm7_requester *r, uint64_t now) {
  if (!r || !requester_time(r, now))
    return;
  for (unsigned i = 0; i < GM7_PENDING; i++) {
    struct gm7_pending *p = &r->pending[i];
    if (p->used && now > p->deadline)
      memset(p, 0, sizeof *p);
  }
}
int gm7_request_begin(struct gm7_requester *r, struct gm7_packet *out,
                      uint64_t peer, uint64_t boot, uint64_t now,
                      gm7_random_fn random, void *context) {
  if (!r || !out || !peer || !boot || peer == r->identity || !random ||
      !requester_time(r, now))
    return -EINVAL;
  gm7_requester_expire(r, now);
  struct gm7_pending *slot = NULL;
  for (unsigned i = 0; i < GM7_PENDING; i++) {
    struct gm7_pending *p = &r->pending[i];
    if (p->used && p->request.target == peer)
      return -EALREADY;
    if (!p->used && !slot)
      slot = p;
  }
  if (!slot)
    return -ENOSPC;
  if (r->sequence == UINT64_MAX || now > INT64_MAX - GM7_RTT_US) {
    r->failed = true;
    return -EOVERFLOW;
  }
  /* The CSPRNG is required even after seeding. A nonwrapping allocation
   * counter enters the PRF, so repeated entropy cannot reproduce a consumed
   * request nonce in this local boot. No deterministic identity/boot hash or
   * exposed plain counter replaces any part of the unpredictable128 bits. */
  if (!r->seeded) {
    uint8_t nonzero = 0;
    if (random(context, r->nonce_seed, sizeof r->nonce_seed)) {
      r->failed = true;
      return -EIO;
    }
    for (unsigned i = 0; i < sizeof r->nonce_seed; i++)
      nonzero |= r->nonce_seed[i];
    if (!nonzero) {
      r->failed = true;
      return -EIO;
    }
    r->seeded = true;
  }
  uint8_t entropy[16], tuple[24], digest[32];
  if (random(context, entropy, sizeof entropy)) {
    r->failed = true;
    memset(r->pending, 0, sizeof r->pending);
    return -EIO;
  }
  uint64_t values[3] = {r->identity, r->boot, r->sequence + 1};
  for (unsigned j = 0; j < 3; j++)
    for (unsigned i = 0; i < 8; i++)
      tuple[j * 8 + i] = (uint8_t)(values[j] >> (56 - 8 * i));
  struct tc_hmac_state_struct h;
  static const uint8_t domain[] = "GM7v2-nonce";
  if (!tc_hmac_set_key(&h, r->group.psk, 32) || !tc_hmac_init(&h) ||
      !tc_hmac_update(&h, domain, sizeof domain) ||
      !tc_hmac_update(&h, r->nonce_seed, sizeof r->nonce_seed) ||
      !tc_hmac_update(&h, tuple, sizeof tuple) ||
      !tc_hmac_update(&h, entropy, sizeof entropy) ||
      !tc_hmac_final(digest, sizeof digest, &h)) {
    r->failed = true;
    memset(r->pending, 0, sizeof r->pending);
    return -EIO;
  }
  uint64_t hi = 0, lo = 0;
  for (unsigned i = 0; i < 8; i++) {
    hi = (hi << 8) | digest[i];
    lo = (lo << 8) | digest[8 + i];
  }
  memset(entropy, 0, sizeof entropy);
  memset(digest, 0, sizeof digest);
  if (!(hi || lo)) {
    r->failed = true;
    return -EIO;
  }
  for (unsigned i = 0; i < GM7_PENDING; i++)
    if (r->pending[i].used && r->pending[i].request.nonce_hi == hi &&
        r->pending[i].request.nonce_lo == lo) {
      r->failed = true;
      memset(r->pending, 0, sizeof r->pending);
      return -EIO;
    }
  struct gm7_request q = {.requester = r->identity,
                          .requester_boot = r->boot,
                          .target = peer,
                          .target_boot = boot,
                          .nonce_hi = hi,
                          .nonce_lo = lo,
                          .request_sequence = ++r->sequence};
  *slot = (struct gm7_pending){.request = q,
                               .allocated = now,
                               .deadline = now + GM7_RTT_US,
                               .used = true};
  *out = (struct gm7_packet){.group = r->group.id,
                             .key_epoch = r->group.epoch,
                             .kind = GM7_REQUEST,
                             .body.request = q};
  return 0;
}
static bool same_request(const struct gm7_request *a,
                         const struct gm7_request *b) {
  return a->requester == b->requester &&
         a->requester_boot == b->requester_boot && a->target == b->target &&
         a->target_boot == b->target_boot && a->nonce_hi == b->nonce_hi &&
         a->nonce_lo == b->nonce_lo &&
         a->request_sequence == b->request_sequence;
}
int gm7_request_host_bound(struct gm7_requester *r, const struct gm7_request *q,
                           uint32_t quantum, uint64_t now) {
  if (!r || !q || !quantum || quantum > 1000 || !requester_time(r, now))
    return -EINVAL;
  for (unsigned i = 0; i < GM7_PENDING; i++) {
    struct gm7_pending *p = &r->pending[i];
    if (p->used && same_request(&p->request, q)) {
      if (p->sent)
        return -EALREADY;
      uint64_t lower = p->allocated >= quantum ? p->allocated - quantum : 0;
      if (lower > INT64_MAX - GM7_RTT_US || now > lower + GM7_RTT_US) {
        memset(p, 0, sizeof *p);
        return -ETIME;
      }
      p->tx = lower;
      p->deadline = lower + GM7_RTT_US;
      p->sent = true;
      p->endpoint = GM7_HOST_CAUSAL_BOUND_NOT_RF;
      p->quantization_us = quantum;
      return 0;
    }
  }
  return -ENOENT;
}
int gm7_request_sent(struct gm7_requester *r, const struct gm7_request *q,
                     uint64_t tx, uint64_t now) {
  if (!r || !q || !requester_time(r, now) || tx > now)
    return -EINVAL;
  for (unsigned i = 0; i < GM7_PENDING; i++) {
    struct gm7_pending *p = &r->pending[i];
    if (p->used && same_request(&p->request, q)) {
      if (p->sent)
        return -EALREADY;
      if (tx < p->allocated || tx >= p->deadline ||
          tx > INT64_MAX - GM7_RTT_US) {
        memset(p, 0, sizeof *p);
        return -ETIME;
      }
      p->sent = true;
      p->tx = tx;
      p->deadline = tx + GM7_RTT_US;
      return 0;
    }
  }
  return -ENOENT;
}
int gm7_anchor_accept(struct gm7_requester *r, struct gm7_session *s,
                      const struct gm7_packet *p, uint64_t rx, uint64_t now) {
  if (!r || !s || !p || !requester_time(r, now) || rx > now ||
      gm7_validate(p) || p->kind != GM7_ANCHOR || p->group != r->group.id ||
      p->key_epoch != r->group.epoch)
    return -EINVAL;
  const struct gm7_anchor *a = &p->body.anchor;
  if (a->requester != r->identity || a->requester_boot != r->boot ||
      a->sampled_mono_us > INT64_MAX)
    return -ESTALE;
  struct gm7_pending *pending = NULL;
  for (unsigned i = 0; i < GM7_PENDING; i++) {
    struct gm7_pending *q = &r->pending[i];
    if (q->used && q->request.target == a->responder &&
        q->request.target_boot == a->responder_boot &&
        q->request.nonce_hi == a->nonce_hi &&
        q->request.nonce_lo == a->nonce_lo) {
      pending = q;
      break;
    }
  }
  if (!pending || !pending->sent)
    return -ESTALE;
  uint8_t endpoint = pending->endpoint;
  uint32_t quantum = pending->quantization_us;
  if (endpoint == GM7_HOST_CAUSAL_BOUND_NOT_RF) {
    if (now > (uint64_t)INT64_MAX - quantum)
      return -EOVERFLOW;
    rx = now + quantum;
  }
  const uint64_t tx = pending->tx;
  if (rx < tx)
    return -ESTALE;
  uint64_t deadline = pending->deadline, rtt = rx - tx;
  /* Consume the authenticated exact outstanding nonce once, including a
   * response whose original source horizon is exhausted by the measured RTT. */
  memset(pending, 0, sizeof *pending);
  if (rx > deadline || rtt > GM7_RTT_US || rtt >= a->valid_us ||
      now > INT64_MAX - GM7_SESSION_US ||
      (now > rx && now - rx >= a->valid_us - rtt))
    return -ESTALE;
  int64_t offset_lo = (int64_t)tx - (int64_t)a->sampled_mono_us,
          offset_hi = (int64_t)rx - (int64_t)a->sampled_mono_us;
  bool same_boot = s->group == p->group && s->key_epoch == p->key_epoch &&
                   s->peer == a->responder &&
                   s->peer_boot == a->responder_boot &&
                   s->local_boot == r->boot;
  struct gm7_session prior = *s;
  if (same_boot && s->valid) {
    uint64_t old_lo, old_hi;
    int rc = gm7_session_map(s, a->sampled_mono_us, now, &old_lo, &old_hi);
    if (rc || old_lo > rx || old_hi < tx) {
      gm7_session_clear(s);
      return -ERANGE;
    }
    /* Intersect the new causal bracket with the old supported drift envelope.
     * A renewal can tighten freshness; it cannot erase an observed clock step.
     */
    if (old_lo > tx)
      offset_lo = (int64_t)old_lo - (int64_t)a->sampled_mono_us;
    if (old_hi < rx)
      offset_hi = (int64_t)old_hi - (int64_t)a->sampled_mono_us;
  }
  uint64_t jitter = (a->nonce_hi ^ a->nonce_lo) % GM7_RENEW_SPAN_US;
  *s = (struct gm7_session){
      .group = p->group,
      .key_epoch = p->key_epoch,
      .peer = a->responder,
      .peer_boot = a->responder_boot,
      .local_boot = r->boot,
      .floor = same_boot && s->floor > a->control_sequence ? s->floor : a->control_sequence,
      .established = endpoint == GM7_HOST_CAUSAL_BOUND_NOT_RF ? now : rx,
      .until = (endpoint == GM7_HOST_CAUSAL_BOUND_NOT_RF ? now : rx) +
               GM7_SESSION_US,
      .renew_at = (endpoint == GM7_HOST_CAUSAL_BOUND_NOT_RF ? now : rx) +
                  GM7_RENEW_MIN_US + jitter,
      .drift_from = tx,
      .endpoint_lower = tx,
      .endpoint_upper = rx,
      .conservative_elapsed_us = (uint32_t)rtt,
      .endpoint = endpoint,
      .offset_lo = offset_lo,
      .offset_hi = offset_hi,
      .source_until = rx + a->valid_us - rtt,
      .control = {.sender = a->responder,
                  .boot = a->responder_boot,
                  .birth_us = a->birth_us,
                  .founder = a->founder,
                  .term = a->term,
                  .active = a->active,
                  .active_boot = a->active_boot,
                  .sent_mono_us = a->sampled_mono_us,
                  .sent_utc_us = a->utc_us},
      .descriptor = {.birth = a->birth_us, .founder = a->founder,
          .term = a->term, .timing_epoch = a->timing_epoch,
          .source_gen = a->source_gen, .capture_gen = a->capture_gen,
          .train_gen = a->train_gen},
      .anchor_flags = p->flags,
      .valid = true};
  if (same_boot) {
    if (s->floor < prior.last_sequence) s->floor = prior.last_sequence;
    s->last_sequence = prior.last_sequence;
    s->last_sent = prior.last_sent;
    s->last_diag[0] = prior.last_diag[0];
    s->last_diag[1] = prior.last_diag[1];
    /* A new nonce refreshes only the causal clock bracket. Role, lineage,
     * source and train changes must not erase accepted control/replay state.
     * The separately sampled physical descriptor grants no control authority. */
    if (prior.control_until) {
      s->control = prior.control;
      s->control_until = prior.control_until;
      s->source_until = prior.source_until;
    }
  }
  return 0;
}
bool gm7_session_live(const struct gm7_session *s, uint64_t now) {
  return s && s->valid && s->group && s->key_epoch && s->peer && s->peer_boot &&
         s->local_boot && now >= s->established && now < s->until;
}
bool gm7_session_timing_current(const struct gm7_session *s) {
  const struct gm7_control *c = &s->control;
  const struct gm7_bound_anchor *a = &s->descriptor;
  return s->control_until && c->source_valid_us && (s->anchor_flags & GM7_A_SERVING) &&
         !(s->anchor_flags & GM7_A_RELAY) && a->timing_epoch &&
         c->active == s->peer && c->active_boot == s->peer_boot &&
         c->birth_us == a->birth && c->founder == a->founder &&
         c->term == a->term &&
         c->source_gen == a->source_gen && c->capture_gen == a->capture_gen &&
         c->train_gen == a->train_gen;
}
bool gm7_session_due(const struct gm7_session *s, uint64_t now) {
  return !gm7_session_live(s, now) || now >= s->renew_at ||
         (s->control_until && s->control.active == s->peer &&
          !gm7_session_timing_current(s));
}
static bool offset(uint64_t remote, int64_t delta, uint64_t *out) {
  if (delta >= 0)
    return plus(remote, (uint64_t)delta, out);
  uint64_t d = (uint64_t)(-(delta + 1)) + 1;
  if (remote < d)
    return false;
  *out = remote - d;
  return true;
}
int gm7_session_map(const struct gm7_session *s, uint64_t remote, uint64_t now,
                    uint64_t *lo, uint64_t *hi) {
  if (!lo || !hi || !gm7_session_live(s, now) || remote > INT64_MAX)
    return -ESTALE;
  if (now < s->drift_from)
    return -ESTALE;
  uint64_t age = now - s->drift_from, drift = (age * 200 + 999999) / 1000000;
  uint64_t width = (uint64_t)(s->offset_hi - s->offset_lo);
  if (width > GM7_OFFSET_MAX_US || drift > (GM7_OFFSET_MAX_US - width) / 2)
    return -ERANGE;
  if (s->offset_lo < INT64_MIN + (int64_t)drift ||
      s->offset_hi > INT64_MAX - (int64_t)drift ||
      !offset(remote, s->offset_lo - (int64_t)drift, lo) ||
      !offset(remote, s->offset_hi + (int64_t)drift, hi))
    return -EOVERFLOW;
  return 0;
}
int gm7_session_control_check(struct gm7_session *s, const struct gm7_packet *p,
                        uint64_t rx, uint64_t now, uint64_t *original,
                        uint64_t *until, uint64_t *source_until) {
  if (!s || !p || !original || !until || !source_until || rx > now || gm7_validate(p) ||
      p->kind > GM7_WITHDRAW || p->group != s->group ||
      p->key_epoch != s->key_epoch)
    return -EINVAL;
  const struct gm7_control *c = &p->body.control;
  if (c->sender != s->peer || c->boot != s->peer_boot ||
      c->sequence < s->floor || c->sequence <= s->last_sequence)
    return -EALREADY;
  if (s->last_sequence && c->sent_mono_us < s->last_sent) {
    gm7_session_clear(s);
    return -ESTALE;
  }
  uint64_t lo, hi;
  int rc = gm7_session_map(s, c->sent_mono_us, now, &lo, &hi);
  if (rc) {
    gm7_session_clear(s);
    return rc;
  }
  if (lo > rx) {
    gm7_session_clear(s);
    return -ERANGE;
  }
  uint64_t expiry;
  if (!plus(lo, c->max_age_us, &expiry) || now >= expiry)
    return -ESTALE;
  if (p->kind == GM7_WITHDRAW && s->last_sequence) {
    const struct gm7_control *current = &s->control;
    if (c->birth_us != current->birth_us || c->founder != current->founder ||
        c->term != current->term || c->active != current->active ||
        c->active_boot != current->active_boot || c->train_gen != current->train_gen ||
        (c->hot && (c->hot != current->hot || c->hot_boot != current->hot_boot)))
      return -EXDEV;
  }
  /* Fresh control is independent of timing descriptor admission. A role,
   * source, train or lineage change cannot force a second liveness handshake
   * before merge/stop-self or ordinary STATUS enters inventory. */
  if (p->kind == GM7_ANNOUNCE) {
    uint64_t source;
    if (!plus(lo, c->source_valid_us, &source) || now >= source)
      return -ESTALE;
    *source_until = source;
  } else
    *source_until = p->kind == GM7_WITHDRAW ? now : s->source_until;
  *original = lo;
  *until = expiry;
  (void)hi;
  return 0;
}
int gm7_session_control(struct gm7_session *s, const struct gm7_packet *p,
                        uint64_t rx, uint64_t now, uint64_t *original,
                        uint64_t *until) {
  uint64_t source;
  int rc = gm7_session_control_check(s, p, rx, now, original, until, &source);
  if (rc) return rc;
  s->last_sequence = p->body.control.sequence;
  s->last_sent = p->body.control.sent_mono_us;
  s->control = p->body.control;
  s->control_until = *until;
  s->source_until = source;
  return 0;
}
int gm7_session_diagnostic(struct gm7_session *s, const struct gm7_packet *p,
                           uint64_t rx, uint64_t now, uint64_t *until) {
  if (!s || !p || !until || rx > now || gm7_validate(p) ||
      (p->kind != GM7_ROLE && p->kind != GM7_GNSS) || p->group != s->group ||
      p->key_epoch != s->key_epoch)
    return -EINVAL;
  uint64_t sender, boot, sequence, sampled, birth, founder, term, active, aboot;
  uint32_t token, valid;
#define COPY_DIAG(m)                                                           \
  do {                                                                         \
    const struct gm7_##m *d = &p->body.m;                                      \
    sender = d->sender;                                                        \
    boot = d->boot;                                                            \
    sequence = d->sequence;                                                    \
    sampled = d->sampled_mono_us;                                              \
    birth = d->birth_us;                                                       \
    founder = d->founder;                                                      \
    term = d->term;                                                            \
    active = d->active;                                                        \
    aboot = d->active_boot;                                                    \
    token = d->timing_epoch;                                                   \
    valid = d->valid_us;                                                       \
  } while (0)
  if (p->kind == GM7_ROLE) {
    COPY_DIAG(role);
  } else {
    COPY_DIAG(gnss);
  }
#undef COPY_DIAG
  unsigned index = p->kind - GM7_ROLE;
  const struct gm7_bound_anchor *a = &s->descriptor;
  uint32_t source = p->kind == GM7_ROLE ? p->body.role.source_gen
                                        : p->body.gnss.source_gen,
           capture = p->kind == GM7_ROLE ? p->body.role.capture_gen
                                         : p->body.gnss.capture_gen,
           train = p->kind == GM7_ROLE ? p->body.role.train_gen
                                       : p->body.gnss.train_gen;
  if (sender != s->peer || boot != s->peer_boot ||
      sequence <= s->last_diag[index])
    return -ESTALE;
  const struct gm7_control *c = &s->control;
  bool serving = c->active == s->peer && c->active_boot == s->peer_boot;
  if (!s->last_sequence || now >= s->control_until || birth != c->birth_us ||
      founder != c->founder || term != c->term || active != c->active ||
      aboot != c->active_boot || source != c->source_gen ||
      capture != c->capture_gen || train != c->train_gen ||
      (serving ? (!gm7_session_timing_current(s) || token != a->timing_epoch)
               : token != 0))
    return -ESTALE;
  uint64_t lo, hi;
  int rc = gm7_session_map(s, sampled, now, &lo, &hi);
  if (rc)
    return rc;
  if (lo > rx || !plus(lo, valid, until) || now >= *until)
    return -ESTALE;
  s->last_diag[index] = sequence;
  (void)hi;
  return 0;
}
