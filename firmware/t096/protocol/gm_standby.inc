/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * G5 is the normal GM owner. Legacy Config5 experiments below remain isolated.
 * One ACTIVE appoints one successor; inventory never supplies source permission.
 */
static void gm5_event(struct r2_network *n,unsigned reason,uint64_t peer)
{
 struct r2_gm5 *g=&n->gm5;g->last_reason=reason;
 r2_timing_event(R2_TIMING_GM_CONTROL,0,peer,40u+reason,g->role==R2_GM5_ACTIVE?g->generation:n->management_generation,g->role);
 struct r2_gm5_transition *e=&g->history[g->transitions%R2_GM5_HISTORY];
 *e=(struct r2_gm5_transition){n->now,peer,g->role==R2_GM5_ACTIVE?g->generation:n->management_generation,g->role,reason};count(&g->transitions);
}
static struct r2_gm5_candidate *gm5_candidate(struct r2_network *n,uint64_t id,bool create)
{
 if(!id||id==n->identity)return NULL;
 struct r2_gm5_candidate *old=NULL;
 for(unsigned i=0;i<R2_GM5_CANDIDATES;i++){
  struct r2_gm5_candidate *c=&n->gm5.candidate[i];if(c->identity==id)return c;
  if(!c->identity)return create?c:NULL;
  if(c->identity!=n->gm5.selected&&c->identity!=n->gm5.active&&(!old||c->at<old->at))old=c;
 }
 return create?old:NULL;
}
static unsigned gm5_slot(const struct r2_network *n,uint64_t id)
{for(unsigned p=0;p<n->count;p++)if(p!=n->self&&n->peer[p].identity==id)return p;return 255;}
static bool gm5_eligible(const struct r2_network *n,const struct r2_gm5_candidate *c)
{return c->identity&&c->eligible&&(!c->inhibited||c->negative_root==n->identity)&&n->now<c->until&&n->now>=c->cooldown&&c->epoch&&c->boot;}
/* CONTACT -> ACQUIRE -> HOT -> REPAIR share this sole phase deadline.
 * Only a causal appointment replaces CONTACT; repair can arm only once. */
static void gm5_attempt_done(struct r2_gm5 *g)
{g->attempt_started=g->attempt_until=0;g->contacting=false;}
static void gm5_attempt_clear(struct r2_gm5 *g)
{gm5_attempt_done(g);g->stable_since=g->ready_at=0;}
static void gm5_attempt_start(struct r2_network *n,uint64_t duration)
{n->gm5.attempt_started=n->now;n->gm5.attempt_until=n->now+duration;}
static void gm5_repair(struct r2_network *n)
{
 if(n->gm5.role==R2_GM5_HOT)n->gm5.role=R2_GM5_ACQUIRING;
 if(!n->gm5.attempt_until){gm5_attempt_start(n,R2_GM5_REPAIR_US);n->gm5.repair_used=true;}
}
static void gm5_grant_clear(struct r2_network *n,unsigned reason)
{
 if(n->standby_authorized||n->gm5.grant_until){n->gm5.grant_until=0;n->standby_authorized=false;gm5_event(n,reason,n->gm5.active);}
 /* G5 reason is an enum; legacy withdrawal diagnostics are a bitmask. */
 withdraw_hot(n,512u);
 if(n->gm5.role==R2_GM5_HOT)gm5_repair(n);
 n->takeover_pending=false;
}
static void gm5_select(struct r2_network *n)
{
 struct r2_gm5 *g=&n->gm5;if(g->role!=R2_GM5_ACTIVE||g->selected||n->now<g->next_attempt||n->now<g->retire_until)return;
 struct r2_gm5_candidate *best=NULL;
 for(unsigned i=0;i<R2_GM5_CANDIDATES;i++){
  struct r2_gm5_candidate *c=&g->candidate[i];if(!gm5_eligible(n,c))continue;
  struct r2_clock_quality a=c->quality,b=best?best->quality:c->quality;a.identity=b.identity=0;
  int quality=best?r2_quality_compare(&a,&b):-1;
  /* Quality dataset first, successful recent acquisition then fewer failures.
   * Compare full stable identity only after the independent history fields. */
  if(!best||quality<0||(quality==0&&(c->successes>best->successes||
     (c->successes==best->successes&&(c->failures<best->failures||(c->failures==best->failures&&c->identity<best->identity))))))best=c;
 }
 if(!best){if(g->last_reason!=R2_GM5_NO_CANDIDATE)gm5_event(n,R2_GM5_NO_CANDIDATE,0);return;}
 /* Inventory nominates one peer, not a delivered appointment. The original
  * contact deadline also bounds an unreachable best candidate and its local
  * failure history. Sparse COLD sleep must not spend the PA acquisition time. */
 g->selected=best->identity;gm5_attempt_start(n,R2_GM5_CONTACT_US);
 g->stable_since=g->ready_at=0;g->repair_used=false;g->grant_pending=false;g->contacting=true;g->next_poll=n->now;count(&g->attempts);gm5_event(n,R2_GM5_CONTACT,g->selected);
}
static void gm5_failed(struct r2_network *n,unsigned reason)
{
 struct r2_gm5 *g=&n->gm5;uint64_t old=g->selected;
 struct r2_gm5_candidate *c=gm5_candidate(n,old,false);
 if(c){if(c->failures!=UINT16_MAX)++c->failures;c->last_reason=reason;
  unsigned shift=c->failures>4?4:c->failures-1;uint64_t delay=UINT64_C(30000000)<<shift;
  if(delay>300000000)delay=300000000;
  c->cooldown=n->now+delay;}
 count(&g->failures);if(g->issued_until+R2_GM5_RETIRE_GUARD_US>g->retire_until)g->retire_until=g->issued_until+R2_GM5_RETIRE_GUARD_US;
 g->selected=0;gm5_attempt_clear(g);
 g->next_attempt=n->now+5000000;management_clear(n);n->coordinator=255;n->coordinator_boot=0;
 n->pair=(struct r2_pair){.root=255,.standby=255};gm5_event(n,reason,old);
}
uint64_t r2_gm5_control_peer(const struct r2_network *n)
{
 const struct r2_gm5 *g=&n->gm5;if(!g->enabled)return 0;
 return g->role==R2_GM5_ACTIVE?g->selected:
  (g->role==R2_GM5_ACQUIRING||g->role==R2_GM5_HOT)?g->active:0;
}
bool r2_gm5_wants_control(const struct r2_network *n,uint64_t id)
{return id&&r2_gm5_control_peer(n)==id&&!(n->gm5.role==R2_GM5_ACTIVE&&n->gm5.contacting);}
bool r2_gm5_poll_due(const struct r2_network *n,uint64_t now)
{return n->gm5.enabled&&now>=n->gm5.next_poll&&now-n->gm5.next_poll<1000000;}
void r2_gm5_poll_completed(struct r2_network *n,uint64_t now)
{
 struct r2_gm5 *g=&n->gm5;if(now<g->next_poll)return;
 /* One original due time; no catch-up. Missed and completed opportunities
  * share this policy: an unpaired ACTIVE's self identity is not an incumbent.
  * Pair repair gets precedence. */
 g->next_poll=now+((r2_gm5_control_peer(n)&&!bound(n,gm5_slot(n,r2_gm5_control_peer(n))))?5000000:g->role==R2_GM5_ACTIVE?(g->selected?30000000:5000000):g->active?30000000:5000000);
}
static void gm5_set_root(struct r2_network *n,unsigned root)
{
 uint64_t boot=root==n->self?n->boot:root<n->count?n->peer[root].boot:0;
 uint32_t epoch=root==n->self?n->candidate.generation:root<n->count?n->peer[root].candidate_epoch:0;
 if(root==n->choice&&boot==n->root_boot&&(!n->view.available||epoch==n->view.generation))return;
 if(root!=n->self&&epoch&&n->view.available&&epoch!=n->view.generation){gm5_grant_clear(n,R2_GM5_ROOT_CHANGED);n->management_generation=0;n->gm5.grant_generation=0;}
 authority_clear(n);n->choice=root;n->root_boot=boot;n->fence=n->now+3000000;count(&n->topologies);
}
/* Same finite grant can be retransmitted, never extended. New original grants
 * use strictly increasing generations. A lost COMMIT/READY is conservatively
 * counted as issued by ACTIVE before transport submission. */
static int gm5_management_receive(struct r2_network *n,unsigned p,const struct r2_auth_record *r,uint64_t permit,uint64_t expiry)
{
 struct r2_gm5 *g=&n->gm5;
 if(!live(n,p)||!n->ready||n->coordinator>=n->count||r->root!=n->choice||
    r->root_boot!=n->root_boot||r->source_epoch!=n->view.generation||
    r->source_second!=descriptor(n)||!r->decision||r->decision>UINT32_MAX||
    r->coordinator_boot!=n->coordinator_boot)return -ESTALE;
 if(g->role==R2_GM5_ACTIVE&&!g->contacting&&p==n->coordinator&&r->kind==R2_AUTH_READY&&r->decision==g->generation){
  if(!(r->flags&1)){g->ready_at=g->stable_since=0;withdraw_hot(n,32);return 0;}
  g->ready_at=n->now;
  if((r->flags&4)&&g->grant_pending&&n->now<g->issued_until){
   g->grant_pending=false;n->hot_ready=true;n->management_committed=true;
   n->management_until=n->now+coordination_lease(n);
   if(g->attempt_until){gm5_attempt_done(g);count(&g->successes);struct r2_gm5_candidate *c=gm5_candidate(n,g->selected,false);if(c&&c->successes!=UINT16_MAX)++c->successes;gm5_event(n,R2_GM5_COMMITTED,g->selected);}
  }
  return 0;
 }
 if((g->role==R2_GM5_ACQUIRING||g->role==R2_GM5_HOT)&&p==n->choice){
  if(r->kind==R2_AUTH_PREPARE){
   if(r->decision<n->management_generation)return -ESTALE;
   n->management_generation=(uint32_t)r->decision;
   n->management_hash=r->source_second;n->management_pending_until=expiry;
   response(n,p,r,R2_AUTH_READY,expiry);n->peer[p].response.flags=synced(n)&&n->admission.eligible&&!g->inhibited?1:0;
   return 0;
  }
  if(r->kind==R2_AUTH_COMMIT){
   if(r->flags!=1||r->decision<n->management_generation||r->decision<g->grant_generation||
      permit<=n->now||r->permit_until_us<=r->created_us||r->permit_until_us-r->created_us>R2_GM5_GRANT_US)return -ESTALE;
   if(r->decision==g->grant_generation&&!g->grant_until)return -ESTALE;
   if(r->decision>g->grant_generation){g->grant_generation=(uint32_t)r->decision;g->grant_until=permit;}
   else if(permit<g->grant_until)g->grant_until=permit;
   n->management_generation=(uint32_t)r->decision;
   n->management_hash=r->source_second;n->management_until=n->now+coordination_lease(n);
   n->management_committed=true;
   if(synced(n)&&n->admission.eligible&&!g->inhibited){
    n->hot_ready=n->standby_authorized=true;g->role=R2_GM5_HOT;gm5_attempt_done(g);
    n->standby_root_boot=n->root_boot;n->standby_source_epoch=n->candidate.generation;
    n->pair=(struct r2_pair){n->root_boot,n->boot,g->grant_until,n->view.generation,n->choice,n->self};
   }
   response(n,p,r,R2_AUTH_READY,expiry);n->peer[p].response.flags=n->hot_ready?5:0;
   count(&n->management_commits);return 0;
  }
 }
 return -ESTALE;
}
static void gm5_tick(struct r2_network *n,bool physical,uint64_t now,const struct r2_gm_coordination *coord)
{
 struct r2_gm5 *g=&n->gm5;
 bool constrained=r2_gm_coordination_live(coord,n->identity,now);
 bool direct=constrained&&r2_network_coordination_direct(n,coord,now);
 unsigned constrained_peer=constrained?gm5_slot(n,coord->root):255;
 /* A current direct authenticated withdrawal retires exactly this root boot
  * and source epoch. Keep the original negative-constraint deadline, never a
  * rolling exception. It cannot create successor authority: the finite grant,
  * own source and original output fences remain independently necessary. */
 if(constrained&&direct&&n->standby_authorized&&now<g->grant_until&&
    g->active==coord->root&&g->active_boot==coord->boot&&n->pair.epoch==coord->epoch&&
    constrained_peer<n->count&&bound(n,constrained_peer)&&n->peer[constrained_peer].source_withdrawn&&
    !n->peer[constrained_peer].candidate_epoch&&
    (g->retired_root!=coord->root||g->retired_boot!=coord->boot||g->retired_epoch!=coord->epoch)){
  g->retired_root=coord->root;g->retired_boot=coord->boot;g->retired_epoch=coord->epoch;g->retired_until=coord->until;
 }
 bool retired=constrained&&coord->boot&&g->retired_root==coord->root&&g->retired_boot==coord->boot&&g->retired_epoch==coord->epoch&&now<g->retired_until;
 bool own=physical&&(!constrained||direct||retired);g->inhibited=constrained&&!direct&&!retired;
 if(constrained&&!retired&&((g->role==R2_GM5_ACTIVE)||(g->active&&g->active!=coord->root))){
  if(g->role==R2_GM5_ACTIVE&&g->selected)gm5_failed(n,R2_GM5_INHIBITED);
  gm5_grant_clear(n,R2_GM5_INHIBITED);g->role=R2_GM5_COLD;gm5_attempt_clear(g);g->active=g->active_boot=g->active_until=g->selected=0;
  gm5_set_root(n,255);gm5_event(n,R2_GM5_INHIBITED,coord->root);
 }
 if(now>=g->next_poll&&now-g->next_poll>=1000000)r2_gm5_poll_completed(n,now);
 if(!g->round_until)g->round_until=n->admission.boot_at+R2_ELECTION_STABLE_US+5000000+(n->identity%3)*1000000;
 n->members_mask=own?1u<<n->self:0;
 for(unsigned p=0;p<n->count;p++)if(p!=n->self&&live(n,p)&&now<n->peer[p].candidate_until)n->members_mask|=1u<<p;
 if(g->grant_until&&now>=g->grant_until)gm5_grant_clear(n,R2_GM5_GRANT_EXPIRED);
 if(!own){
  gm5_grant_clear(n,g->inhibited?R2_GM5_INHIBITED:R2_GM5_SOURCE);
  if(g->role==R2_GM5_ACTIVE){if(g->selected)gm5_failed(n,R2_GM5_SOURCE);g->active=g->active_boot=g->active_until=0;g->role=R2_GM5_COLD;gm5_attempt_clear(g);gm5_set_root(n,255);gm5_event(n,g->inhibited?R2_GM5_INHIBITED:R2_GM5_SOURCE,0);}
 }
 /* Only the current finite grant and direct root liveness may start takeover.
  * A map gap withdraws HOT immediately, while the one repair timer continues. */
 unsigned active=gm5_slot(n,g->active);
 struct r2_gm5_candidate *root_candidate=gm5_candidate(n,g->active,false);
 bool withdrawn=root_candidate&&root_candidate->boot==g->active_boot&&root_candidate->withdraw_created&&root_candidate->withdraw_epoch==n->pair.epoch;
 bool root_failed=g->active&&g->active!=n->identity&&((retired&&g->active==coord->root)||withdrawn||(active<n->count&&bound(n,active)&&n->peer[active].source_withdrawn)||
  (n->root_health_at&&now>=n->root_health_at&&now-n->root_health_at>=coordination_lease(n)));
 if(n->standby_authorized&&own&&g->active_boot==n->standby_root_boot&&n->candidate.generation==n->standby_source_epoch&&g->grant_until>now&&root_failed){
  if(!n->takeover_pending){n->takeover_pending=true;n->takeover_started=now;n->takeover_not_before=now+3000000;gm5_event(n,R2_GM5_TAKEOVER,g->active);}
  if(now>=n->takeover_not_before&&now<g->grant_until){
   gm5_attempt_clear(g);g->role=R2_GM5_ACTIVE;g->active=n->identity;g->active_boot=n->boot;g->active_until=0;g->selected=0;
   g->next_attempt=now+5000000;g->retire_until=g->grant_until+R2_GM5_RETIRE_GUARD_US;
   gm5_set_root(n,n->self);/* The already completed3s takeover fence is final. */
   n->fence=now;gm5_grant_clear(n,R2_GM5_TAKEOVER);n->coordinator=255;n->coordinator_boot=0;
  }
 }else if(n->takeover_pending&&!root_failed)n->takeover_pending=false;
 if(g->role!=R2_GM5_ACTIVE&&g->active&&now>=g->active_until&&!n->standby_authorized){
  g->active=g->active_boot=0;g->selected=0;g->role=R2_GM5_COLD;gm5_attempt_clear(g);gm5_set_root(n,255);
  g->round_until=now+30000000+(n->identity%3)*1000000;gm5_event(n,R2_GM5_ROOT_CHANGED,0);
 }
 if(g->role==R2_GM5_COLD&&!g->active&&own&&(!constrained||retired)&&(!n->observed_root||n->observed_root==n->identity||now>=n->observed_until)&&now>=g->round_until&&now>=g->retire_until){
  bool preferred=true;
  for(unsigned i=0;i<R2_GM5_CANDIDATES;i++){struct r2_gm5_candidate *c=&g->candidate[i];if(gm5_eligible(n,c)&&r2_quality_compare(&c->quality,&n->quality)<0)preferred=false;}
  if(preferred){gm5_attempt_clear(g);g->role=R2_GM5_ACTIVE;g->active=n->identity;g->active_boot=n->boot;gm5_set_root(n,n->self);gm5_event(n,R2_GM5_SELECTED,n->identity);}
 }
 if(g->role==R2_GM5_ACTIVE&&own){
  gm5_set_root(n,n->self);
  if(now>=n->fence){if(!n->local_gm){if(n->next_publication==UINT32_MAX){r2_network_stop(n,now);return;}n->publication=++n->next_publication;}
   n->local_gm=n->ready=true;n->view=n->candidate;n->service_until=n->candidate.valid_until_us;}
  if(g->selected){
   unsigned p=gm5_slot(n,g->selected);struct r2_gm5_candidate *c=gm5_candidate(n,g->selected,false);
   bool source=p<n->count&&live(n,p)&&now<n->peer[p].candidate_until&&!n->peer[p].source_withdrawn;
   bool physical=source||(c&&c->eligible&&(!c->inhibited||c->negative_root==n->identity)&&now<c->until);
   bool ready=source&&g->ready_at&&now>=g->ready_at&&now-g->ready_at<coordination_lease(n);
   if(!ready||!own||!n->ready||!synced(n)){
    bool was_hot=n->hot_ready||g->successes;withdraw_hot(n,ready?32:8);g->stable_since=0;g->grant_pending=false;
    if(was_hot&&!g->attempt_until){gm5_repair(n);count(&g->attempts);gm5_event(n,R2_GM5_MAP,g->selected);}
   }
   if(!physical){gm5_failed(n,R2_GM5_SOURCE);}
   else if(g->attempt_until&&now>=g->attempt_until){gm5_failed(n,R2_GM5_TIMEOUT);}
   else if(!g->contacting&&ready&&n->ready&&synced(n)){
    if(!g->stable_since)g->stable_since=now;
    if(now-g->stable_since>=R2_GM5_STABLE_US){
     if(!g->issued_until||g->issued_until<=now+R2_GM5_GRANT_US-2000000){
      if(g->generation==UINT32_MAX){r2_network_stop(n,now);return;}
      ++g->generation;g->issued_until=now+R2_GM5_GRANT_US;g->grant_pending=true;
      n->management_generation=g->generation;n->management_hash=descriptor(n);
      n->peer[p].next_management=0;
     }
     n->pair=(struct r2_pair){n->boot,n->peer[p].boot,g->issued_until,n->candidate.generation,n->self,p};
    }
   }
   if(g->selected&&p<n->count){n->coordinator=p;n->coordinator_boot=n->peer[p].boot;}
  }
  gm5_select(n);
 }else if(g->role==R2_GM5_ACQUIRING||g->role==R2_GM5_HOT){
  if(g->role==R2_GM5_ACQUIRING&&g->attempt_until&&now>=g->attempt_until&&!n->standby_authorized){
   g->role=R2_GM5_COLD;gm5_attempt_clear(g);g->selected=0;gm5_set_root(n,255);gm5_event(n,R2_GM5_TIMEOUT,g->active);return;
  }
  active=gm5_slot(n,g->active);gm5_set_root(n,active);
  n->coordinator=n->self;n->coordinator_boot=n->boot;n->local_gm=false;
  n->ready=active<n->count&&now>=n->fence&&n->view.available&&n->publication&&now<n->service_until&&n->view.generation==n->peer[active].candidate_epoch;
  n->follower_valid=n->ready;
  if(!own||!synced(n)||now>=n->management_until){withdraw_hot(n,!own?8:32);if(g->role==R2_GM5_HOT){gm5_repair(n);}}
 }else{gm5_set_root(n,255);n->coordinator=255;n->coordinator_boot=0;n->ready=n->local_gm=n->follower_valid=false;withdraw_hot(n,1);}
 n->follower_valid=n->ready&&!n->local_gm;
 if(n->follower_valid){n->follower_utc=n->view.utc_second*1000000+now-n->view.evidence_us;n->follower_at=n->follower_last_good=now;n->follower_until=n->service_until;count(&n->follower_updates);}
 n->follower_clock_valid=n->follower_last_good&&now-n->follower_last_good<R2_HOLDOVER_US;
 if(g->selected&&g->role==R2_GM5_ACTIVE&&g->generation==0)g->generation=1;
 if(g->role==R2_GM5_ACTIVE)n->management_generation=g->generation;
}
/* Four classes share the existing cursor, advanced only on transport acceptance.
 * Every due class gets a turn within four accepted opportunities. Responses do
 * not bypass that order; their original expiry still clips acceptance. Shared
 * 500ms probes bound production, with 1s G5 records covering delayed owners.
 * Bootstrap HELLO remains200ms; overload cannot extend source/grant authority. */
static bool gm5_next(struct r2_network *n,uint8_t available,uint8_t *peer,uint8_t *bytes,bool *is_hello)
{
 uint64_t wanted=r2_gm5_control_peer(n);unsigned p=gm5_slot(n,wanted);
 if(p>=n->count||!(available&(1u<<p))||!n->peer[p].connected)return false;
 struct r2_net_peer *m=&n->peer[p];struct r2_auth_record r;bool h=false;struct r2_gm5 *g=&n->gm5;
 bool authenticated=bound(n,p);
 bool due[4]={
  m->response_pending&&n->now<m->response.expires_us,
  n->now>=m->next_hello,
  authenticated&&g->role==R2_GM5_ACTIVE&&n->ready&&n->now>=m->next_service&&
   (m->acked_publication!=n->publication||m->acked_permit!=n->service_until),
  authenticated&&g->role==R2_GM5_ACTIVE&&!g->contacting&&n->ready&&
   n->coordinator==p&&n->now>=m->next_management};
 unsigned kind=4;
 for(unsigned i=0;i<4;i++){unsigned k=(n->next_peer+i)%4;if(due[k]){kind=k;break;}}
 if(kind==0)r=m->response;
 else if(kind==1){
  if(n->hello_sequence==UINT64_MAX){r2_network_stop(n,n->now);return false;}
  r=record(n,p,R2_AUTH_HELLO);r.root=255;r.coordinator_boot=n->boot;r.challenge=m->own_nonce;r.sequence=++n->hello_sequence;
  r.decision=m->nonce?m->nonce:1;r.root_boot=m->boot;r.source_second=m->hello_created;r.evidence_us=m->hello_received?m->hello_received:n->now;
  r.priority1=n->quality.priority1;r.clock_class=n->quality.clock_class;r.accuracy=n->quality.accuracy;r.priority2=n->quality.priority2;r.variance=n->quality.variance;
  r.boot_age_ms=milliseconds(n->now-n->admission.boot_at);r.source_age_ms=n->admission.observing?milliseconds(n->now-n->admission.stable_at):0;
  if(n->admission.eligible&&!g->inhibited&&n->candidate.available&&n->now<n->candidate.valid_until_us){r.root=n->self;r.source_epoch=n->candidate.generation;r.permit_until_us=n->candidate.valid_until_us;r.flags=1;}
  h=true;
 }else if(kind==2){
  r=record(n,p,R2_AUTH_SERVICE);r.decision=n->publication;r.source_epoch=n->view.generation;r.source_second=n->view.utc_second;r.evidence_us=n->view.evidence_us;r.permit_until_us=n->view.valid_until_us;r.flags=1|(n->view.relative_ready?4:0);
 }else if(kind==3){
  bool commit=(n->hot_ready||g->grant_pending)&&n->now<g->issued_until;
  r=record(n,p,commit?R2_AUTH_COMMIT:R2_AUTH_PREPARE);r.coordinator_boot=n->coordinator_boot;r.decision=g->generation;r.source_epoch=n->view.generation;r.source_second=descriptor(n);r.flags=commit?1:0;
  if(commit)r.permit_until_us=g->issued_until;
 }else return false;
 if(!h){if(n->tx_sequence==UINT64_MAX){r2_network_stop(n,n->now);return false;}r.sequence=++n->tx_sequence;}
 if(encode_control(n,bytes,&r,m->tx_key))return false;
 *peer=p;*is_hello=h;return true;
}

int r2_gm5_status_encode(struct r2_network *n,const struct r2_join_exchange *e,unsigned phase,uint8_t out[R2_GM5_STATUS_BYTES])
{
 if(!n||!e||!n->gm5.enabled||phase>2||!e->accepted||n->now>=e->until)return -EACCES;
 struct r2_gm5 *g=&n->gm5;
 /* Arm once, only while constructing the matching causal appointment reply
  * (server1/client2). Construction precedes local send acceptance, so a busy
  * or lost send can only shorten this fixed budget, never renew it. STATUS0
  * carries inventory only; a different peer/session cannot arm the nominee. */
 struct r2_gm5_candidate *c=gm5_candidate(n,e->peer,false);
 if(g->role==R2_GM5_ACTIVE&&g->contacting&&g->selected==e->peer&&
    ((e->server&&phase==1)||(!e->server&&phase==2))&&n->now<g->attempt_until&&
    c&&c->boot==e->peer_boot&&gm5_eligible(n,c)&&n->admission.eligible&&!g->inhibited&&
    n->candidate.available&&n->now<n->candidate.valid_until_us){
  g->contacting=false;gm5_attempt_start(n,R2_GM5_ACQUIRE_US);
  gm5_event(n,R2_GM5_SELECTED,g->selected);
 }
 memset(out,0,R2_GM5_STATUS_BYTES);out[0]=19;out[1]=phase;out[2]=5;out[3]=g->role;
 r2_group_put(out+4,n->group.epoch,4);
#define P(o,v,b) r2_group_put(out+o,v,b)
 P(8,n->identity,8);P(16,e->peer,8);P(24,n->boot,8);P(32,e->peer_boot,8);
 P(40,e->server?e->remote_nonce:e->nonce,8);P(48,e->server?e->nonce:e->remote_nonce,8);P(56,n->now,8);
 P(64,n->candidate.generation,4);out[68]=(n->admission.eligible?1:0)|(g->inhibited?2:0);
 out[69]=n->quality.priority1;out[70]=n->quality.clock_class;out[71]=n->quality.accuracy;out[72]=n->quality.priority2;P(73,n->quality.variance,2);
 P(76,g->active,8);P(84,g->active_boot,8);P(92,(g->role==R2_GM5_HOT||g->role==R2_GM5_ACQUIRING)?n->identity:g->contacting?0:g->selected,8);
 unsigned p=gm5_slot(n,g->selected);P(100,p<n->count?n->peer[p].boot:0,8);
 P(108,g->role==R2_GM5_ACTIVE?g->generation:n->management_generation,4);P(112,g->role==R2_GM5_ACTIVE?g->issued_until:g->grant_until,8);P(120,g->attempt_until,8);
 P(128,n->admission.observing?milliseconds(n->now-n->admission.stable_at):0,4);P(132,g->last_reason,4);P(136,g->negative_root,8);
#undef P
 int rc=r2_group_mac(out+144,&n->group,"PTOBG5S5",out,144);if(!rc)count(&g->status_tx);return rc;
}
int r2_gm5_status_receive(struct r2_network *n,const struct r2_join_exchange *e,unsigned phase,const uint8_t *w,size_t size,uint64_t now)
{
 if(!n||!e||!n->gm5.enabled||!e->accepted||now>=e->until||size!=R2_GM5_STATUS_BYTES||w[0]!=19||w[1]!=phase||w[2]!=5||w[3]>R2_GM5_ACTIVE||w[68]>3||w[75]||
  r2_group_get(w+4,4)!=n->group.epoch||r2_group_get(w+8,8)!=e->peer||r2_group_get(w+16,8)!=n->identity||r2_group_get(w+24,8)!=e->peer_boot||r2_group_get(w+32,8)!=n->boot||
  r2_group_get(w+40,8)!=(e->server?e->remote_nonce:e->nonce)||r2_group_get(w+48,8)!=(e->server?e->nonce:e->remote_nonce)||
  !r2_group_verify(w+144,&n->group,"PTOBG5S5",w,144))return -EACCES;
 struct r2_gm5_candidate *c=gm5_candidate(n,e->peer,true);if(!c)return -ENOSPC;
 struct r2_gm5 *g=&n->gm5;n->now=now;
 uint64_t boot=r2_group_get(w+24,8);if(c->identity!=e->peer){*c=(struct r2_gm5_candidate){0};}
 else if(c->boot!=boot){uint64_t cooldown=c->cooldown;uint16_t failures=c->failures,successes=c->successes,last=c->last_reason;
  *c=(struct r2_gm5_candidate){.cooldown=cooldown,.failures=failures,.successes=successes,.last_reason=last};}
 uint64_t created=r2_group_get(w+56,8);uint32_t epoch=(uint32_t)r2_group_get(w+64,4);
 /* A delayed, authenticated positive inside the still-live JOIN must not undo
  * a newer withdrawal, including after the finite successor has taken over.
  * A newer non-ACTIVE report may describe physical requalification in the
  * same epoch and permit nomination. It cannot revive an ACTIVE claim or clear
  * the withdrawal floor; only actual positive causal HELLO retires that fence. */
 if(c->withdraw_created&&(created<=c->withdraw_created||((w[68]&1)&&w[3]==R2_GM5_ACTIVE&&epoch==c->withdraw_epoch)))return -ESTALE;
 c->identity=e->peer;c->boot=boot;
 if((c->eligible&&!(w[68]&1))||(g->active==c->identity&&g->active_boot==boot&&w[3]!=R2_GM5_ACTIVE))gm5_negative(n,c,created,epoch);
 c->at=now;c->until=now+R2_GM5_STATUS_US;c->physical_at=now;c->physical_until=0;c->role=w[3];c->eligible=w[68]&1;c->inhibited=w[68]&2;c->epoch=(uint32_t)r2_group_get(w+64,4);
 c->quality=(struct r2_clock_quality){w[69],w[70],w[71],w[72],(uint16_t)r2_group_get(w+73,2),c->identity};
 c->root=r2_group_get(w+76,8);c->root_boot=r2_group_get(w+84,8);c->selected=r2_group_get(w+92,8);c->selected_boot=r2_group_get(w+100,8);
 c->generation=(uint32_t)r2_group_get(w+108,4);c->grant_until=r2_group_get(w+112,8);c->attempt_until=r2_group_get(w+120,8);c->source_age_ms=(uint32_t)r2_group_get(w+128,4);c->negative_root=r2_group_get(w+136,8);c->reported_reason=(uint8_t)r2_group_get(w+132,4);
 /* Reported remote history is descriptive. Our failures/successes/cooldown
  * remain local observations, not overwritten by the candidate's claims. */
 count(&g->status_rx);
 bool constrained=g->negative_root!=0;
 bool retired=constrained&&g->retired_root==g->negative_root&&g->retired_boot==g->negative_boot&&g->retired_epoch==g->negative_epoch&&now<g->retired_until;
 if(c->eligible&&c->epoch&&c->role==R2_GM5_ACTIVE&&c->root==c->identity&&c->root_boot==c->boot&&(!constrained||retired||(c->identity==g->negative_root&&(!g->negative_boot||c->boot==g->negative_boot)&&c->epoch==g->negative_epoch))){
  bool wins=g->role!=R2_GM5_ACTIVE||c->identity<n->identity;
  if(wins){
   if(g->role==R2_GM5_ACTIVE){if(g->selected)gm5_failed(n,R2_GM5_MERGE);gm5_set_root(n,255);gm5_grant_clear(n,R2_GM5_MERGE);g->role=R2_GM5_COLD;gm5_attempt_clear(g);gm5_event(n,R2_GM5_MERGE,c->identity);}
   if(g->active!=c->identity||g->active_boot!=c->boot){gm5_grant_clear(n,R2_GM5_ROOT_CHANGED);n->management_generation=0;g->grant_generation=0;g->role=R2_GM5_COLD;gm5_attempt_clear(g);gm5_set_root(n,255);}
   g->active=c->identity;g->active_boot=c->boot;g->active_until=c->until;
   if(c->selected==n->identity&&n->admission.eligible&&(!g->inhibited||g->negative_root==c->identity)){
    if(g->role!=R2_GM5_HOT&&g->role!=R2_GM5_ACQUIRING){g->role=R2_GM5_ACQUIRING;g->selected=n->identity;gm5_attempt_start(n,R2_GM5_ACQUIRE_US);gm5_event(n,R2_GM5_SELECTED,c->identity);}
   }else if(g->role==R2_GM5_HOT||g->role==R2_GM5_ACQUIRING){gm5_grant_clear(n,R2_GM5_RETIRED);g->role=R2_GM5_COLD;gm5_attempt_clear(g);g->selected=0;gm5_set_root(n,255);gm5_event(n,R2_GM5_RETIRED,c->identity);}
  }
 }
 gm5_select(n);return 0;
}
