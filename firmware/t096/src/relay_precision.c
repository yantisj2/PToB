/* SPDX-License-Identifier: Apache-2.0 */
#include "relay_precision.h"
#include <limits.h>
static bool reject(struct r2_relay_precision *s){r2_math_inc(&s->rejected);return false;}
static bool same_origin(const struct r2_relay_origin *a,const struct r2_relay_origin *b){
 return a->root==b->root&&a->relay==b->relay&&a->boot==b->boot&&a->source_epoch==b->source_epoch&&
  a->created_ns==b->created_ns&&a->expires_ns==b->expires_ns&&a->frame_ns==b->frame_ns&&a->utc_offset_ns==b->utc_offset_ns&&a->hold_horizon_us==b->hold_horizon_us&&a->hold_budget_ns==b->hold_budget_ns;
}
/* Same physical timestamp domain, strictly no wider source permission. */
static bool origin_restricts(const struct r2_relay_origin *a,const struct r2_relay_origin *b){
 return a->root==b->root&&a->relay==b->relay&&a->boot==b->boot&&a->source_epoch==b->source_epoch&&
  a->created_ns==b->created_ns&&a->expires_ns<=b->expires_ns&&a->frame_ns==b->frame_ns&&a->utc_offset_ns==b->utc_offset_ns&&a->hold_horizon_us<=b->hold_horizon_us&&a->hold_budget_ns<=b->hold_budget_ns;
}
static uint64_t held_until(const struct r2_relay_origin *o){
 return o->hold_horizon_us?r2_sat_add(o->created_ns,(uint64_t)o->hold_horizon_us*1000):o->expires_ns;
}
static bool stale(uint64_t now,uint64_t then){return now<then||now-then>=R2_RADIO_MAX_AGE_TICKS;}
static bool plus_utc(uint64_t ns,int64_t offset,int64_t *out){
 if(ns>INT64_MAX||offset==INT64_MIN||
    (offset>0&&ns>(uint64_t)(INT64_MAX-offset))||
    (offset<0&&ns<(uint64_t)-offset))return false;
 *out=(int64_t)ns+offset;return true;
}
static bool retired(const uint64_t *boots,unsigned count,uint64_t boot){
 for(unsigned i=0;i<count;i++)if(boots[i]==boot)return true;
 return false;
}
static bool retire(struct r2_relay_precision *s,uint64_t *boots,unsigned *count,uint64_t boot){
 if(!boot||retired(boots,*count,boot))return true;
 if(*count==R2_RELAY_RETIRED){s->failed=true;return false;}
 boots[(*count)++]=boot;return true;
}
void r2_relay_precision_withdraw(struct r2_relay_precision *s){
 memset(&s->upstream,0,sizeof s->upstream);memset(s->map,0,sizeof s->map);
 memset(s->seen,0,sizeof s->seen);memset(s->sent,0,sizeof s->sent);
 s->origin_valid=s->phase_admitted=s->published_origin_valid=false;
 s->published_until_tick=0;memset(&s->published_origin,0,sizeof s->published_origin);s->sent_count=s->map_head=0;
 s->last_tx_tick=s->last_root_ns=s->origin_until_tick=0;
}
void r2_relay_precision_pause(struct r2_relay_precision *s){
 memset(s->upstream.original,0,sizeof s->upstream.original);
 memset(s->upstream.correction,0,sizeof s->upstream.correction);
 memset(s->seen,0,sizeof s->seen);
}
void r2_relay_precision_downstream_withdraw(struct r2_relay_precision *s){
 memset(s->sent,0,sizeof s->sent);s->sent_count=0;s->phase_admitted=false;
 s->last_tx_tick=s->last_root_ns=0;
}
void r2_relay_precision_init(struct r2_relay_precision *s,unsigned self,unsigned root,
 unsigned relay,uint64_t boot,uint64_t epoch){
 memset(s,0,sizeof *s);s->self=self;s->root=root;s->relay=relay;
 s->parent=self==relay?root:relay;s->boot=boot;s->epoch=epoch;
 s->bound=R2_RELAY_BOUND_UNKNOWN;
 s->failed=self>=4||root>=4||relay>=4||root==relay||!boot||!epoch;
}
bool r2_relay_precision_train(struct r2_relay_precision *s,uint64_t epoch){
 if(s->failed||!epoch||epoch>UINT32_MAX||s->train==UINT32_MAX||s->map_generation==UINT32_MAX){s->failed=true;return false;}
 ++s->train;++s->map_generation;s->epoch=epoch;s->publication=0;
 memset(s->sent,0,sizeof s->sent);s->sent_count=0;s->phase_admitted=false;
 s->last_tx_tick=s->last_root_ns=0;return true;
}
static bool scope(struct r2_relay_precision *s,const struct r2_relay_packet *p){
 if(retired(s->retired_root,s->retired_root_count,p->origin.boot)||
    retired(s->retired_sender,s->retired_sender_count,p->route.boot))return false;
 bool root_change=s->root_boot!=p->origin.boot;
 if(root_change){
  if(!retire(s,s->retired_root,&s->retired_root_count,s->root_boot))return false;
  s->root_boot=p->origin.boot;s->source_epoch=0;s->origin_highwater=0;
 }
 if(p->origin.source_epoch<s->source_epoch)return false;
 bool epoch_change=p->origin.source_epoch!=s->source_epoch;
 if(epoch_change){s->source_epoch=p->origin.source_epoch;s->origin_highwater=0;}
 if(p->origin.created_ns<s->origin_highwater)return false;
 bool sender_change=s->remote.boot!=p->route.boot;
 if(sender_change&&!retire(s,s->retired_sender,&s->retired_sender_count,s->remote.boot))return false;
 if(!root_change&&!epoch_change&&!sender_change&&s->remote.train){
  if(p->route.train<s->remote.train)return false;
  if(p->route.train==s->remote.train&&
     (p->route.capture_epoch!=s->remote.capture_epoch||p->route.map_generation!=s->remote.map_generation))return false;
 }
 bool train_change=s->remote.train!=p->route.train;
 if(root_change||epoch_change||sender_change||train_change){
  bool retained=!root_change&&!epoch_change&&!sender_change&&train_change&&
   p->route.capture_epoch==s->remote.capture_epoch&&
   r2_radio_precision_rebind_train(&s->upstream,p->route.train,p->route.capture_epoch);
  if(retained){
   memset(s->map,0,sizeof s->map);s->map_head=0;
   memset(s->seen,0,sizeof s->seen);
   r2_relay_precision_downstream_withdraw(s);
  }else r2_relay_precision_withdraw(s);
  r2_math_inc(&s->scope_changes);
 }
 s->remote=p->route;s->origin_highwater=p->origin.created_ns;return true;
}
static struct r2_precision_packet legacy(const struct r2_relay_packet *p){
 struct r2_precision_packet out={.boot=p->origin.boot,.created_ns=p->origin.created_ns,
  .expires_ns=held_until(&p->origin),.utc_offset_ns=p->origin.utc_offset_ns,
  .source_epoch=p->origin.source_epoch,.train=p->route.train,
  .publication=p->route.publication,.capture_epoch=p->route.capture_epoch,
  .root=p->origin.root,.flags=1,.count=p->count};
 for(unsigned i=0;i<p->count;i++)out.correction[i]=(struct r2_precise_correction){
  p->correction[i].publication,p->correction[i].event,p->correction[i].channel,
  p->correction[i].ticks,p->correction[i].generation,held_until(&p->origin)};
 return out;
}
static void filter_replay(struct r2_relay_precision *s,struct r2_precision_packet *p,uint64_t available){
 unsigned count=0;
 for(unsigned c=0;c<p->count;c++){
  struct r2_precise_correction value=p->correction[c];unsigned free=R2_RADIO_PENDING;bool duplicate=false;
  for(unsigned i=0;i<R2_RADIO_PENDING;i++){
   struct r2_relay_seen *q=&s->seen[i];
   if(q->used&&!stale(available,q->arrived)&&q->publication==value.publication&&
      q->event==value.event&&q->channel==value.channel&&q->generation==value.generation){duplicate=true;break;}
   if(!q->used||stale(available,q->arrived))free=i;
  }
  if(duplicate){r2_math_inc(&s->duplicates);continue;}
  if(free==R2_RADIO_PENDING){s->failed=true;continue;}
  s->seen[free]=(struct r2_relay_seen){available,value.publication,(uint32_t)value.generation,value.event,value.channel,true};
  p->correction[count++]=value;
 }
 p->count=count;
}
bool r2_relay_precision_rx(struct r2_relay_precision *s,const struct r2_relay_packet *p,
 const uint8_t proof[32],struct r2_capture rx,uint16_t event,uint8_t channel,
 struct r2_capture available,uint64_t now,int64_t expected){
 int64_t expiry,creation;
 if(!p||!proof||s->failed||s->self==s->root||p->origin.root!=s->root||p->origin.relay!=s->relay||
    p->route.transmitter!=s->parent||p->route.depth!=(s->self==s->relay?0:1)||
    rx.kind!=R2_CAPTURE_RADIO||rx.epoch!=s->epoch||available.kind!=R2_CAPTURE_NOW||
    available.epoch!=rx.epoch||stale(available.tick,rx.tick)||expected<0||
    !plus_utc(held_until(&p->origin),p->origin.utc_offset_ns,&expiry)||
    !plus_utc(p->origin.created_ns,p->origin.utc_offset_ns,&creation)||
    expiry<=expected||(creation>expected&&creation-expected>=250000000))
  return reject(s);
 if(p->origin.hold_horizon_us&&expected>=0){
  int64_t fresh_expiry;
  if(!plus_utc(p->origin.expires_ns,p->origin.utc_offset_ns,&fresh_expiry))return reject(s);
  if(expected>=fresh_expiry&&(p->route.bound_ns==R2_RELAY_BOUND_UNKNOWN||
     p->route.bound_ns>p->origin.hold_budget_ns||s->hold_bound.provenance==R2_BOUND_UNKNOWN))return reject(s);
 }
 /* Common source deadline and independent coarse age are checked before any
  * scope mutation. Relay publication can never refresh an expired envelope. */
 /* An authenticated narrowed origin cannot be rolled back by a delayed
  * packet carrying the older, wider permission with the same creation. */
 if(s->origin_valid&&origin_restricts(&s->origin,&p->origin)&&!same_origin(&s->origin,&p->origin))return reject(s);
 if(!scope(s,p))return reject(s);
 if(!p->origin.frame_ns){r2_math_inc(&s->bootstrap);return false;}
 /* Existing maps describe the same captured timestamp domain. Replace only
  * their permission/proof with the authenticated restriction, never widen it. */
 for(unsigned i=0;i<R2_RELAY_MAPS;i++)if(s->map[i].used&&origin_restricts(&p->origin,&s->map[i].origin)){
  s->map[i].origin=p->origin;memcpy(s->map[i].proof,proof,32);
 }
 s->origin=p->origin;memcpy(s->proof,proof,32);s->origin_valid=true;s->bound=p->route.bound_ns;
 uint64_t remaining=(uint64_t)(expiry-expected);if(remaining>10000000000ULL)remaining=10000000000ULL;
 uint64_t ticks=remaining*2/125;
 if(available.tick>UINT64_MAX-ticks){s->failed=true;return reject(s);}
 s->origin_until_tick=available.tick+ticks;
 struct r2_radio_scope auth={.boot=p->origin.boot,.until_us=now+250000,
  .source_epoch=p->origin.source_epoch,.publication_epoch=p->route.train,.root=s->root,.follower=true};
 r2_radio_precision_scope(&s->upstream,auth,s->epoch);
 struct r2_precision_packet input=legacy(p);filter_replay(s,&input,available.tick);
 if(s->failed)return reject(s);
 bool received=p->origin.hold_horizon_us?r2_radio_precision_rx_held_origin(&s->upstream,&input,rx,event,channel,available,now,expected,p->origin.hold_horizon_us):(s->v14?r2_radio_precision_rx_serving_origin(&s->upstream,&input,rx,event,channel,available,now,expected):r2_radio_precision_rx_immutable_origin(&s->upstream,&input,rx,event,channel,available,now,expected));
 if(received&&r2_radio_precision_step(&s->upstream,available,now)){
  struct r2_relay_map_record *m=&s->map[s->map_head++%R2_RELAY_MAPS];
  *m=(struct r2_relay_map_record){.map=s->upstream.servo.output,.origin=p->origin,.accepted=available.tick,.bound=p->route.bound_ns,.used=true};
  if(!r2_radio_precision_map(&s->upstream,&m->map)){m->used=false;return reject(s);}
  m->bound=R2_RELAY_BOUND_UNKNOWN; /* Empirical correction has no calibrated bound. */
  memcpy(m->proof,proof,32);
 }
 return received;
}
static const struct r2_relay_map_record *map_origin(const struct r2_relay_precision *s,uint64_t tick,
 const struct r2_relay_origin *origin){
 const struct r2_relay_map_record *best=NULL;
 for(unsigned i=0;i<R2_RELAY_MAPS;i++){
  const struct r2_relay_map_record *m=&s->map[i];
  if(m->used&&m->map.valid&&m->map.delivery<=tick&&tick>=m->accepted&&tick-m->accepted<R2_CAPTURE_HZ*10&&
     same_origin(&m->origin,origin)&&(!best||m->map.delivery>best->map.delivery))best=m;
 }
 return best;
}
static bool map_usable(const struct r2_relay_precision *s,const struct r2_relay_map_record *m,uint64_t tick,int64_t *ns){
 if(!m->used||!m->map.valid||tick<m->map.delivery||tick<m->accepted)return false;
 uint64_t age=s->self==s->relay&&!s->passive_receiver?(s->experimental_fresh_us==500000?R2_CAPTURE_HZ/2:R2_RADIO_MAX_AGE_TICKS):R2_CAPTURE_HZ*3/2;
 /* Unsigned age refusal needs no affine projection. Signed holdover still
  * follows its original mapping/bound checks below. */
 if(!m->origin.hold_horizon_us&&(tick-m->map.delivery>=age||tick-m->accepted>=age))return false;
 if(!r2_servo_mapped(&m->map,tick,ns)||*ns<0)return false;
 if(tick-m->map.delivery<age&&tick-m->accepted<age&&(uint64_t)*ns<m->origin.expires_ns)return true;
 if(!m->origin.hold_horizon_us||(uint64_t)*ns<m->origin.created_ns||(uint64_t)*ns>=held_until(&m->origin))return false;
 uint64_t elapsed=r2_ceil_div((uint64_t)*ns-m->origin.created_ns,1000);
 uint64_t phase=m->map.phase<0?(uint64_t)(-(m->map.phase+1))+1:(uint64_t)m->map.phase;
 uint32_t base=phase>=UINT32_MAX?UINT32_MAX:r2_relay_bound_add(m->bound,(uint32_t)phase);
 uint32_t error=r2_hold_error(&s->hold_bound,elapsed,base);
 return error!=R2_V14_UNKNOWN&&error<=m->origin.hold_budget_ns;
}
static const struct r2_relay_map_record *usable_map(const struct r2_relay_precision *s,uint64_t tick){
 /* Test the freshest causal candidate first. An older map cannot win once
  * this one is usable; evaluating all8 affine maps on every deadline-search
  * probe was redundant. Equal-delivery ties retain the original lowest index.
  * If the freshest map is unusable, retain the exact older-map fallback. */
 const struct r2_relay_map_record *latest=NULL,*best=NULL;int64_t ns;
 for(unsigned i=0;i<R2_RELAY_MAPS;i++){
  const struct r2_relay_map_record *m=&s->map[i];
  if(m->used&&m->map.valid&&m->map.delivery<=tick&&m->accepted<=tick&&
     (!latest||m->map.delivery>latest->map.delivery))latest=m;
 }
 if(latest&&map_usable(s,latest,tick,&ns))return latest;
 for(unsigned i=0;i<R2_RELAY_MAPS;i++)if(&s->map[i]!=latest&&
    (!best||s->map[i].map.delivery>best->map.delivery)&&map_usable(s,&s->map[i],tick,&ns))best=&s->map[i];
 return best;
}
bool r2_relay_precision_read(const struct r2_relay_precision *s,struct r2_capture now,
 uint64_t us,int64_t *relative,int64_t *utc){
 (void)us;
 if(s->failed||!s->origin_valid||now.epoch!=s->epoch||now.kind!=R2_CAPTURE_NOW)return false;
 /* Only the leaf may hold its selected software clock through the quiet
  * interval. Relaying still requires the strict250ms fresh upstream map. The
  * immutable original source expiry can shorten this1.5s ceiling. */
 const struct r2_relay_map_record *m=usable_map(s,now.tick);int64_t ns;
 if(!m||!r2_servo_mapped(&m->map,now.tick,&ns)||ns<0||
    (uint64_t)ns>=held_until(&m->origin)||!plus_utc((uint64_t)ns,m->origin.utc_offset_ns,utc))return false;
 *relative=ns;return true;
}
bool r2_relay_precision_root(struct r2_relay_precision *s,struct r2_capture now,
 uint32_t source_epoch,uint64_t ns,uint64_t expires,int64_t utc,uint64_t revision,const uint8_t keys[4][16]){
 if(s->failed||s->self!=s->root||now.epoch!=s->epoch||!source_epoch||!ns||ns>INT64_MAX||
    expires<=ns||expires-ns>(s->v14?1650000000:1500000000)||utc<0||!revision)return reject(s);
 bool change=s->source_epoch!=source_epoch;
 if(change){
  if(source_epoch<s->source_epoch){s->failed=true;return reject(s);}
  s->source_epoch=source_epoch;s->frame_ns=0;r2_relay_precision_withdraw(s);
 }
 /* Renew before the immutable source deadline, not on every third PA.
  * Unnecessary envelope rotation discards otherwise-live physical corrections;
  * one ordinary controller collision can then exceed the250ms map age. Keep
  * the original signed expiry unchanged while it remains authoritative. The
  *500ms lead covers both root and relay captured-correction publication
  * pipelines, including a missed62.5ms event and the50ms owner watchdog. A shorter
  * newly observed source deadline still renews immediately and fences old TX.
  * Do not rotate the same old permit while waiting for the next PPS/RMC: that
  * creates a redundant empty pipeline immediately before the real renewal. */
 bool restrict_only=s->origin_valid&&s->origin.frame_ns==s->frame_ns&&
  ns>=s->origin.created_ns&&ns<s->origin.expires_ns&&expires<s->origin.expires_ns;
 if(restrict_only){
  /* A tighter deadline does not change any physical capture. Re-sign the
   * restriction, retaining creation/frame/UTC and eligible correction tuples. */
  s->origin.expires_ns=expires;
  if(s->group.id?(s->v14?r2_group_origin_sign3(s->proof,&s->origin,&s->group,s->identities):r2_group_origin_sign(s->proof,&s->origin,&s->group,s->identities)):r2_relay_origin_sign(s->proof,&s->origin,keys))return reject(s);
 }else if(!s->origin_valid||s->origin.frame_ns!=s->frame_ns||ns<s->origin.created_ns||
    ns>=s->origin.expires_ns||(s->origin.expires_ns-ns<=500000000&&expires>s->origin.expires_ns)||expires<s->origin.expires_ns){
  s->origin=(struct r2_relay_origin){s->root,s->relay,s->boot,source_epoch,ns,expires,s->frame_ns,utc-(int64_t)ns,s->hold_horizon_us,s->hold_budget_ns};
  if(s->group.id?(s->v14?r2_group_origin_sign3(s->proof,&s->origin,&s->group,s->identities):r2_group_origin_sign(s->proof,&s->origin,&s->group,s->identities)):r2_relay_origin_sign(s->proof,&s->origin,keys))return reject(s);
  s->origin_valid=true;memset(s->sent,0,sizeof s->sent);s->sent_count=0;
 }
 s->revision=revision;s->bound=r2_hold_error(&s->hold_bound,0,0);
 uint64_t remaining=held_until(&s->origin)>ns?(held_until(&s->origin)-ns)*2/125:0;
 if(now.tick>UINT64_MAX-remaining){s->failed=true;return reject(s);}
 s->origin_until_tick=now.tick+remaining;return true;
}
uint32_t r2_relay_precision_valid_for(const struct r2_relay_precision *s,
 struct r2_capture now,uint64_t us,uint32_t maximum){
 int64_t relative,utc;
 if(maximum>250000||!r2_relay_precision_read(s,now,us,&relative,&utc)||
    now.tick>UINT64_MAX-(uint64_t)maximum*16||us>UINT64_MAX-maximum)return 0;
 uint32_t lo=0,hi=maximum;
 while(lo<hi){uint32_t mid=lo+(hi-lo+1)/2;struct r2_capture future=now;future.tick+=(uint64_t)mid*16;
  if(r2_relay_precision_read(s,future,us+mid,&relative,&utc))lo=mid;else hi=mid-1;
 }
 return lo;
}
bool r2_relay_precision_selected_origin(const struct r2_relay_precision *s,
 struct r2_capture now,uint64_t us,struct r2_relay_origin *out){
 int64_t relative,utc;if(!out||!r2_relay_precision_read(s,now,us,&relative,&utc))return false;
 const struct r2_relay_map_record *m=usable_map(s,now.tick);
 if(!m){return false;}
 *out=m->origin;return true;
}
bool r2_relay_precision_prepare(const struct r2_relay_precision *s,struct r2_capture now,
 uint64_t us,bool new_train,struct r2_relay_prepared *out){
 if(s->passive_receiver&&s->self==s->relay)return false;
 if(!out||s->failed||!s->origin_valid||now.kind!=R2_CAPTURE_NOW||now.epoch!=s->epoch||
    s->epoch>UINT32_MAX||now.tick>=s->origin_until_tick||
    (new_train?(s->train==UINT32_MAX||s->map_generation==UINT32_MAX):
     (!s->train||!s->map_generation||s->publication==UINT32_MAX)))return false;
 struct r2_relay_prepared q={.new_train=new_train,.mapped=true,
  .previous_train=s->train,.previous_publication=s->publication,
  .previous_generation=s->map_generation,.valid_until_tick=s->origin_until_tick,
  .published_until_tick=s->published_until_tick};
 const struct r2_relay_map_record *m=NULL;int64_t ns=0,utc;
 if(s->self!=s->root){
  if(s->self!=s->relay||!s->origin.frame_ns)return false;
  m=usable_map(s,now.tick);
  q.mapped=r2_relay_precision_read(s,now,us,&ns,&utc)&&m&&ns>=0&&(uint64_t)ns>=m->origin.created_ns;
  if(q.mapped&&s->published_origin_valid&&s->published_origin.boot==m->origin.boot&&
     s->published_origin.source_epoch==m->origin.source_epoch&&m->origin.created_ns<s->published_origin.created_ns)q.mapped=false;
 }
 bool restriction=s->published_origin_valid&&origin_restricts(&s->origin,&s->published_origin)&&!same_origin(&s->origin,&s->published_origin);
 bool advance=restriction||(s->published_origin_valid&&s->origin.boot==s->published_origin.boot&&
  s->origin.source_epoch==s->published_origin.source_epoch&&
  s->origin.root==s->published_origin.root&&s->origin.relay==s->published_origin.relay&&
  s->origin.created_ns>s->published_origin.created_ns&&
  now.tick<s->published_until_tick&&s->published_until_tick-now.tick<=R2_CAPTURE_HZ/8);
 /* An old-origin map can remain valid up to that origin's deadline. It must
  * not prevent a predeadline envelope advance when no newer map is ready. */
 if(q.mapped&&m&&advance&&!restriction&&m->origin.created_ns<=s->published_origin.created_ns)q.mapped=false;
 struct r2_relay_origin origin=q.mapped&&m?m->origin:s->origin;
 if(q.mapped){
  if(m){
   uint64_t ticks=(held_until(&origin)-(uint64_t)ns)*2/125;
   if(now.tick>UINT64_MAX-ticks)return false;
   uint64_t deadline=now.tick+ticks;
   if(!s->published_origin_valid||!same_origin(&s->published_origin,&origin)||
      !q.published_until_tick||deadline<q.published_until_tick)q.published_until_tick=deadline;
   if(q.published_until_tick<q.valid_until_tick)q.valid_until_tick=q.published_until_tick;
   uint64_t fresh=now.tick+(uint64_t)r2_relay_precision_valid_for(s,now,us,250000)*16;
   if(fresh<q.valid_until_tick)q.valid_until_tick=fresh;
  }
  memcpy(q.proof,m?m->proof:s->proof,32);
 }else if(s->published_origin_valid&&!new_train){
  /* Keep an unchanged envelope through ordinary map gaps. Before its original
   * deadline, a newer or strictly narrower authenticated origin may carry count0 on the
   * SAME controller train. It carries no map/correction authority. The125ms
   * lead covers the62.5ms publication cadence plus the existing50ms owner
   * watchdog budget; failure still closes at the old installed deadline. */
  if(advance){memcpy(q.proof,s->proof,32);q.published_until_tick=s->origin_until_tick;}
  else{origin=s->published_origin;memcpy(q.proof,s->packet_proof,32);
   if(s->published_until_tick<q.valid_until_tick)q.valid_until_tick=s->published_until_tick;}
 }else{memcpy(q.proof,s->proof,32);q.published_until_tick=s->origin_until_tick;}
 if(s->self==s->root)q.published_until_tick=s->origin_until_tick;
 if(now.tick>=q.valid_until_tick)return false;
 q.packet=(struct r2_relay_packet){.origin=origin,.route={s->self,s->root,
  s->self==s->root?0:1,s->self==s->root?0:1,s->boot,s->train+(new_train?1:0),
  new_train?1:s->publication+1,(uint32_t)s->epoch,s->map_generation+(new_train?1:0),s->bound}};
 /* Bound every physical correction carried during this installed payload,
  * including elapsed growth and the entire remaining actuator slew. */
 if(m&&origin.hold_horizon_us){
  int64_t end_ns;
  if(!r2_servo_mapped(&m->map,q.valid_until_tick,&end_ns)||end_ns<0||(uint64_t)end_ns<origin.created_ns)return false;
  uint64_t phase=m->map.phase<0?(uint64_t)(-(m->map.phase+1))+1:(uint64_t)m->map.phase;
  uint32_t base=phase>=UINT32_MAX?UINT32_MAX:r2_relay_bound_add(m->bound,(uint32_t)phase);
  q.packet.route.bound_ns=r2_hold_error(&s->hold_bound,r2_ceil_div((uint64_t)end_ns-origin.created_ns,1000),base);
  if(q.packet.route.bound_ns==UINT32_MAX||q.packet.route.bound_ns>origin.hold_budget_ns)return false;
 }
 if(!new_train&&q.mapped&&origin.frame_ns&&(s->self==s->root||s->phase_admitted)){
  for(unsigned i=0;i<s->sent_count;i++)if(origin_restricts(&origin,&s->sent_origin[i])&&
    s->sent[i].ticks<=UINT64_MAX/125&&s->sent[i].ticks*125/2>=origin.created_ns&&
    s->sent[i].ticks*125/2<held_until(&origin))q.packet.correction[q.packet.count++]=s->sent[i];
 }
 *out=q;return true;
}
bool r2_relay_precision_commit(struct r2_relay_precision *s,
 const struct r2_relay_prepared *q,struct r2_capture now){
 if(!q||s->failed||!s->origin_valid||now.kind!=R2_CAPTURE_NOW||now.epoch!=s->epoch||
    now.tick>=q->valid_until_tick||now.tick>=s->origin_until_tick||
    q->packet.route.boot!=s->boot||q->packet.route.capture_epoch!=s->epoch||
    q->packet.origin.boot!=s->origin.boot||q->packet.origin.source_epoch!=s->origin.source_epoch||
    s->train!=q->previous_train||s->publication!=q->previous_publication||
    s->map_generation!=q->previous_generation)return false;
 if(q->new_train&&!r2_relay_precision_train(s,now.epoch))return false;
 if(!q->mapped)r2_relay_precision_downstream_withdraw(s);
 /* Envelope high-water is independent of whether this publication carries
  * a qualified map. Never roll back to an older map after a count0 advance. */
 s->published_origin=q->packet.origin;s->published_origin_valid=true;
 s->published_until_tick=q->published_until_tick;
 memcpy(s->packet_proof,q->proof,32);s->publication=q->packet.route.publication;return true;
}
bool r2_relay_precision_packet(struct r2_relay_precision *s,struct r2_capture now,uint64_t us,struct r2_relay_packet *p){
 struct r2_relay_prepared q;
 if(!r2_relay_precision_prepare(s,now,us,false,&q)||!r2_relay_precision_commit(s,&q,now))return false;
 *p=q.packet;return true;
}
bool r2_relay_precision_tx(struct r2_relay_precision *s,const struct r2_relay_packet *p,
 struct r2_capture tx,uint16_t event,uint8_t channel,uint64_t source_ns,uint64_t revision,uint64_t expires){
 if(s->passive_receiver&&s->self==s->relay)return reject(s);
 if(s->failed||!p||tx.kind!=R2_CAPTURE_RADIO||tx.epoch!=s->epoch||!tx.tick||
    p->route.transmitter!=s->self||p->route.boot!=s->boot||p->route.train!=s->train||
    p->route.map_generation!=s->map_generation||p->route.capture_epoch!=s->epoch||
    p->origin.root!=s->root||p->origin.relay!=s->relay||channel>36)return reject(s);
 uint64_t ns;
 if(s->self==s->root){
  if(!revision||!source_ns||source_ns>=expires||p->origin.boot!=s->boot||p->origin.source_epoch!=s->source_epoch)return reject(s);
  ns=source_ns;s->revision=revision;
  if(!p->origin.frame_ns){
   /* No bootstrap correction is produced. Fresh signed origin follows. */
   s->frame_ns=ns;memset(s->sent,0,sizeof s->sent);s->sent_count=0;return true;
  }
 }else{
  const struct r2_relay_map_record *m=map_origin(s,tx.tick,&p->origin);int64_t mapped;
  if(s->self!=s->relay||!m||!map_usable(s,m,tx.tick,&mapped)||mapped<0||
     !same_origin(&m->origin,&p->origin))return reject(s);
  ns=(uint64_t)mapped;s->bound=r2_hold_error(&s->hold_bound,r2_ceil_div(ns-m->origin.created_ns,1000),m->bound);
  if(ns<p->origin.frame_ns)return reject(s);
  s->phase_ns=(int64_t)((ns-p->origin.frame_ns)%R2_RELAY_FRAME_NS);
  s->phase_admitted=s->phase_ns>=(int64_t)R2_RELAY_PHASE_MIN_NS&&s->phase_ns<=(int64_t)R2_RELAY_PHASE_MAX_NS;
  if(!s->phase_admitted){
   r2_math_inc(&s->phase_rejected);memset(s->sent,0,sizeof s->sent);s->sent_count=0;return false;
  }
 }
 if(ns<p->origin.created_ns||ns>=held_until(&p->origin)||ns>UINT64_MAX/2||
    (s->last_tx_tick&&tx.tick<=s->last_tx_tick)||
    (s->last_root_ns&&ns<=s->last_root_ns))return reject(s);
 s->last_tx_tick=tx.tick;s->last_root_ns=ns;
 s->sent[1]=s->sent[0];s->sent_origin[1]=s->sent_origin[0];s->sent_origin[0]=p->origin;
 s->sent[0]=(struct r2_relay_correction){
  p->route.publication,event,channel,ns*2/125,s->map_generation};
 if(s->sent_count<2){++s->sent_count;}
 r2_math_inc(&s->tx_corrections);return true;
}
void r2_relay_precision_view(const struct r2_relay_precision *s,struct r2_capture now,
 uint64_t us,struct r2_radio_precision_view *out){
 r2_radio_precision_view(&s->upstream,now,us,out);int64_t ns,utc;
 out->map_valid=r2_relay_precision_read(s,now,us,&ns,&utc);
 out->authorized=!s->failed&&s->origin_valid&&now.epoch==s->epoch&&now.tick<s->origin_until_tick;out->train=s->train;out->publications=s->publication;
 out->rejected+=s->rejected;out->duplicates+=s->duplicates;
 out->locked=out->locked&&out->map_valid;
}

bool r2_relay_phase_retry(uint64_t now,uint64_t requested,int64_t phase,uint32_t *delay){
 if(!delay||requested>now||phase<0||(uint64_t)phase>=R2_RELAY_FRAME_NS)return false;
 uint64_t correction=(R2_RELAY_SLOT_NS+R2_RELAY_FRAME_NS-(uint64_t)phase)%R2_RELAY_FRAME_NS;
 uint64_t target=(requested%62500+(correction+999)/1000)%62500;
 uint64_t wait=(target+62500-now%62500)%62500;
 /* Leave a bounded public close/start opportunity; never spin in callbacks. */
 if(wait<2000)wait+=62500;
 *delay=(uint32_t)wait;return true;
}

bool r2_relay_precision_shadow_map(const struct r2_relay_precision *s,struct r2_capture now,uint64_t us,r2_servo_map_t *map,struct r2_relay_origin *origin,uint64_t *lo){
 int64_t ns,utc;if(!map||!origin||!lo||!r2_relay_precision_read(s,now,us,&ns,&utc))return false;
 const struct r2_relay_map_record *m=usable_map(s,now.tick);if(!m)return false;
 *map=m->map;*origin=m->origin;*lo=m->accepted;return true;
}
