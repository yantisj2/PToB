/* SPDX-License-Identifier: Apache-2.0
 * Portable association and causal delivered-map owner. No callback timestamps,
 * radio scheduling, heap, crypto or device writes. */
#include "radio_precision.h"
#include "rc_direct_calibration.h"
#include <limits.h>
static bool live(const struct r2_radio_precision *s, uint64_t now) {
  return s->scope.boot && s->scope.source_epoch && now < s->scope.until_us &&
         (s->scope.gm || s->scope.follower);
}
static bool captured(const struct r2_radio_precision *s, struct r2_capture c) {
  return c.kind == R2_CAPTURE_RADIO && c.epoch && c.epoch == s->local_epoch &&
         c.tick;
}
static void association_clear(struct r2_radio_precision *s) {
  memset(s->original, 0, sizeof s->original);
  memset(s->correction, 0, sizeof s->correction);
  memset(s->sent, 0, sizeof s->sent);
  s->sequence = 0;
  s->data_until = 0;
  s->last_available = 0;
  s->last_accepted = 0;
  s->permission_local=s->permission_ns=s->permission_created=s->permission_expires=0;
  s->permission_offset=0;s->original_progress=s->permission_publication=0;s->permission_anchor=false;
  s->sent_count = s->beat_count = s->beat_head = 0;
  s->tx_seen = false;
}
static void history_clear(struct r2_radio_precision *s) {
  association_clear(s);
  memset(&s->servo, 0, sizeof s->servo);
}
bool r2_radio_precision_rebind_train(struct r2_radio_precision *s,
                                     uint32_t train,uint32_t capture_epoch){
  if(!s->scope.follower||s->scope.gm||!s->train||train<=s->train||
     !capture_epoch||capture_epoch!=s->remote_capture||s->servo.fault||
     !s->servo.fit_valid||s->servo.session!=s->train)return false;
  /* Caller has authenticated unchanged root/source and parent clock domains.
   * Transport tuples and exposed availability retire before accepting any new
   * correction; the causal fitter still checks the next independent sample. */
  association_clear(s);
  s->train=train;s->scope.publication_epoch=train;
  s->servo.session=train;s->servo.last_sequence=0;
  return true;
}
void r2_radio_precision_scope(struct r2_radio_precision *s,
                              struct r2_radio_scope p, uint64_t epoch) {
  bool change = s->scope.boot != p.boot ||
                s->scope.source_epoch != p.source_epoch || s->scope.local_source_epoch != p.local_source_epoch || s->scope.publication_epoch != p.publication_epoch ||
                ((s->scope.root_identity&&p.root_identity)?s->scope.root_identity!=p.root_identity:s->scope.root!=p.root) || s->scope.gm != p.gm ||
                s->scope.follower != p.follower || s->scope.broadcast != p.broadcast || s->local_epoch != epoch;
  if (change) {
    history_clear(s);
    s->train = s->remote_capture = 0;
  }
  s->scope = p;
  s->local_epoch = epoch;
}
static bool tuple(const struct r2_radio_original *o,
                  const struct r2_precise_correction *c) {
  return o->publication == c->publication && o->event == c->event &&
         o->channel == c->channel;
}
static bool stale(uint64_t now, uint64_t then) {
  return now < then || now - then > R2_RADIO_MAX_AGE_TICKS;
}
static bool reject(struct r2_radio_precision *s) {
  r2_math_inc(&s->rejected);
  return false;
}
/* A fresh signed source grant is independent of a new physical correction.
 * Its coordinate conversion is anchored to an actually accepted pair, never
 * header UTC or a callback time. Reuse the public-client2kppm+100us guard.
 * Clock/model reads retain a hard1.5s cap from the last accepted pair. */
static void source_permission(struct r2_radio_precision *s,
 const struct r2_precision_packet *p,struct r2_capture available,uint64_t horizon){
 bool progressing=p->publication&&p->publication>s->original_progress;
 if(progressing)s->original_progress=p->publication;
 if(!progressing||!s->scope.broadcast||horizon!=1650000000||!s->permission_anchor||
  p->utc_offset_ns<0||p->utc_offset_ns!=s->permission_offset||
  p->created_ns<s->permission_created)return;
 bool restriction=p->expires_ns<s->permission_expires;
 uint64_t prior_until=s->data_until;
 if(restriction){
  /* Authenticated revocation cannot wait for a usable physical model.
   * Keep its publication barrier so an older accepted pair cannot revive it. */
  s->data_until=0;s->permission_created=p->created_ns;
  s->permission_expires=p->expires_ns;s->permission_publication=p->publication;
 }
 if(s->servo.fault||!s->servo.fit_valid||!s->last_accepted||
  available.tick<s->last_accepted||available.tick-s->last_accepted>=R2_CAPTURE_HZ*3/2||
  available.tick<s->permission_local)return;
 r2_servo_map_t map;if(!r2_radio_precision_map(s,&map))return;
 uint64_t elapsed=available.tick-s->permission_local;
 if(elapsed>R2_RADIO_MAX_AGE_TICKS+R2_CAPTURE_HZ*3/2)return;
 uint64_t ns=elapsed*125/2;
 if(s->permission_ns>UINT64_MAX-ns)return;
 uint64_t current=s->permission_ns+ns,guard=ns/500+100000;
 if(current>UINT64_MAX-guard||current<guard||p->created_ns>current-guard||
  p->expires_ns<=current+guard||p->expires_ns<=s->permission_ns||
  p->expires_ns-s->permission_ns>horizon+1750000000)return;
 uint64_t duration=(p->expires_ns-s->permission_ns)/1000;
 uint64_t reserve=duration/500+100;
 if(duration<=reserve)return;
 duration-=reserve;
 if(duration>(UINT64_MAX-s->permission_local)/(R2_CAPTURE_HZ/1000000))return;
 uint64_t until=s->permission_local+duration*(R2_CAPTURE_HZ/1000000);
 if(until<=available.tick)return;
 if(restriction)s->data_until=until<prior_until?until:prior_until;
 else if(p->expires_ns>s->permission_expires){
  /* Immutable source origins may predate this packet. Progression and the
   * original absolute expiry, converted with the accepted-pair guard, bound
   * authority; arrival cannot manufacture a fresh source lifetime. */
  if(until>s->data_until){s->data_until=until;r2_math_inc(&s->permission_updates);}
 }
 s->permission_created=p->created_ns;s->permission_expires=p->expires_ns;
 s->permission_publication=p->publication;
}
static bool receive(struct r2_radio_precision *s,
                           const struct r2_precision_packet *p,
                           struct r2_capture rx, uint16_t event,
                           uint8_t channel, struct r2_capture available,
                           uint64_t now, int64_t expected,bool immutable,uint64_t horizon_ns) {
  if (!p || !live(s, now) || !s->scope.follower || !captured(s, rx) ||
      available.kind != R2_CAPTURE_NOW || available.epoch != rx.epoch ||
      stale(available.tick, rx.tick) || p->root != s->scope.root ||
      p->boot != s->scope.boot || p->source_epoch != s->scope.source_epoch ||
      (!p->train || p->train != s->scope.publication_epoch) || !p->capture_epoch || p->flags != 1 || p->count > 2 ||
      channel > 36 || expected < 0)
    return reject(s);
  /* Header creation's coarse UTC binds freshness independently of learned fit.
   */
  if (p->created_ns > INT64_MAX || p->utc_offset_ns == INT64_MIN ||
      (p->utc_offset_ns > 0 &&
       p->created_ns > (uint64_t)(INT64_MAX - p->utc_offset_ns)) ||
      (p->utc_offset_ns < 0 && p->created_ns < (uint64_t)-p->utc_offset_ns))
    return reject(s);
  int64_t creation = (int64_t)p->created_ns + p->utc_offset_ns;
  uint64_t distance = creation > expected ? (uint64_t)(creation - expected)
                                          : (uint64_t)(expected - creation);
  if ((!immutable&&distance>=250000000) ||
      (immutable&&creation>expected&&distance>=250000000)||p->expires_ns<=p->created_ns||
      p->expires_ns - p->created_ns > horizon_ns)
    return reject(s);
  if(immutable){
    if(p->expires_ns>INT64_MAX||
       (p->utc_offset_ns>0&&p->expires_ns>(uint64_t)(INT64_MAX-p->utc_offset_ns))||
       (p->utc_offset_ns<0&&p->expires_ns<(uint64_t)-p->utc_offset_ns))return reject(s);
    if((int64_t)p->expires_ns+p->utc_offset_ns<=expected)return reject(s);
  }
  if (s->train && p->train < s->train)
    return reject(s);
  if (s->train != p->train || s->remote_capture != p->capture_epoch) {
    /* Within one train a changed capture epoch is invalid; transmitter must
     * retire/recreate the train to advertise a new hardware incarnation. */
    if (s->train == p->train && s->remote_capture)
      return reject(s);
    history_clear(s);
    s->train = p->train;
    s->remote_capture = p->capture_epoch;
  }
  /* An offset change needs a new authenticated context, not a silent rebase
   * of an existing model or a way around a tighter permission barrier. */
  if(s->scope.broadcast&&s->permission_anchor&&
     p->utc_offset_ns!=s->permission_offset)return reject(s);
  unsigned slot = R2_RADIO_PENDING;
  for (unsigned i = 0; i < R2_RADIO_PENDING; i++) {
    struct r2_radio_original *o = &s->original[i];
    if (o->used && !stale(available.tick, o->local) &&
        o->publication == p->publication && o->event == event &&
        o->channel == channel) {
      r2_math_inc(&s->duplicates);
      return false;
    }
    if (!o->used || stale(available.tick, o->local))
      slot = i;
  }
  if (slot == R2_RADIO_PENDING) {
    r2_math_inc(&s->overflows);
    history_clear(s);
    return false;
  }
  s->original[slot] = (struct r2_radio_original){.local=rx.tick,.created_ns=p->created_ns,
      .expires_ns=p->expires_ns,.utc_offset_ns=p->utc_offset_ns,.publication=p->publication,
      .event=event,.channel=channel,.used=true};
  for (unsigned c = 0; c < p->count; c++) {
    const struct r2_precise_correction *value = &p->correction[c];
    unsigned free = R2_RADIO_PENDING;
    bool duplicate = false;
    for (unsigned i = 0; i < R2_RADIO_PENDING; i++) {
      struct r2_radio_correction *q = &s->correction[i];
      if (q->used && !stale(available.tick, q->arrived) &&
          q->value.publication == value->publication &&
          q->value.event == value->event &&
          q->value.channel == value->channel) {
        duplicate = true;
        break;
      }
      if (!q->used || stale(available.tick, q->arrived))
        free = i;
    }
    if (duplicate)
      continue;
    if (free == R2_RADIO_PENDING) {
      r2_math_inc(&s->overflows);
      history_clear(s);
      return false;
    }
    s->correction[free] =
        (struct r2_radio_correction){*value, available.tick, true};
  }
  source_permission(s,p,available,horizon_ns);
  return true;
}
bool r2_radio_precision_rx(struct r2_radio_precision *s,const struct r2_precision_packet *p,
 struct r2_capture rx,uint16_t event,uint8_t channel,struct r2_capture available,
 uint64_t now,int64_t expected){return receive(s,p,rx,event,channel,available,now,expected,false,1500000000);}
/* V14 permission margin is not a longer local source or delivered-map life. */
bool r2_radio_precision_rx_serving_origin(struct r2_radio_precision *s,const struct r2_precision_packet *p,
 struct r2_capture rx,uint16_t event,uint8_t channel,struct r2_capture available,
 uint64_t now,int64_t expected){return receive(s,p,rx,event,channel,available,now,expected,true,1650000000);}
bool r2_radio_precision_rx_immutable_origin(struct r2_radio_precision *s,const struct r2_precision_packet *p,
 struct r2_capture rx,uint16_t event,uint8_t channel,struct r2_capture available,
 uint64_t now,int64_t expected){return receive(s,p,rx,event,channel,available,now,expected,true,1500000000);}
bool r2_radio_precision_step(struct r2_radio_precision *s,
                             struct r2_capture available, uint64_t now) {
  if (!live(s, now) || !s->scope.follower || available.kind != R2_CAPTURE_NOW ||
      available.epoch != s->local_epoch || available.tick <= s->last_available)
    return false;
  /* A correction arrives after later RF originals. Only an original after
   * the previous delivered-map boundary can be scored causally. Thinning is
   * by availability, never by residual; do not post-fit those earlier events.
   */
  struct r2_radio_original *best = NULL;
  struct r2_radio_correction *correction = NULL;
  for (unsigned i = 0; i < R2_RADIO_PENDING; i++) {
    struct r2_radio_original *o = &s->original[i];
    if (!o->used || stale(available.tick, o->local) ||
        o->local <= s->servo.last_available)
      continue;
    for (unsigned j = 0; j < R2_RADIO_PENDING; j++) {
      struct r2_radio_correction *c = &s->correction[j];
      if (c->used && !stale(available.tick, c->arrived) &&
          tuple(o, &c->value) && (!best || o->local < best->local)) {
        best = o;
        correction = c;
      }
    }
  }
  if (!best)
    return false;
  struct r2_precise_correction c = correction->value;
  correction->used = false;
  if (c.ticks > INT64_MAX / 125 || !c.generation ||
      c.expires_ns > best->expires_ns || c.ticks * 125 / 2 < best->created_ns ||
      c.ticks * 125 / 2 >= c.expires_ns || s->sequence == UINT32_MAX)
    return reject(s);
  uint64_t data_until=0;
  if(s->scope.broadcast){
    uint64_t duration=(c.expires_ns-c.ticks*125/2)/1000*(R2_CAPTURE_HZ/1000000);
    if(best->local>UINT64_MAX-duration || available.tick>=best->local+duration)
      return reject(s);
    data_until=best->local+duration;
  }
  r2_servo_sample_t sample = {.session = s->train,
                              .sequence = ++s->sequence,
                              .local = best->local,
                              .remote = c.ticks,
                              .available = available.tick,
                              .peer_pps_below200 = false};
  /* No physical UTC/PPS qualification is inferred from relative disciplining.
   */
  bool accepted = r2_servo_observe_permitted(&s->servo, &sample, true, true);
  s->last_available = available.tick;
  if (accepted) {
    s->last_accepted = available.tick;
    bool same_permission=s->permission_anchor&&s->permission_offset==best->utc_offset_ns;
    bool current_grant=best->created_ns==s->permission_created&&c.expires_ns==s->permission_expires;
    bool newer_grant=best->publication>=s->permission_publication&&
      best->created_ns>=s->permission_created&&c.expires_ns>=s->permission_expires;
    if(!same_permission||((current_grant||newer_grant)&&data_until>s->data_until))s->data_until=data_until;
    if(!same_permission){
      s->permission_created=best->created_ns;s->permission_expires=c.expires_ns;s->permission_publication=best->publication;
    }else{
      if(best->publication>=s->permission_publication&&best->created_ns>=s->permission_created){
        s->permission_publication=best->publication;
        s->permission_created=best->created_ns;
        if(c.expires_ns>s->permission_expires)s->permission_expires=c.expires_ns;
      }
    }
    s->permission_local=best->local;s->permission_ns=c.ticks*125/2;
    s->permission_offset=best->utc_offset_ns;s->permission_anchor=true;
#ifdef R2_LORA
    s->origin_created_ns=best->created_ns;
    s->origin_expires_ns=c.expires_ns; /* minimum original/correction permission */
#endif
    r2_math_inc(&s->accepted);
  }
  else
    r2_math_inc(&s->rejected);
  return accepted;
}
bool r2_radio_precision_tx(struct r2_radio_precision *s,
                           const struct r2_precision_packet *p,
                           struct r2_capture tx, uint16_t event,
                           uint8_t channel, uint64_t ns, uint64_t generation,
                           uint64_t expires, uint64_t now) {
  if (!p || !live(s, now) || !s->scope.gm || !captured(s, tx) ||
      p->root != s->scope.root || p->boot != s->scope.boot ||
      p->source_epoch != s->scope.source_epoch || (!p->train || p->train != s->scope.publication_epoch) ||
      p->capture_epoch != tx.epoch || !generation || channel > 36) {
    r2_math_inc(&s->tx_reject[0]);return reject(s);
  }
  if (ns < p->created_ns || ns >= p->expires_ns || ns >= expires ||
      ns > UINT64_MAX / 2) {
    r2_math_inc(&s->tx_reject[1]);return reject(s);
  }
  if (s->train != p->train) {
    history_clear(s);
    s->train = p->train;
    s->remote_capture = p->capture_epoch;
  }
  if (s->tx_seen) {
    uint16_t events = (uint16_t)(event - s->last_tx_event);
    if (!events || events >= 0x8000 || ns <= s->last_tx_ns) {
      r2_math_inc(&s->tx_reject[2]);return reject(s);
    }
    uint64_t elapsed = ns - s->last_tx_ns,
             expected = (uint64_t)events * R2_PRECISION_INTERVAL_NS;
    int64_t error = elapsed > expected ? (int64_t)(elapsed - expected)
                                       : -(int64_t)(expected - elapsed);
    if (error > INT32_MAX || error < INT32_MIN) {
      s->beat_count = 0;
      r2_math_inc(&s->tx_reject[3]);return reject(s);
    }
    s->beat_errors[s->beat_head] = (int32_t)error;
    s->beat_head = (s->beat_head + 1) % 32;
    if (s->beat_count < 32)
      s->beat_count++;
    uint64_t squares = 0;
    bool overflow = false;
    for (unsigned i = 0; i < s->beat_count; i++) {
      int64_t e = s->beat_errors[i];
      uint64_t sq = (uint64_t)(e * e);
      if (sq > UINT64_MAX - squares) {
        overflow = true;
        break;
      }
      squares += sq;
    }
    s->beat_rms_ns =
        overflow ? UINT32_MAX : r2_servo_rms_floor(squares, s->beat_count);
  }
  s->tx_seen = true;
  s->last_tx_ns = ns;
  s->last_tx_event = event;
  s->last_available = tx.tick;
  s->sent[1] = s->sent[0];
  s->sent[0] = (struct r2_precise_correction){
      p->publication, event,
      channel,        ns * 2 / 125,
      generation,     expires < p->expires_ns ? expires : p->expires_ns};
  if (s->sent_count < 2)
    s->sent_count++;
  r2_math_inc(&s->accepted);
  return true;
}
bool r2_radio_precision_map(const struct r2_radio_precision *s,r2_servo_map_t *out){
 if(!s||!out||!s->scope.follower||!s->servo.output.valid)return false;
 *out=s->servo.output;
 return r2_empirical_phase_ns(out->anchor,&out->anchor);
}
bool r2_radio_precision_read(const struct r2_radio_precision *s,
                             struct r2_capture now, uint64_t us, int64_t *ns) {
  r2_servo_map_t map;
  return live(s, us) && s->scope.follower && now.kind == R2_CAPTURE_NOW &&
         now.epoch == s->local_epoch && s->last_available &&
         now.tick >= s->last_available &&
         now.tick - s->last_available < R2_CAPTURE_HZ * 3 / 2 &&
         (!s->scope.broadcast||(s->last_accepted&&now.tick>=s->last_accepted&&
          now.tick-s->last_accepted<R2_CAPTURE_HZ*3/2))&&
         (!s->scope.broadcast || (s->data_until && now.tick<s->data_until)) &&
         r2_radio_precision_map(s,&map) &&
         r2_radio_map_time(&map, now.tick, ns);
}
void r2_radio_precision_view(const struct r2_radio_precision *s,
                             struct r2_capture now, uint64_t us,
                             struct r2_radio_precision_view *v) {
  *v = (struct r2_radio_precision_view){.authorized = live(s, us),
                                        .accepted = s->accepted,
                                        .rejected = s->rejected,
                                        .duplicates = s->duplicates,
                                        .overflows = s->overflows};
  memcpy(v->tx_reject,s->tx_reject,sizeof v->tx_reject);
  bool fresh = live(s, us) && now.kind == R2_CAPTURE_NOW &&
               now.epoch == s->local_epoch && s->last_available &&
               now.tick >= s->last_available &&
               now.tick - s->last_available < R2_CAPTURE_HZ * 3 / 2 &&
               (!s->scope.broadcast||(s->last_accepted&&now.tick>=s->last_accepted&&
                now.tick-s->last_accepted<R2_CAPTURE_HZ*3/2))&&
               (!s->scope.broadcast || (s->data_until && now.tick<s->data_until));
  if (s->scope.gm) {
    v->rms_ns = s->beat_rms_ns;
    v->samples = s->beat_count;
    v->rms_valid = fresh && v->samples >= 2 && v->rms_ns != UINT32_MAX;
    return;
  }
  v->fit_valid = fresh && s->servo.fit_valid;
  if (live(s, us) && now.kind == R2_CAPTURE_NOW &&
      now.epoch == s->local_epoch && s->last_accepted && now.tick >= s->last_accepted) {
    uint64_t age = (now.tick - s->last_accepted) / (R2_CAPTURE_HZ / 1000);
    v->correction_age_valid = age <= UINT32_MAX;
    v->correction_age_ms = age <= UINT32_MAX ? (uint32_t)age : UINT32_MAX;
  }
  v->rms_ns = r2_servo_output_quality_rms(&s->servo);
  v->samples = r2_servo_output_quality_bins(&s->servo);
  v->rms_valid = fresh && v->samples >= 2 && v->rms_ns != UINT32_MAX;
  v->trim_ppb = s->servo.output.valid ? s->servo.output.rate : 0;
  v->drift_valid = fresh && s->servo.fit_valid;
  if (v->drift_valid)
    v->drift_ppb = (int32_t)(-(int64_t)s->servo.rate * INT64_C(1000000000) /
                             (INT64_C(1000000000) + s->servo.rate));
  int64_t mapped;
  v->map_valid = r2_radio_precision_read(s, now, us, &mapped);
  v->locked = v->map_valid && r2_servo_pll_locked(&s->servo, now.tick);
}

bool r2_radio_precision_rx_held_origin(struct r2_radio_precision *s,const struct r2_precision_packet *p,
 struct r2_capture rx,uint16_t event,uint8_t channel,struct r2_capture available,
 uint64_t now,int64_t expected,uint32_t signed_horizon_us){
 if(!signed_horizon_us||signed_horizon_us>10000000)return false;
 return receive(s,p,rx,event,channel,available,now,expected,true,(uint64_t)signed_horizon_us*1000);
}

bool r2_radio_map_time(const r2_servo_map_t *map,uint64_t tick,int64_t *ns){
 return r2_servo_mapped(map,tick,ns);
}
