/* SPDX-License-Identifier: Apache-2.0 */
#include "group_join.h"
#include <errno.h>
#include <string.h>
static size_t name_length(const char name[25]) {
  const char *end = memchr(name, 0, 25);
  return end ? (size_t)(end - name) : 25;
}
static bool valid(const struct r2_join_record *r) {
  if (!r || r->kind < 1 || r->kind > 5 || r->capabilities > 7 ||
      r->source_flags > 1 || r->control > 1 || r->demand > 4 ||
      (r->demand && ((r->demand!=2&&r->demand!=4&&r->source_flags) || r->control ||
       (r->demand==1?r->kind!=R2_JOIN_PROOF:r->kind!=R2_JOIN_ACCEPT))) ||
      (r->control && !(r->capabilities & 1)) || !r->sender || !r->receiver ||
      r->sender == r->receiver || !r->sender_boot || !r->client_nonce ||
      !r->sequence || r->created_us > INT64_MAX / 4 ||
      !r2_group_name(r->name, name_length(r->name)))
    return false;
  if (r->kind != R2_JOIN_REQUEST && (!r->receiver_boot || !r->server_nonce))
    return false;
  if (r->source_flags) {
    if ((r->kind != R2_JOIN_ACCEPT && r->kind != R2_JOIN_PROOF) || !r->root ||
        !r->root_boot || !r->source_epoch || !r->publication ||
        r->utc_us < 946684800000000ULL || r->utc_us >= 4102444800000000ULL ||
        !r->source_bound_us || r->source_bound_us > 50000 || !r->remaining_us || r->remaining_us > 1500000)
      return false;
  } else if(r->source_bound_us){return false;
  } else if(r->demand){
    bool scoped=r->root&&r->root_boot&&r->source_epoch&&r->publication;
    bool unbound=!r->root&&!r->root_boot&&!r->source_epoch&&!r->publication;
    if((!scoped&&!unbound)||(r->demand==2&&!scoped)||r->utc_us||r->remaining_us)return false;
  } else if (r->root || r->root_boot || r->source_epoch || r->publication ||
             r->utc_us || r->remaining_us)
    return false;
  return true;
}
int r2_group_join_encode(uint8_t out[R2_GROUP_JOIN_BYTES], const struct r2_join_record *r,
                         const struct r2_group_config *g) {
  if (!out || !r2_group_valid(g) || !valid(r))
    return -EINVAL;
  memset(out, 0, R2_GROUP_JOIN_BYTES);
  out[0] = 14;
  out[1] = r->kind;
  out[2] = 4;
  out[3] = r->capabilities;
  r2_group_put(out + 4, g->epoch, 4);
#define P(o, f, n) r2_group_put(out + o, r->f, n)
  P(8, sender, 8);
  P(16, receiver, 8);
  P(24, sender_boot, 8);
  P(32, receiver_boot, 8);
  P(40, client_nonce, 8);
  P(48, server_nonce, 8);
  P(56, sequence, 8);
  P(64, created_us, 8);
  P(72, echo_us, 8);
  P(80, root, 8);
  P(88, root_boot, 8);
  P(96, source_epoch, 4);
  P(100, publication, 4);
  P(104, utc_us, 8);
  P(112, remaining_us, 4);
#undef P
  out[116] = r->source_flags;
  out[117] = (uint8_t)name_length(r->name);
  memcpy(out + 118, r->name, out[117]);
  out[142] = r->control;
  out[143] = r->demand;
  r2_group_put(out + 144, r->source_bound_us, 4);
  return r2_group_mac(out + 148, g, "PTOBG4J4", out, 148);
}
int r2_group_join_decode(struct r2_join_record *r, const uint8_t *w, size_t n,
                         const struct r2_group_config *g) {
  if (!r || !w || !r2_group_valid(g) || n != R2_GROUP_JOIN_BYTES || w[0] != 14 || w[2] != 4 ||
      w[142] > 1 || w[143] > 4 || r2_group_get(w + 4, 4) != g->epoch)
    return -EINVAL;
  if (!r2_group_verify(w + 148, g, "PTOBG4J4", w, 148))
    return -EACCES;
  if (!r2_group_name((const char *)w + 118, w[117]))
    return -EINVAL;
  for (unsigned i = w[117]; i < 24; i++)
    if (w[118 + i])
      return -EINVAL;
  struct r2_join_record x = {.kind = w[1],
                             .capabilities = w[3],
                             .source_flags = w[116],
                             .control = w[142],.demand = w[143]};
#define G(o, f, n) x.f = r2_group_get(w + o, n)
  G(8, sender, 8);
  G(16, receiver, 8);
  G(24, sender_boot, 8);
  G(32, receiver_boot, 8);
  G(40, client_nonce, 8);
  G(48, server_nonce, 8);
  G(56, sequence, 8);
  G(64, created_us, 8);
  G(72, echo_us, 8);
  G(80, root, 8);
  G(88, root_boot, 8);
  G(96, source_epoch, 4);
  G(100, publication, 4);
  G(104, utc_us, 8);
  G(112, remaining_us, 4);
  G(144, source_bound_us, 4);
#undef G
  memcpy(x.name, w + 118, w[117]);
  if (!valid(&x))
    return -EINVAL;
  *r = x;
  return 0;
}
int r2_group_join_begin(struct r2_join_exchange *s, bool server, uint64_t self,
                        uint64_t peer, uint64_t boot, uint64_t nonce,
                        uint64_t now) {
  if (!s || !self || !peer || self == peer || !boot || !nonce ||
      now > INT64_MAX / 4)
    return -EINVAL;
  *s = (struct r2_join_exchange){.self = self,
                                 .peer = peer,
                                 .boot = boot,
                                 .nonce = nonce,
                                 .server = server,
                                 .until = now + R2_GROUP_JOIN_LIFETIME_US};
  return 0;
}
int r2_group_join_receive(struct r2_join_exchange *s,
                          const struct r2_join_record *r, uint64_t now) {
  if (!s || !valid(r) || now >= s->until || r->sender != s->peer ||
      r->receiver != s->self || r->sequence <= s->last_sequence)
    return -ESTALE;
  if (s->server && r->kind == R2_JOIN_REQUEST && !s->remote_nonce && !s->proved) {
    /* A request only allocates this already-bounded exchange, never membership.
     */
    s->peer_boot = r->sender_boot;
    s->peer_capabilities = r->capabilities;
    s->control = r->control;
    s->remote_nonce = r->client_nonce;
    s->request_at = r->created_us;
  } else if (!s->server && r->kind == R2_JOIN_CHALLENGE && !s->challenged && !s->accepted) {
    if ((r->control && !s->control) || r->receiver_boot != s->boot ||
        r->client_nonce != s->nonce || r->echo_us != s->request_at ||
        now < s->request_at)
      return -ESTALE;
    s->peer_boot = r->sender_boot;
    s->peer_capabilities = r->capabilities;
    s->control = r->control;
    s->remote_nonce = r->server_nonce;
    s->challenge_at = r->created_us;
    s->challenged = true;
  } else if (s->server && r->kind == R2_JOIN_PROOF && !s->proved && !s->accepted) {
    if (!s->challenge_at || r->sender_boot != s->peer_boot ||
        r->receiver_boot != s->boot || r->client_nonce != s->remote_nonce ||
        r->server_nonce != s->nonce || r->control != s->control ||
        r->echo_us != s->challenge_at || now < s->challenge_at ||
        now >= s->until)
      return -ESTALE;
    s->proof_at = r->created_us;
    s->proved = true;
    memcpy(s->name, r->name, sizeof s->name);
    s->capabilities = r->capabilities;
  } else if (!s->server && r->kind == R2_JOIN_ACCEPT && !s->accepted) {
    if (!s->challenged || !s->proof_at || r->sender_boot != s->peer_boot ||
        r->receiver_boot != s->boot || r->client_nonce != s->nonce ||
        r->server_nonce != s->remote_nonce || r->control != s->control ||
        r->echo_us != s->proof_at || now < s->proof_at ||
        now >= s->until)
      return -ESTALE;
    s->accepted = true;
    memcpy(s->name, r->name, sizeof s->name);
    s->capabilities = r->capabilities;
  } else
    return -EACCES;
  s->last_sequence = r->sequence;
  return 0;
}
bool r2_group_join_time(struct r2_join_time *out,
                        const struct r2_join_record *r, uint64_t sent,
                        uint64_t received, uint32_t bound) {
  if (!out || !valid(r) ||
      (r->kind != R2_JOIN_ACCEPT && r->kind != R2_JOIN_PROOF) ||
      !r->source_flags || received < sent || received > INT64_MAX / 4 ||
      !bound || bound > 50000)
    return false;
  if(r->source_bound_us>bound)bound=r->source_bound_us;
  uint64_t round = received - sent;
  /* Same declared2000ppm coarse-clock drift budget as anchor holdover, plus
   * two32kHz tick quantizations. This margin never becomes precision evidence. */
  uint64_t margin = (round + 499) / 500 + 64;
  uint64_t guard = round + bound + margin;
  /* The signed source sample was emitted inside this causal round trip.
   * Its age lies in[0,round], so the midpoint estimate has half-round
   * uncertainty. Permission still subtracts the FULL round trip plus the
   * independent source bound; no source lifetime is renewed by transit. */
  uint64_t uncertainty = (round + 1) / 2 + bound + margin;
  if (round >= R2_GROUP_JOIN_TTL_US || uncertainty >= 100000 ||
      r->remaining_us <= guard)
    return false;
  *out = (struct r2_join_time){.utc_us = r->utc_us + round / 2,
                               .until = received + r->remaining_us - guard,
                               .uncertainty_us = (uint32_t)uncertainty};
  return true;
}
uint64_t r2_group_anchor_deadline(uint64_t at,uint32_t uncertainty_us){
 if(uncertainty_us>=249800)return at;
 uint64_t duration=(UINT64_C(249800)-uncertainty_us)*500;
 return at>UINT64_MAX-duration?at:at+duration;
}
bool r2_group_coarse_upper(const struct r2_join_time *a,uint64_t at,uint64_t now,uint64_t *upper){
 if(!a||!upper||!a->utc_us||now<at)return false;
 uint64_t elapsed=now-at;
 if(elapsed>UINT64_C(124950000))return false;
 uint64_t guard=a->uncertainty_us+elapsed/500+100;
 if(guard>=249900||a->utc_us>INT64_MAX/1000-elapsed-guard)return false;
 *upper=a->utc_us+elapsed+guard;return true;
}
void r2_group_join_remember(struct r2_join_display cache[8],
                            const struct r2_join_exchange *s, uint64_t now) {
  if (!cache || !s || (!s->proved && !s->accepted))
    return;
  unsigned slot = 0;
  for (unsigned i = 0; i < 8; i++) {
    if (cache[i].identity == s->peer) {
      slot = i;
      break;
    }
    if (cache[i].seen < cache[slot].seen)
      slot = i;
  }
  cache[slot] = (struct r2_join_display){
      .identity = s->peer, .boot = s->peer_boot, .seen = now};
  memcpy(cache[slot].name, s->name, 25);
}
