/* SPDX-License-Identifier: Apache-2.0 */
#include "rc_dynamic.h"
#include <errno.h>
#include <string.h>
#define PUT r2_group_put
#define GET r2_group_get
static bool zero(const uint8_t *p, size_t n) {
  uint8_t bits = 0;
  while (n--)
    bits |= *p++;
  return !bits;
}
static void path_write(uint8_t p[84], const struct rc7_path *s) {
  memset(p, 0, 84);
  for (unsigned i = 0; i < 3; i++) {
    uint8_t *w = p + 28 * i;
    const struct rc7_node *n = &s->node[i];
    PUT(w, n->id, 8);
    PUT(w + 8, n->boot, 8);
    PUT(w + 16, n->train, 4);
    PUT(w + 20, n->epoch, 4);
    w[24] = n->slot;
    w[25] = n->role;
  }
}
bool rc7_path_empty(const struct rc7_path *s) {
  uint8_t p[84];
  if (!s)
    return false;
  path_write(p, s);
  return !s->source_epoch && !s->depth && zero(p, 84);
}
bool rc7_path_valid(const struct rc7_path *s) {
  if (!s || !s->source_epoch || s->depth > 2)
    return false;
  for (unsigned i = 0; i < 3; i++) {
    const struct rc7_node *n = &s->node[i];
    if (i > s->depth) {
      if (n->id || n->boot || n->train || n->epoch || n->slot || n->role)
        return false;
      continue;
    }
    if (!n->id || !n->boot || !n->train || !n->epoch ||
        n->role != (i ? 1 : 0) || (!i && n->slot) ||
        (i && (!n->slot || n->slot > 3 || n->slot <= s->node[i - 1].slot)))
      return false;
    for (unsigned j = 0; j < i; j++)
      if (n->id == s->node[j].id)
        return false;
  }
  return true;
}
bool rc7_path_same(const struct rc7_path *a, const struct rc7_path *b) {
  uint8_t x[84], y[84];
  if (!a || !b || a->source_epoch != b->source_epoch || a->depth != b->depth)
    return false;
  path_write(x, a);
  path_write(y, b);
  return !memcmp(x, y, 84);
}
int rc7_path_mac(uint8_t out[16], const struct rc7_path *s,
                 const struct r2_group_config *g) {
  uint8_t body[89];
  if (!rc7_path_valid(s))
    return -EINVAL;
  PUT(body, s->source_epoch, 4);
  body[4] = s->depth;
  path_write(body + 5, s);
  return r2_group_mac(out, g, "RC7PATHS", body, sizeof body);
}
static bool origin_valid(const struct rc5_origin *o) {
  return o && o->root && o->boot && o->epoch && o->expires > o->issued &&
         o->expires - o->issued <= RC_DYNAMIC_SOURCE_NS &&
         o->frame <= o->issued && (!o->hold_us) == (!o->hold_ns) &&
         o->hold_us <= 10000000 && o->hold_ns != UINT32_MAX;
}
static void origin_write(uint8_t *p, const struct rc5_origin *o) {
  PUT(p, o->root, 8);
  PUT(p + 8, o->boot, 8);
  PUT(p + 16, o->epoch, 4);
  PUT(p + 20, o->issued, 8);
  PUT(p + 28, o->expires, 8);
  PUT(p + 36, (uint64_t)o->utc_offset, 8);
  PUT(p + 44, o->frame, 8);
  PUT(p + 52, o->hold_us, 4);
  PUT(p + 56, o->hold_ns, 4);
}
static void origin_read(struct rc5_origin *o, const uint8_t *p) {
  *o = (struct rc5_origin){.root = GET(p, 8),
                           .boot = GET(p + 8, 8),
                           .epoch = GET(p + 16, 4),
                           .issued = GET(p + 20, 8),
                           .expires = GET(p + 28, 8),
                           .utc_offset = (int64_t)GET(p + 36, 8),
                           .frame = GET(p + 44, 8),
                           .hold_us = GET(p + 52, 4),
                           .hold_ns = GET(p + 56, 4)};
}
int rc7_origin_sign(uint8_t out[16], const struct rc5_origin *o,
                    const struct r2_group_config *g) {
  uint8_t body[60];
  if (!origin_valid(o))
    return -EINVAL;
  origin_write(body, o);
  return r2_group_mac(out, g, "RC7ORIGN", body, 60);
}
static bool control_valid(const struct rc7_control *c) {
  if (!c || !c->sender || !c->sender_boot || c->type < 1 || c->type > 3 ||
      c->operation > 1 || (c->operation && c->type != RC7_REQUEST) ||
      (c->ready && c->type != RC7_RESPONSE) ||
      (c->publishing && c->type != RC7_CAPABILITY))
    return false;
  bool path = rc7_path_valid(&c->path);
  if (!path && !rc7_path_empty(&c->path))
    return false;
  if (c->type == RC7_CAPABILITY) {
    if (c->target || c->target_boot || c->challenge || c->nonce ||
        c->sequence || c->lease_us || !c->next_challenge)
      return false;
  } else if (!c->target || c->target == c->sender || !c->target_boot ||
             !c->challenge || !c->nonce || !c->sequence || !c->lease_us ||
             c->lease_us > RC_DYNAMIC_LEASE_US ||
             (c->type == RC7_REQUEST ? c->next_challenge != 0
                                     : c->next_challenge == 0))
    return false;
  if (path) {
    const struct rc7_node *n = &c->path.node[c->path.depth];
    uint64_t endpoint = c->type == RC7_REQUEST ? c->target : c->sender,
             boot = c->type == RC7_REQUEST ? c->target_boot : c->sender_boot;
    if (n->id != endpoint || n->boot != boot)
      return false;
    if (c->type != RC7_CAPABILITY)
      for (unsigned i = 0; i <= c->path.depth; i++)
        if (c->path.node[i].id ==
            (c->type == RC7_REQUEST ? c->sender : c->target))
          return false;
  }
  if (c->publishing && !path)
    return false;
  if (c->ready) {
    if (!path || !c->publication_floor || c->root_hi_ns < c->root_lo_ns ||
        c->root_hi_ns - c->root_lo_ns > 1200000000 ||
        c->source_expires_ns <= c->root_hi_ns ||
        c->source_expires_ns - c->root_hi_ns > RC_DYNAMIC_SOURCE_NS)
      return false;
  } else if (c->publication_floor || c->root_lo_ns || c->root_hi_ns ||
             c->source_expires_ns)
    return false;
  return true;
}
int rc7_control_encode(uint8_t ad[212], const struct rc7_control *c,
                       const struct r2_group_config *g) {
  if (!ad || !r2_group_valid(g) || !control_valid(c))
    return -EINVAL;
  memset(ad, 0, 212);
  ad[0] = 211;
  ad[1] = ad[2] = ad[3] = 255;
  ad[4] = 'R';
  ad[5] = 'Q';
  ad[6] = 7;
  ad[7] = c->type;
  ad[8] = 5;
  ad[9] = c->ready | (c->publishing << 1);
  ad[10] = c->path.depth;
  ad[11] = c->operation;
  PUT(ad + 12, g->epoch, 4);
  PUT(ad + 16, c->sender, 8);
  PUT(ad + 24, c->target, 8);
  PUT(ad + 32, c->sender_boot, 8);
  PUT(ad + 40, c->target_boot, 8);
  PUT(ad + 48, c->challenge, 8);
  PUT(ad + 56, c->nonce, 8);
  PUT(ad + 64, c->sequence, 4);
  PUT(ad + 68, c->lease_us, 4);
  PUT(ad + 72, c->next_challenge, 8);
  PUT(ad + 80, c->path.source_epoch, 4);
  PUT(ad + 84, c->publication_floor, 4);
  PUT(ad + 88, c->root_lo_ns, 8);
  PUT(ad + 96, c->root_hi_ns, 8);
  PUT(ad + 104, c->source_expires_ns, 8);
  path_write(ad + 112, &c->path);
  return r2_group_mac(ad + 196, g, "RC7LOCAL", ad, 196);
}
int rc7_control_decode(struct rc7_control *c, const uint8_t *ad, size_t n,
                       const struct r2_group_config *g) {
  if (!c || !ad || !r2_group_valid(g) || n != 212 || ad[0] != 211 ||
      ad[1] != 255 || ad[2] != 255 || ad[3] != 255 || ad[4] != 'R' ||
      ad[5] != 'Q' || ad[6] != 7 || ad[8] != 5 || (ad[9] & ~3u) ||
      GET(ad + 12, 4) != g->epoch)
    return -EINVAL;
  if (!r2_group_verify(ad + 196, g, "RC7LOCAL", ad, 196))
    return -EACCES;
  *c = (struct rc7_control){.type = ad[7],
                            .ready = ad[9] & 1,
                            .publishing = ad[9] & 2,
                            .operation = ad[11],
                            .sender = GET(ad + 16, 8),
                            .target = GET(ad + 24, 8),
                            .sender_boot = GET(ad + 32, 8),
                            .target_boot = GET(ad + 40, 8),
                            .challenge = GET(ad + 48, 8),
                            .nonce = GET(ad + 56, 8),
                            .sequence = GET(ad + 64, 4),
                            .lease_us = GET(ad + 68, 4),
                            .next_challenge = GET(ad + 72, 8),
                            .publication_floor = GET(ad + 84, 4),
                            .root_lo_ns = GET(ad + 88, 8),
                            .root_hi_ns = GET(ad + 96, 8),
                            .source_expires_ns = GET(ad + 104, 8)};
  c->path.depth = ad[10];
  c->path.source_epoch = GET(ad + 80, 4);
  for (unsigned i = 0; i < 3; i++) {
    const uint8_t *p = ad + 112 + 28 * i;
    if (p[26] || p[27])
      return -EINVAL;
    c->path.node[i] = (struct rc7_node){.id = GET(p, 8),
                                        .boot = GET(p + 8, 8),
                                        .train = GET(p + 16, 4),
                                        .epoch = GET(p + 20, 4),
                                        .slot = p[24],
                                        .role = p[25]};
  }
  return control_valid(c) ? 0 : -EINVAL;
}
static const unsigned cap_offset[4] = {0, 32, 72, 112};
static const unsigned cap_length[4] = {24, 8, 12, 100};
int rc7_capability_encode(uint8_t out[144], const struct rc7_control *c,
                          const struct r2_group_config *g) {
  uint8_t canonical[212];
  if (!out || !c || c->type != RC7_CAPABILITY ||
      rc7_control_encode(canonical, c, g))
    return -EINVAL;
  if (!zero(canonical + 24, 8) || !zero(canonical + 40, 32) ||
      !zero(canonical + 84, 28))
    return -EINVAL;
  unsigned at = 0;
  for (unsigned i = 0; i < 4; i++) {
    memcpy(out + at, canonical + cap_offset[i], cap_length[i]);
    at += cap_length[i];
  }
  return 0;
}
int rc7_capability_decode(struct rc7_control *c, const uint8_t *wire, size_t n,
                          const struct r2_group_config *g) {
  uint8_t canonical[212] = {0};
  if (!c || !wire || n != 144 || wire[0] != 211 || wire[1] != 255 ||
      wire[2] != 255 || wire[3] != 255 || wire[4] != 'R' || wire[5] != 'Q' ||
      wire[6] != 7 || wire[7] != RC7_CAPABILITY || wire[8] != 5 ||
      (wire[9] & ~2u) || wire[11])
    return -EINVAL;
  unsigned at = 0;
  for (unsigned i = 0; i < 4; i++) {
    memcpy(canonical + cap_offset[i], wire + at, cap_length[i]);
    at += cap_length[i];
  }
  return rc7_control_decode(c, canonical, sizeof canonical, g);
}
static bool packet_valid(const struct rc7_packet *p,
                         const struct rc7_path *path) {
  if (!p || !rc7_path_valid(path) || !origin_valid(&p->origin) ||
      p->count > 2 || !p->publication)
    return false;
  const struct rc7_node *n = &path->node[path->depth], *root = &path->node[0];
  if (p->origin.root != root->id || p->origin.boot != root->boot ||
      p->origin.epoch != path->source_epoch || p->sender != n->id ||
      p->boot != n->boot || p->train != n->train || p->epoch != n->epoch ||
      p->depth != path->depth || p->slot != n->slot)
    return false;
  for (unsigned i = 0; i < p->count; i++) {
    const struct rc5_correction *c = &p->correction[i];
    if (!c->publication || c->publication >= p->publication ||
        c->channel > 36 || !c->generation)
      return false;
    if (i && c->publication == p->correction[0].publication &&
        c->event == p->correction[0].event &&
        c->channel == p->correction[0].channel)
      return false;
  }
  return true;
}
int rc7_encode(uint8_t ad[190], const struct rc7_packet *p,
               const struct rc7_path *path, const struct r2_group_config *g) {
  uint8_t proof[16], body[190];
  if (!ad || !packet_valid(p, path) || rc7_origin_sign(proof, &p->origin, g) ||
      memcmp(proof, p->proof, 16) || rc7_path_mac(body, path, g))
    return -EINVAL;
  memset(ad, 0, 190);
  ad[0] = 189;
  ad[1] = ad[2] = ad[3] = 255;
  ad[4] = 'R';
  ad[5] = 'C';
  ad[6] = 7;
  ad[7] = p->count;
  PUT(ad + 8, g->epoch, 4);
  origin_write(ad + 12, &p->origin);
  memcpy(ad + 72, p->proof, 16);
  PUT(ad + 88, p->sender, 8);
  ad[96] = p->depth;
  ad[97] = p->slot;
  PUT(ad + 98, p->boot, 8);
  PUT(ad + 106, p->train, 4);
  PUT(ad + 110, p->publication, 4);
  PUT(ad + 114, p->epoch, 4);
  for (unsigned i = 0; i < p->count; i++) {
    uint8_t *w = ad + 118 + 27 * i;
    const struct rc5_correction *c = &p->correction[i];
    PUT(w, c->publication, 4);
    PUT(w + 4, c->event, 2);
    w[6] = c->channel;
    PUT(w + 7, c->generation, 4);
    PUT(w + 11, c->age_us, 4);
    PUT(w + 15, c->tx_ns, 8);
    PUT(w + 23, c->bound, 4);
  }
  ad[172] = 5;
  memcpy(body + 16, ad, 174);
  return r2_group_mac(ad + 174, g, "RC7PACKT", body, 190);
}
int rc7_decode(struct rc7_packet *p, const uint8_t *ad, size_t n,
               const struct rc7_path *path, const struct r2_group_config *g) {
  uint8_t body[190], proof[16];
  if (!p || !ad || !r2_group_valid(g) || n != 190 || ad[0] != 189 ||
      ad[1] != 255 || ad[2] != 255 || ad[3] != 255 || ad[4] != 'R' ||
      ad[5] != 'C' || ad[6] != 7 || ad[7] > 2 || GET(ad + 8, 4) != g->epoch ||
      ad[172] != 5 || ad[173] || rc7_path_mac(body, path, g))
    return -EINVAL;
  memcpy(body + 16, ad, 174);
  if (!r2_group_verify(ad + 174, g, "RC7PACKT", body, 190))
    return -EACCES;
  memset(p, 0, sizeof *p);
  origin_read(&p->origin, ad + 12);
  memcpy(p->proof, ad + 72, 16);
  p->count = ad[7];
  p->sender = GET(ad + 88, 8);
  p->depth = ad[96];
  p->slot = ad[97];
  p->boot = GET(ad + 98, 8);
  p->train = GET(ad + 106, 4);
  p->publication = GET(ad + 110, 4);
  p->epoch = GET(ad + 114, 4);
  if (rc7_origin_sign(proof, &p->origin, g) || memcmp(proof, p->proof, 16))
    return -EACCES;
  for (unsigned i = 0; i < 2; i++) {
    const uint8_t *w = ad + 118 + 27 * i;
    if (i >= p->count) {
      if (!zero(w, 27))
        return -EINVAL;
      continue;
    }
    p->correction[i] = (struct rc5_correction){.publication = GET(w, 4),
                                               .event = GET(w + 4, 2),
                                               .channel = w[6],
                                               .generation = GET(w + 7, 4),
                                               .age_us = GET(w + 11, 4),
                                               .tx_ns = GET(w + 15, 8),
                                               .bound = GET(w + 23, 4)};
  }
  return packet_valid(p, path) ? 0 : -EINVAL;
}
bool rc7_bind(struct rc7_receiver *s, const struct rc7_path *path,
              uint64_t self, bool tx, uint8_t slot,
              const struct rc5_admission *a) {
  if (!s || !a || !self || !rc7_path_valid(path) ||
      path->depth + (tx ? 1 : 0) > 2)
    return false;
  struct rc5_routes routes = {.count = path->depth + 2};
  for (unsigned i = 0; i <= path->depth; i++) {
    const struct rc7_node *p = &path->node[i];
    if (p->id == self)
      return false;
    routes.node[i] = (struct rc5_node){.id = p->id,
                                       .parent = i ? i - 1 : 0,
                                       .slot = p->slot,
                                       .depth = i,
                                       .transmitter = true};
  }
  unsigned own = routes.count - 1;
  routes.node[own] = (struct rc5_node){.id = self,
                                       .parent = own - 1,
                                       .slot = tx ? slot : 255,
                                       .depth = path->depth + (tx ? 1 : 0),
                                       .transmitter = tx};
  if (!rc5_routes_valid(&routes))
    return false;
  const struct rc7_node *p = &path->node[path->depth], *r = &path->node[0];
  if (a->parent_boot != p->boot || a->parent_train != p->train ||
      a->parent_epoch != p->epoch || a->root_boot != r->boot ||
      a->root_epoch != path->source_epoch)
    return false;
  bool same = s->clock.bound && s->self == self && s->transmitter == tx &&
              rc7_path_same(&s->path, path);
  if (same && a->publication_floor < s->clock.admission.publication_floor)
    return false;
  uint32_t seen_publication = s->clock.publication_high;
  uint16_t seen_event = s->clock.event_high;
  bool event_seen = s->clock.event_seen;
  if (!same)
    rc5_init(&s->clock, &routes, own);
  if (!rc5_bind(&s->clock, a))
    return false;
  if (same) {
    /* A renewed floor fences new originals. It must not pretend that a
     * higher publication was already received and suppress valid follow-ups
     * for originals saved before the renewal. */
    s->clock.publication_high = seen_publication;
    s->clock.event_high = seen_event;
    s->clock.event_seen = event_seen;
  }
  s->clock.routes = routes;
  s->path = *path;
  s->self = self;
  s->transmitter = tx;
  return true;
}
static bool fresh(const struct rc5_state *s, uint64_t tick, uint64_t ns,
                  uint64_t expires) {
  uint64_t lo, hi;
  if (!rc5_coarse_interval(s, tick, &lo, &hi) || hi > UINT64_MAX - 100000)
    return false;
  return ns >= (lo > 100000 ? lo - 100000 : 0) && ns <= hi + 100000 &&
         expires > hi;
}
bool rc7_receive(struct rc7_receiver *s, const struct rc7_packet *p,
                 uint64_t rx, uint16_t event, uint8_t channel, uint64_t now) {
  struct rc5_state *c = s ? &s->clock : NULL;
  if (!c || !c->bound || !packet_valid(p, &s->path) ||
      now >= c->context_until || rx > now || now - rx >= 8000000 ||
      channel > 36 || p->publication < c->publication_high) {
    if (c)
      ++c->rejected;
    return false;
  }
  bool accepted = false;
  for (unsigned i = 0; i < p->count; i++)
    for (unsigned j = 0; j < RC5_HISTORY; j++) {
      struct rc5_pending *q = &c->pending[j];
      const struct rc5_correction *k = &p->correction[i];
      if (!q->used || q->packet.publication != k->publication ||
          q->event != k->event || q->channel != k->channel)
        continue;
      if (now < q->rx || now - q->rx >= 8000000 ||
          !fresh(c, q->rx, k->tx_ns, q->packet.origin.expires)) {
        q->used = false;
        ++c->rejected;
        continue;
      }
      uint64_t lo, hi;
      if (!rc5_coarse_interval(c, now, &lo, &hi) ||
          hi >= q->packet.origin.expires) {
        q->used = false;
        ++c->rejected;
        continue;
      }
      if ((!c->reference.valid || q->rx > c->reference.rx) &&
          k->tx_ns >= c->last_parent_ns) {
        c->reference =
            (struct rc5_reference){.origin = q->packet.origin,
                                   .rx = q->rx,
                                   .completed_at = now,
                                   .parent_tx_ns = k->tx_ns,
                                   .generation = k->generation,
                                   .age_us = k->age_us,
                                   .bound = k->bound,
                                   .valid = true,
                                   .original_available = q->available,
                                   .followup_rx = rx,
                                   .publication = k->publication,
                                   .event = k->event,
                                   .channel = k->channel};
        memcpy(c->reference.proof, q->packet.proof, 16);
        c->last_parent_ns = k->tx_ns;
        ++c->accepted;
        accepted = true;
      }
      q->used = false;
    }
  /* A renewal floor governs new originals, never previously admitted captures.
   */
  if (p->publication <= c->admission.publication_floor)
    return accepted;
  for (unsigned j = 0; j < RC5_HISTORY; j++) {
    struct rc5_pending *q = &c->pending[j];
    if (q->packet.publication == p->publication && q->event == event &&
        q->channel == channel)
      return accepted;
  }
  if (p->publication == c->publication_high && c->event_seen &&
      (uint16_t)(event - c->event_high) >= 32768) {
    ++c->rejected;
    return accepted;
  }
  c->publication_high = p->publication;
  c->event_high = event;
  c->event_seen = true;
  struct rc5_pending *q = &c->pending[c->head++ % RC5_HISTORY];
  if (q->used)
    ++c->evicted;
  *q = (struct rc5_pending){
      .packet = {.origin = p->origin, .publication = p->publication},
      .rx = rx,
      .available = now,
      .event = event,
      .channel = channel,
      .used = true};
  memcpy(q->packet.proof, p->proof, 16);
  return accepted;
}
