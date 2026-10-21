/* Config5. Single serialized owner; no allocation, callback work or stored keys. */
#include "network.h"
#include <errno.h>
#include <string.h>
#include "timing_events.h"
#if defined(R2_SERVICE_NORMAL)
#include <zephyr/sys/printk.h>
#endif
#if defined(R2_FIELD_FIXTURE)
#include "bs_tracing.h"
#endif
#if defined(R2_RC_DYNAMIC_FIXTURE)
extern bool r2_rc7_fixture_gm_pair(void);
#endif
static void count(uint32_t *v){if(*v!=UINT32_MAX)++*v;}
static uint64_t earlier(uint64_t a,uint64_t b){return a<b?a:b;}
/* G5 renews finite grants every2s. A5s coordination lease tolerates changing
 * acknowledgement latency without extending any original grant or source. */
static uint64_t coordination_lease(const struct r2_network *n)
{return n->gm5.enabled?R2_GM5_COORDINATION_US:R2_MANAGEMENT_LEASE_US;}
/* Mapping and source evidence retain their own original bounds. Management
 * heartbeat loss must not revoke a still-valid direct source permission. */
static bool bound(const struct r2_network *n,unsigned p)
{return p<n->count&&p!=n->self&&n->peer[p].connected&&n->peer[p].authenticated&&n->now>=n->peer[p].seen&&n->now-n->peer[p].seen<10000000;}
bool r2_network_member(const struct r2_network *n,unsigned p)
{return p<n->count&&p!=n->self&&n->peer[p].member&&n->now>=n->peer[p].member_at&&n->now-n->peer[p].member_at<R2_V14_MEMBERSHIP_US;}
bool r2_network_mesh_contact(const struct r2_network *n,uint64_t now)
{
 for(unsigned p=0;p<n->count;p++)if(p!=n->self&&n->peer[p].member&&
    now>=n->peer[p].member_at&&now-n->peer[p].member_at<R2_V14_MEMBERSHIP_US)return true;
 if(n->gm5.enabled)for(unsigned i=0;i<R2_GM5_CANDIDATES;i++){
  const struct r2_gm5_candidate *c=&n->gm5.candidate[i];
  /* at/until belongs to the actual authenticated STATUS sender, not a root
   * or selected identity reported by another node. No local attempt renews it. */
  if(c->identity&&c->identity!=n->identity&&c->boot&&now>=c->at&&now<c->until)return true;
 }
 return false;
}
static bool live(const struct r2_network *n,unsigned p)
{return bound(n,p)&&n->now>=n->peer[p].alive&&n->now-n->peer[p].alive<coordination_lease(n);}
/* Retaining an established partner is identity preference, not timing or
 * source permission. A rejected new map must not turn a live partner into
 * a failed identity; actual source/management/HOT gates still use bound(). */
static bool preference_live(const struct r2_network *n,unsigned p)
{return p<n->count&&p!=n->self&&n->peer[p].connected&&n->peer[p].authenticated&&!n->peer[p].source_withdrawn&&n->now>=n->peer[p].alive&&n->now-n->peer[p].alive<coordination_lease(n);}
static bool unresolved_join_candidate(const struct r2_network *n)
{
 if(!n->group.id)return false;
 /* JOIN authenticates identity/capability before HELLO has a causal map.
  * Do not turn that unknown peer source into an isolated-first-GM decision.
  * This holds only a new promotion. A previously mapped expired source, a
  * disconnected/expired identity bound, and an existing serving incumbent
  * retain their original election/expiry/takeover behavior. */
 for(unsigned p=0;p<n->count;p++)if(bound(n,p)&&n->peer[p].member&&
    (n->peer[p].capabilities&3)==3&&!n->peer[p].map_valid)return true;
 return false;
}
#if defined(R2_RC_DYNAMIC_FIXTURE)
static void pair_trace(const struct r2_network *n,unsigned why,unsigned root,unsigned standby){
 if(!r2_rc7_fixture_gm_pair()||(n->pair.root==root&&n->pair.standby==standby&&n->now<n->pair.until))return;
 unsigned r=n->pair.root,s=n->pair.standby;
 printk("GM_PAIR_RESERVATION id=%llx at=%llu why=%u old_root=%u old_standby=%u old_until=%llu new_root=%u new_standby=%u hot=%u\n",
 (unsigned long long)n->identity,(unsigned long long)n->now,why,r,s,(unsigned long long)n->pair.until,root,standby,n->hot_ready);
 printk("GM_PAIR_PARTNERS id=%llx at=%llu root_candidate_until=%llu standby_candidate_until=%llu root_alive=%llu standby_alive=%llu\n",
 (unsigned long long)n->identity,(unsigned long long)n->now,
 (unsigned long long)(r==n->self?n->candidate.valid_until_us:r<n->count?n->peer[r].candidate_until:0),
 (unsigned long long)(s==n->self?n->candidate.valid_until_us:s<n->count?n->peer[s].candidate_until:0),
 (unsigned long long)(r<n->count?n->peer[r].alive:0),(unsigned long long)(s<n->count?n->peer[s].alive:0));
 printk("GM_PAIR_RECOVERY id=%llx at=%llu root_authenticated=%u standby_authenticated=%u management_until=%llu authorized=%u takeover=%u not_before=%llu\n",
 (unsigned long long)n->identity,(unsigned long long)n->now,
 r==n->self||(r<n->count&&n->peer[r].authenticated),s==n->self||(s<n->count&&n->peer[s].authenticated),
 (unsigned long long)n->management_until,n->standby_authorized,n->takeover_pending,(unsigned long long)n->takeover_not_before);
}
#else
#define pair_trace(...) ((void)0)
#endif
static uint64_t mapped(uint64_t t,int64_t offset)
{return offset<0&&t<(uint64_t)-offset?0:(uint64_t)((int64_t)t+offset);}
/* Each remote instant grows its own conservative offset bound. A future
 * finite grant must not lend its longer drift distance to a short record TTL. */
static uint64_t map_drift(const struct r2_net_peer *m,uint64_t t,uint64_t now)
{
 uint64_t distance=now-m->seen;
 if(t){uint64_t d=t>m->map_anchor?t-m->map_anchor:m->map_anchor-t;if(d>distance)distance=d;}
 return distance/1000000*2000+(distance%1000000)*2000/1000000+100;
}
/* Pair IDs can be authenticated before its other partner is discovered. This
 * single bounded hint only withdraws output; it never admits a clock map or
 * gives an unknown identity source, management or HOT authority. */
static void discovery_hint(struct r2_network *n,unsigned p,const struct r2_auth_record *r,
                           const struct r2_group_pair_hint *h,int64_t lo)
{
 if(!n->group.id||!h)return;
 if(!h->until){if(n->discovery_sender==p)n->discovery_pair.until=0;return;}
 const struct r2_net_peer *m=&n->peer[p];
 bool root=m->identity==h->root,standby=m->identity==h->standby;
 if(!h->root||!h->standby||h->root==h->standby||!h->root_boot||!h->standby_boot||!h->epoch||
    (!root&&!standby)||(root?h->root_boot:h->standby_boot)!=r->boot||
    (root&&h->epoch!=r->source_epoch)||h->until<=r->created_us||h->until-r->created_us>R2_PAIR_RESERVE_US)return;
 uint64_t until=mapped(h->until,lo-(int64_t)((h->until-r->created_us)*2000/1000000+100));
 uint64_t source=mapped(r->permit_until_us,lo-(int64_t)((r->permit_until_us-r->created_us)*2000/1000000+100));
 until=earlier(until,source);if(until<=n->now)return;
 struct r2_group_pair_hint *old=&n->discovery_pair;
 if(old->until>n->now&&(old->root<h->root||(old->root==h->root&&old->standby<h->standby)))return;
 *old=*h;old->until=until;n->discovery_sender=p;n->discovery_sender_epoch=r->source_epoch;
#if defined(R2_RC_DYNAMIC_FIXTURE)
 if(r2_rc7_fixture_gm_pair())printk("GM_PAIR_HINT id=%llx peer=%llx at=%llu root=%llx standby=%llx root_boot=%llu standby_boot=%llu epoch=%u until=%llu map=%u\n",
  (unsigned long long)n->identity,(unsigned long long)m->identity,(unsigned long long)n->now,(unsigned long long)h->root,(unsigned long long)h->standby,(unsigned long long)h->root_boot,(unsigned long long)h->standby_boot,h->epoch,(unsigned long long)until,m->map_valid);
#endif
}
static bool discovery_current(const struct r2_network *n)
{
 const struct r2_group_pair_hint *h=&n->discovery_pair;unsigned p=n->discovery_sender;
 if(n->now>=h->until||p>=n->count||p==n->self)return false;
 const struct r2_net_peer *m=&n->peer[p];
 uint64_t boot=m->identity==h->root?h->root_boot:m->identity==h->standby?h->standby_boot:0;
 if(!m->connected||!m->authenticated||!boot||m->boot!=boot)return false;
 /* The sender epoch belongs to the independently validated hint, not its
  * accepted source map. A fresh wide new-epoch HELLO must remain able to
  * withdraw output while the old narrow source map stays unchanged. Current
  * session negative/epoch/nonce updates clear this hint in hello(). */
 for(unsigned i=0;i<n->count;i++){
  uint64_t id=i==n->self?n->identity:n->peer[i].identity;
  uint64_t known_boot=i==n->self?n->boot:n->peer[i].boot;
  if((id==h->root&&known_boot&&known_boot!=h->root_boot)||(id==h->standby&&known_boot&&known_boot!=h->standby_boot))return false;
  if(id==h->root&&i==n->self&&(!n->admission.eligible||n->candidate.generation!=h->epoch))return false;
  if(id==h->root&&i!=n->self&&i!=p&&n->peer[i].authenticated&&n->peer[i].map_valid&&
     (n->peer[i].source_withdrawn||n->peer[i].candidate_epoch!=h->epoch))return false;
 }
 return true;
}
/* Remember whom we yielded to without extending its expired HOT/source hint.
 * Only that directly authenticated primary's current identity activity can
 * inhibit a spare's self-promotion; its surviving HOT sender is insufficient. */
static bool discovery_primary_live(const struct r2_network *n)
{
 const struct r2_group_pair_hint *h=&n->discovery_pair;
 if(!h->root||h->root==n->identity||!h->root_boot)return false;
 for(unsigned p=0;p<n->count;p++)if(p!=n->self&&n->peer[p].identity==h->root)
  return n->peer[p].boot==h->root_boot&&preference_live(n,p);
 return false;
}
static bool clock_now(struct r2_network *n,uint64_t now){if(now<n->now||now>INT64_MAX/4){r2_network_stop(n,n->now);return false;}n->now=now;return !n->stopped;}
static void withdraw_hot(struct r2_network *n,uint32_t reason)
{
 if(!n->hot_ready)return;
 n->hot_ready=false;count(&n->hot_withdrawals);n->hot_withdrawal_at=n->now;
 n->hot_withdrawal_reason=r2_network_hot_refusal(n)|reason;
}
static void management_clear(struct r2_network *n)
{n->management_until=0;n->management_hash=0;n->management_pending_until=0;n->management_ready=0;n->management_committed=false;withdraw_hot(n,512);}
static void authority_clear(struct r2_network *n)
{n->ready=n->local_gm=n->follower_valid=false;n->view.available=false;n->service_until=n->service_acked_until=0;n->publication=0;management_clear(n);}
int r2_network_init(struct r2_network *n,uint8_t self,uint8_t nodes,uint64_t identity,uint64_t boot,uint64_t nonce,uint32_t bound,uint64_t now)
{
 if(!n||self>=nodes||nodes>R2_NET_PEERS||!identity||!boot||!nonce||!bound||bound>50000||now>INT64_MAX/4)return -EINVAL;
 memset(n,0,sizeof(*n));n->self=self;n->count=nodes;n->identity=identity;n->boot=boot;n->nonce=n->next_nonce=nonce;n->map_bound_us=bound;n->now=now;
 n->record_age_us=UINT32_MAX;
 n->pair.root=n->pair.standby=255;n->choice=n->coordinator=255;n->quality=(struct r2_clock_quality){128,248,254,128,65535,identity};
 n->profile_root=n->profile_client=255;n->gm5.next_poll=now;
 r2_admission_init(&n->admission,now);return 0;
}
void r2_network_capabilities(struct r2_network *n,uint8_t gm,uint8_t gnss){n->gm_mask=gm&((1u<<n->count)-1);n->gnss_mask=gnss&((1u<<n->count)-1);}
int r2_network_profile_root(struct r2_network *n,uint8_t root,uint8_t client,uint64_t now){
 if(root>=n->count||client>=n->count||root==client||n->profile_root!=255||!clock_now(n,now))return -EACCES;
 authority_clear(n);n->pair=(struct r2_pair){.root=255,.standby=255};
 n->profile_root=root;n->profile_client=client;n->fence=now+3000000;return 0;
}
void r2_network_timing(struct r2_network *n,uint64_t boot,uint32_t epoch,uint32_t train,bool gm,bool valid,uint64_t now)
{n->timing_relay=false;n->timing_boot=boot;n->timing_epoch=epoch;n->timing_train=train;n->timing_gm=gm;n->timing_valid=valid;n->timing_at=now;}
void r2_network_relay_timing(struct r2_network *n,uint8_t root,uint64_t boot,uint32_t epoch,uint32_t train,bool gm,bool valid,uint64_t now)
{
 r2_network_timing(n,boot,epoch,train,gm,valid&&root==n->choice&&root==n->profile_root&&train!=0,now);
 n->timing_relay=true;
}
static bool synced(const struct r2_network *n)
{return n->ready&&n->timing_valid&&n->timing_boot==n->root_boot&&n->timing_epoch==n->view.generation&&(n->timing_relay?n->profile_root==n->choice&&n->timing_train!=0:n->timing_train==n->publication)&&n->now>=n->timing_at&&n->now-n->timing_at<250000;}
void r2_network_link(struct r2_network *n,uint8_t p,bool connected,uint64_t now)
{
 if(p>=n->count||p==n->self||!clock_now(n,now))return;
 struct r2_net_peer *m=&n->peer[p];if(m->connected==connected)return;
 if(n->discovery_pair.root==m->identity)n->discovery_pair=(struct r2_group_pair_hint){0};
 else if(n->discovery_sender==p)n->discovery_pair.until=0;
 if(connected){if(n->next_nonce==UINT64_MAX){r2_network_stop(n,now);return;}m->own_nonce=++n->next_nonce;}
 n->management_ready&=~(1u<<p);
 m->connected=connected;m->authenticated=m->map_valid=false;m->seen=m->alive=m->candidate_until=0;m->hello_sequence=m->sequence=0;
 /* Identity/name membership survives a transport gap; session proof does not. */
 m->serving_until=0;m->serving_epoch=0;m->source_withdrawn=false;m->nonce=0;m->acked_permit=0;m->acked_publication=0;m->next_hello=0;m->hello_created=m->hello_received=0;m->response_pending=false;memset(m->sent,0,sizeof(m->sent));memset(m->issued,0,sizeof(m->issued));
 if(p==n->choice)authority_clear(n);
 if(p==n->coordinator)management_clear(n);
}
static struct r2_auth_record record(struct r2_network *n,unsigned p,unsigned kind)
{
 struct r2_auth_record r={.kind=kind,.sender=n->self,.target=p,.root=n->choice,.boot=n->boot,.coordinator_boot=n->root_boot,.root_boot=n->root_boot,.challenge=n->peer[p].nonce,.config_epoch=R2_AUTH_CONFIG,.created_us=n->now,.evidence_us=n->now,.expires_us=n->now+(n->gm5.enabled?R2_GM5_RECORD_US:R2_AUTH_ORIGINAL_TTL_US)};
 return r;
}
static void response(struct r2_network *n,unsigned p,const struct r2_auth_record *r,unsigned kind,uint64_t expiry)
{
 struct r2_auth_record a=record(n,p,kind);a.decision=r->decision;a.source_epoch=r->source_epoch;a.source_second=kind==R2_AUTH_ACK?r->sequence:r->source_second;
 if(n->gm5.enabled)a.expires_us=earlier(a.expires_us,expiry);
 a.coordinator_boot=r->coordinator_boot;a.flags=(kind==R2_AUTH_READY?n->ready:synced(n))?1:0;n->peer[p].response=a;n->peer[p].response_pending=true;
}
static bool causal_echo(const struct r2_net_peer *m,const struct r2_auth_record *r,uint64_t received)
{
 if(r->source_second>received||received-r->source_second>=500000)return false;
 for(unsigned i=0;i<4;i++)if(m->sent[i]&&m->sent[i]==r->source_second)return true;
 return false;
}
/* Expired authority can still carry evidence of past identity activity. Use
 * only its conservative creation lower bound, never receipt time. Replays
 * cannot advance that bound, and silence still expires at the original coordination deadline. This
 * does not accept the HELLO or renew any timing/source/pair permission. */
static void identity_activity(struct r2_network *n,unsigned p,const struct r2_auth_record *r,
                              int64_t lo,int64_t hi,uint64_t received)
{
 struct r2_net_peer *m=&n->peer[p];
 if(!m->map_valid||!m->authenticated||m->source_withdrawn||m->boot!=r->boot||
    m->nonce!=r->challenge||m->own_nonce!=r->decision||r->root_boot!=n->boot||
    r->root!=p||r->flags!=1||!m->candidate_epoch||m->candidate_epoch!=r->source_epoch||lo>hi)return;
 uint64_t at=mapped(r->created_us,lo);
 if(!at||at>received||n->now<at||n->now-at>=coordination_lease(n)||at<=m->alive)return;
 m->alive=at;if(p==n->choice&&at>n->root_health_at)n->root_health_at=at;
#if defined(R2_RC_DYNAMIC_FIXTURE)
 if(r2_rc7_fixture_gm_pair())printk("GM_PAIR_ACTIVITY id=%llx peer=%llx at=%llu activity=%llu lower_expiry=%llu\n",
  (unsigned long long)n->identity,(unsigned long long)m->identity,(unsigned long long)n->now,
  (unsigned long long)at,(unsigned long long)mapped(r->expires_us,lo));
#endif
}
/* Negative source/contact evidence survives ACL drain in the identity inventory.
 * Its creation watermark belongs to this remote boot, never our local clock.
 * A COLD fallback waits out every possibly issued15s grant before its ordinary
 *30..32s discovery round; repeated negatives can only shorten the original hold. */
static void gm5_negative(struct r2_network *n,struct r2_gm5_candidate *c,uint64_t created,uint32_t epoch)
{
 if(created<=c->withdraw_created)return;
 c->withdraw_created=created;c->withdraw_epoch=epoch?epoch:c->epoch;
 if(n->gm5.active==c->identity&&n->gm5.active_boot==c->boot){
  uint64_t until=n->now+R2_GM5_GRANT_US+R2_GM5_RETIRE_GUARD_US;
  if(n->gm5.active_until>until)n->gm5.active_until=until;
 }
}
static int hello(struct r2_network *n,unsigned p,const struct r2_auth_record *r,uint64_t received,const struct r2_group_pair_hint *hint)
{
 struct r2_net_peer *m=&n->peer[p];bool hint_session=m->authenticated&&m->boot==r->boot&&m->nonce==r->challenge&&r->decision==m->own_nonce&&r->root_boot==n->boot;bool candidate=r->root==p&&r->source_epoch&&r->permit_until_us>r->created_us&&r->permit_until_us-r->created_us<=R2_PPS_FRESH_US&&r->flags==1&&r->boot_age_ms>=60000&&r->source_age_ms>=60000;
 if(!candidate&&(r->root!=255||r->flags||r->source_epoch||r->permit_until_us))return -EACCES;
 if(candidate&&(!(n->gm_mask&(1u<<p))||!(n->gnss_mask&(1u<<p))))return -EACCES;
 if(r->coordinator_boot!=r->boot||!r->decision||!r->challenge)return -EACCES;
 if(m->authenticated&&m->boot!=r->boot&&r->decision!=m->own_nonce)return -ESTALE;
 if(m->boot==r->boot&&r->sequence<=m->hello_sequence)return -ESTALE;
 /* Without a fresh current-session causal proof, the accepted map must first
  * reject expired HELLOs. A very old map must not veto a fresher causal proof;
  * the coherent new interval still checks the same original TTL below. */
 if(m->map_valid&&m->authenticated&&m->boot==r->boot&&n->now>=m->seen&&!(hint_session&&causal_echo(m,r,received))){
  uint64_t distance=n->now-m->seen;
  uint64_t delta=r->expires_us>m->map_anchor?r->expires_us-m->map_anchor:m->map_anchor-r->expires_us;
  if(delta>distance)distance=delta;
  uint64_t growth=distance/1000000*2000+(distance%1000000)*2000/1000000+100;
  if(mapped(r->expires_us,m->offset_low-(int64_t)growth)<=n->now){
 if(candidate)identity_activity(n,p,r,m->offset_low-(int64_t)growth,m->offset_high+(int64_t)growth,received);
#if defined(R2_SERVICE_NORMAL)
 if(n->now>=230000000&&n->now<234000000)printk("R2_REJECT_HELLO_MAP self=%u peer=%u now=%llu created=%llu expiry=%llu offset=%lld growth=%llu\n",n->self,p,(unsigned long long)n->now,(unsigned long long)r->created_us,(unsigned long long)r->expires_us,(long long)m->offset_low,(unsigned long long)growth);
#endif
 return -ESTALE;}
 }
 if(m->authenticated&&m->boot!=r->boot){if(n->next_nonce==UINT64_MAX){r2_network_stop(n,n->now);return -EOVERFLOW;}m->own_nonce=++n->next_nonce;memset(m->sent,0,sizeof(m->sent));}
 bool changed=m->boot!=r->boot||m->nonce!=r->challenge;
 if(n->freshness_root==m->identity&&(m->boot!=r->boot||(candidate&&r->source_epoch!=n->freshness_epoch)))n->freshness=(struct r2_join_time){0};
 if(changed){if(n->discovery_pair.root==m->identity)n->discovery_pair=(struct r2_group_pair_hint){0};else if(n->discovery_sender==p)n->discovery_pair.until=0;m->serving_until=0;m->serving_epoch=0;m->source_withdrawn=false;if(m->boot!=r->boot)m->member=false;m->authenticated=m->map_valid=false;m->candidate_until=0;m->sequence=m->hello_sequence=0;m->response_pending=false;if(p==n->choice)authority_clear(n);if(p==n->coordinator)management_clear(n);}
 if(m->boot!=r->boot)m->retired_publication=0;
 m->boot=r->boot;m->nonce=r->challenge;m->hello_sequence=r->sequence;m->hello_created=r->created_us;m->hello_received=received;
 if(r->decision!=m->own_nonce||r->root_boot!=n->boot)return 0;
 bool causal=causal_echo(m,r,received);
 if(!causal&&!m->map_valid)return 0;
 /* Established identity/source health uses its current causal interval.
  * It does not require a new echoed clock map on every heartbeat. The mapped
  * original TTL check above already rejected expired current-session data. */
 uint64_t guard=causal?(received-r->source_second)*2000/1000000+100:0;
 uint64_t map_age=r->created_us>=m->map_anchor?r->created_us-m->map_anchor:0;
 uint64_t map_growth=map_age/1000000*2000+(map_age%1000000)*2000/1000000+100;
 int64_t lo=causal?(int64_t)r->source_second-(int64_t)r->evidence_us-(int64_t)guard:m->offset_low-(int64_t)map_growth;
 int64_t hi=causal?(int64_t)received-(int64_t)r->created_us+(int64_t)guard:m->offset_high+(int64_t)map_growth;
 if(m->map_valid&&m->authenticated&&r->created_us>=m->map_anchor&&received>=m->seen&&received-m->seen<10000000){uint64_t elapsed=r->created_us-m->map_anchor,growth=elapsed/1000000*2000+(elapsed%1000000)*2000/1000000+100;int64_t old_lo=m->offset_low-(int64_t)growth,old_hi=m->offset_high+(int64_t)growth;if(lo<old_lo)lo=old_lo;if(hi>old_hi)hi=old_hi;}
 if(lo>hi)return 0;
 if(mapped(r->created_us,lo)>received)return -ESTALE;
 /* Even a wide causal interval can safely withdraw local output, but only
  * while the original HELLO and independent-source/pair permits are live.
  * The80ms accepted map bound below remains unchanged. */
 uint64_t ttl_guard=(r->expires_us-r->created_us)*2000/1000000+100;
 if(mapped(r->expires_us,lo-(int64_t)ttl_guard)<=n->now){
  if(candidate)identity_activity(n,p,r,lo-(int64_t)ttl_guard,hi,received);
  return -ESTALE;
 }
#if defined(R2_RC_DYNAMIC_FIXTURE)
 if(r2_rc7_fixture_gm_pair()&&n->now>=110000000&&n->now<180000000)
  printk("GM_PAIR_MAP id=%llx peer=%llx at=%llu causal=%u width=%llu before=%u seen=%llu lower_expiry=%llu echo_age=%llu\n",(unsigned long long)n->identity,(unsigned long long)m->identity,(unsigned long long)n->now,causal,(unsigned long long)(hi-lo),m->map_valid,(unsigned long long)m->seen,(unsigned long long)mapped(r->expires_us,lo-(int64_t)ttl_guard),(unsigned long long)(received>=r->source_second?received-r->source_second:UINT64_MAX));
#endif
 if(hint_session&&(causal||m->map_valid)){
  /* Causal/current-map freshness proves liveness and negative withdrawal
   * independently of accepting a new timing map. No positive source permit,
   * offset/seen, management reservation or HOT readiness is granted here. */
  if(n->gm5.enabled&&!candidate)for(unsigned i=0;i<R2_GM5_CANDIDATES;i++){
   struct r2_gm5_candidate *c=&n->gm5.candidate[i];
   if(c->identity==m->identity&&c->boot==r->boot){gm5_negative(n,c,r->created_us,c->epoch);c->physical_at=n->now;c->source_age_ms=r->source_age_ms;c->eligible=r->boot_age_ms>=60000&&r->source_age_ms>=60000;c->inhibited=c->eligible;c->physical_until=0;}
  }
  m->source_withdrawn=!candidate;
  if(!candidate){m->candidate_until=0;m->candidate_epoch=0;}
  if(candidate&&causal)m->alive=received;
  if(!candidate&&n->discovery_pair.root==m->identity)n->discovery_pair=(struct r2_group_pair_hint){0};
  else if((n->discovery_sender==p&&(!candidate||n->discovery_sender_epoch!=r->source_epoch))||
     (n->discovery_pair.root==m->identity&&(!candidate||n->discovery_pair.epoch!=r->source_epoch)))n->discovery_pair.until=0;
  if(candidate)discovery_hint(n,p,r,hint,lo);
 }
 if(causal&&hi-lo>(int64_t)n->map_bound_us*2){
  count(&n->hello_map_refusals);
  n->hello_width_us=hi-lo>UINT32_MAX?UINT32_MAX:(uint32_t)(hi-lo);
  return 0;
 }
 m->authenticated=true;m->alive=received;if(causal){m->map_valid=true;m->seen=received;m->offset_low=lo;m->offset_high=hi;m->map_anchor=r->created_us;}count(&n->hello_rx);
 uint64_t expiry_guard=(candidate?r->permit_until_us-r->created_us:0)*2000/1000000+100;
 m->candidate_until=candidate?mapped(r->permit_until_us,lo-(int64_t)expiry_guard):0;m->candidate_epoch=candidate?r->source_epoch:0;m->source_withdrawn=!candidate;
 if(!candidate||m->serving_epoch!=m->candidate_epoch)m->serving_until=0;
 if(r->pair_until_us){
  unsigned root=r->pair_root,standby=r->pair_standby;
  if(root>=n->count||standby>=n->count||root==standby||(p!=root&&p!=standby)||
     !r->pair_epoch||r->pair_until_us<=r->created_us||r->pair_until_us-r->created_us>R2_PAIR_RESERVE_US)return -EACCES;
  if(!(n->gm_mask&(1u<<root))||!(n->gnss_mask&(1u<<root))||!(n->gm_mask&(1u<<standby))||!(n->gnss_mask&(1u<<standby))){
   /* Dynamic discovery can learn an established partner before the other
    * partner. Authenticate this HELLO without adopting its unresolved pair
    * hint; otherwise capability learning deadlocks on the hint itself. */
   if(n->group.id)goto quality;
   return -EACCES;
  }
  uint64_t rb=root==n->self?n->boot:n->peer[root].boot,sb=standby==n->self?n->boot:n->peer[standby].boot;
  uint64_t reserve=mapped(r->pair_until_us,lo-(int64_t)((r->pair_until_us-r->created_us)*2000/1000000+100));
  /* A reservation echo cannot resurrect an explicitly withdrawn source.
   * Retain missing-message reservations; this is only current-incarnation
   * authenticated negative evidence (or our own invalid source). */
  bool withdrawn=root==n->self?!n->admission.eligible:
   (n->peer[root].authenticated&&n->peer[root].source_withdrawn);
  if(withdrawn&&rb==r->pair_root_boot)goto quality;
  if(rb==r->pair_root_boot&&sb==r->pair_standby_boot&&reserve>n->now){
   bool same=n->pair.root==root&&n->pair.standby==standby&&n->pair.root_boot==rb&&n->pair.standby_boot==sb&&n->pair.epoch==r->pair_epoch;
   /* Two established partitions merge deterministically by root identity, then
    * standby identity. An unestablished newcomer never has a competing claim. */
   uint64_t root_id=root==n->self?n->identity:n->peer[root].identity;
   uint64_t old_id=n->pair.root==n->self?n->identity:n->pair.root<n->count?n->peer[n->pair.root].identity:UINT64_MAX;
   uint64_t sid=standby==n->self?n->identity:n->peer[standby].identity;
   uint64_t old_sid=n->pair.standby==n->self?n->identity:n->pair.standby<n->count?n->peer[n->pair.standby].identity:UINT64_MAX;
   if(same||n->now>=n->pair.until||root_id<old_id||(root_id==old_id&&sid<old_sid))
   {
    pair_trace(n,1,root,standby);n->pair=(struct r2_pair){rb,sb,reserve,r->pair_epoch,root,standby};
    /* Incumbent preference is local to the connected domain, not a global
     * serving lease. Independent partitions may retain their own source. */
   }
  }
 }
quality:
 if(n->gm5.enabled){for(unsigned i=0;i<R2_GM5_CANDIDATES;i++)if(n->gm5.candidate[i].identity==m->identity){
  struct r2_gm5_candidate *c=&n->gm5.candidate[i];
  if(c->boot==m->boot){if(n->gm5.active==m->identity&&n->gm5.active_boot==m->boot&&candidate)n->gm5.active_until=n->now+R2_GM5_STATUS_US;
   c->physical_at=n->now;c->physical_until=m->candidate_until;c->source_age_ms=r->source_age_ms;c->eligible=r->boot_age_ms>=60000&&r->source_age_ms>=60000;c->inhibited=c->eligible&&!candidate;if(candidate){c->epoch=r->source_epoch;if(r->created_us>c->withdraw_created)c->withdraw_epoch=0;}}
 }}
 m->quality=(struct r2_clock_quality){r->priority1,r->clock_class,r->accuracy,r->priority2,r->variance,m->identity};
 return 0;
}
static void identities(const struct r2_network *n,uint64_t out[4]){
 for(unsigned p=0;p<4;p++)out[p]=p==n->self?n->identity:p<n->count?n->peer[p].identity:0;
}
static int encode_control(const struct r2_network *n,uint8_t *wire,const struct r2_auth_record *r,const uint8_t key[16]){
 if(!n->group.id)return r2_auth_encode(wire,r,key);
 uint64_t ids[4];identities(n,ids);return n->gm5.enabled?r2_group_control_encode5(wire,r,&n->group,ids,n->name,n->capabilities&3):r2_group_control_encode14(wire,r,&n->group,ids,n->name,n->capabilities&3);
}
static int decode_control(const struct r2_network *n,struct r2_auth_record *r,const uint8_t *wire,size_t size,unsigned sender,const uint8_t key[16],char name[25],uint8_t *caps,struct r2_group_pair_hint *hint){
 if(hint)*hint=(struct r2_group_pair_hint){0};
 if(!n->group.id)return r2_auth_decode(r,wire,size,key);
 uint64_t ids[4];identities(n,ids);return n->gm5.enabled?r2_group_control_decode5(r,wire,size,&n->group,ids,ids[sender],name,caps,hint):r2_group_control_decode14_hint(r,wire,size,&n->group,ids,ids[sender],name,caps,hint);
}
static int gm5_management_receive(struct r2_network *,unsigned,const struct r2_auth_record *,uint64_t,uint64_t);
static uint64_t descriptor(const struct r2_network *n)
{
 uint64_t h=R2_FIXED_SCHEDULE^n->root_boot^((uint64_t)n->view.generation<<32)^n->publication;
 if(n->group.id){
  unsigned order[2]={n->choice,n->coordinator};
  uint64_t ids[4];identities(n,ids);
  if(order[0]<4&&order[1]<4&&ids[order[0]]>ids[order[1]]){unsigned t=order[0];order[0]=order[1];order[1]=t;}
  for(unsigned i=0;i<2;i++){unsigned p=order[i];if(p>=4||(i&&p==order[0]))continue;uint64_t b=p==n->self?n->boot:n->peer[p].boot;h=(h^(b+ids[p]))*UINT64_C(1099511628211);}
 }else{
  for(unsigned p=0;p<n->count;p++)if(p==n->choice||p==n->coordinator){uint64_t b=p==n->self?n->boot:n->peer[p].boot;h=(h^(b+p))*UINT64_C(1099511628211);}
 }
 if(n->gm5.enabled&&n->coordinator<n->count){uint32_t epoch=n->coordinator==n->self?n->candidate.generation:n->peer[n->coordinator].candidate_epoch;h=(h^epoch)*UINT64_C(1099511628211);}
 return h?h:1;
}
int r2_network_receive(struct r2_network *n,uint8_t p,const uint8_t *data,size_t size,uint64_t received,uint64_t now)
{
 if(!n||p>=n->count||p==n->self||!clock_now(n,now))return -EINVAL;
 struct r2_net_peer *m=&n->peer[p];struct r2_auth_record r;char name[25]={0};uint8_t caps=0;struct r2_group_pair_hint hint;int rc=decode_control(n,&r,data,size,p,m->rx_key,name,&caps,&hint);
 if(rc||!m->connected||r.sender!=p||r.target!=n->self||r.created_us>INT64_MAX/4||received>now||now-received>=500000){count(&n->rejected);return -EACCES;}
 if(n->gm5.enabled)count(&n->gm5.control_rx);
 if(r.kind==R2_AUTH_HELLO){
  uint8_t old_gm=n->gm_mask,old_gnss=n->gnss_mask;
  if(n->group.id){
   if(caps&1)n->gm_mask|=1u<<p;else n->gm_mask&=~(1u<<p);
   if(caps&2)n->gnss_mask|=1u<<p;else n->gnss_mask&=~(1u<<p);
  }
  rc=hello(n,p,&r,received,&hint);
#if defined(R2_RC_DYNAMIC_FIXTURE)
  if(r2_rc7_fixture_gm_pair()&&((now>=61000000&&now<68000000)||(now>=89000000&&now<150000000)))
   {printk("GM_PAIR_CAUSE id=%llx peer=%llx at=%llu rc=%d map=%u auth=%u candidate_until=%llu serving_until=%llu\n",(unsigned long long)n->identity,(unsigned long long)m->identity,(unsigned long long)now,rc,m->map_valid,m->authenticated,(unsigned long long)m->candidate_until,(unsigned long long)m->serving_until);
    printk("GM_PAIR_CLAIM id=%llx peer=%llx at=%llu flags=%u epoch=%u permit=%llu created=%llu age=%u boot_age=%u withdrawn=%u\n",(unsigned long long)n->identity,(unsigned long long)m->identity,(unsigned long long)now,r.flags,r.source_epoch,(unsigned long long)r.permit_until_us,(unsigned long long)r.created_us,r.source_age_ms,r.boot_age_ms,m->source_withdrawn);}
#endif
#if defined(R2_FIELD_FIXTURE)
  if(now<2000000)bs_trace_raw(0,"R2_HELLO self=%u rc=%d auth=%u nonce=%llu own=%llu decision=%llu boot=%llu rootboot=%llu echo=%llu received=%llu created=%llu evidence=%llu\n",n->self,rc,m->authenticated,(unsigned long long)m->nonce,(unsigned long long)m->own_nonce,(unsigned long long)r.decision,(unsigned long long)r.boot,(unsigned long long)r.root_boot,(unsigned long long)r.source_second,(unsigned long long)received,(unsigned long long)r.created_us,(unsigned long long)r.evidence_us);
#endif
  if(!rc&&m->authenticated&&m->alive==received){m->member=true;m->member_at=received;if(p==n->choice)n->root_health_at=received;}
  if(!rc&&m->authenticated&&n->group.id){memcpy(m->name,name,sizeof m->name);m->capabilities=caps;}
  else if(n->group.id){n->gm_mask=old_gm;n->gnss_mask=old_gnss;}
  if(rc){count(&n->rejected);}
  return rc;
 }
 if(!bound(n,p)||!m->map_valid||r.boot!=m->boot||r.challenge!=m->own_nonce||r.sequence<=m->sequence){
#if defined(R2_SERVICE_NORMAL)
 if((now>=230000000&&now<234000000)||(now>=502000000&&now<508000000))printk("R2_REJECT_BOUND self=%u peer=%u bound=%u boot=%u challenge=%u seq=%llu last=%llu seen=%llu\n",n->self,p,bound(n,p),r.boot==m->boot,r.challenge==m->own_nonce,(unsigned long long)r.sequence,(unsigned long long)m->sequence,(unsigned long long)m->seen);
#endif
 count(&n->rejected);return -ESTALE;}
 if(r.created_us>INT64_MAX/4||r.evidence_us>INT64_MAX/4||r.expires_us>INT64_MAX/4||r.permit_until_us>INT64_MAX/4)return -ERANGE;
 uint64_t drift=map_drift(m,r.evidence_us,now);int64_t lo=m->offset_low-(int64_t)map_drift(m,r.expires_us,now);
 uint64_t expiry=mapped(r.expires_us,lo),evidence=mapped(r.evidence_us,m->offset_low-(int64_t)drift),permit=r.permit_until_us?mapped(r.permit_until_us,m->offset_low-(int64_t)map_drift(m,r.permit_until_us,now)):0;
 uint64_t created=mapped(r.created_us,m->offset_low-(int64_t)map_drift(m,r.created_us,now)),age=now>=created?now-created:0;
 n->record_age_us=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;
 if(age>n->record_age_max_us)n->record_age_max_us=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;
 if(expiry<=now||evidence>received){
#if defined(R2_SERVICE_NORMAL)
 if((now>=230000000&&now<234000000)||(now>=502000000&&now<508000000))printk("R2_REJECT_MAP self=%u peer=%u now=%llu seq=%llu created=%llu expiry=%llu evidence=%llu offset=%lld seen=%llu anchor=%llu\n",n->self,p,(unsigned long long)now,(unsigned long long)r.sequence,(unsigned long long)r.created_us,(unsigned long long)expiry,(unsigned long long)evidence,(long long)lo,(unsigned long long)m->seen,(unsigned long long)m->map_anchor);
#endif
 count(&n->rejected);return -ESTALE;}
 m->sequence=r.sequence;
 if(r.kind==R2_AUTH_SERVICE){
  if(r.root!=p||r.root_boot!=m->boot||r.coordinator_boot!=m->boot||!r.decision||r.decision>UINT32_MAX||!(r.flags&1)||(r.flags&~5u)||r.source_epoch!=m->candidate_epoch||now>=m->candidate_until||permit<=now||r.permit_until_us<=r.created_us||r.permit_until_us-r.created_us>R2_PPS_FRESH_US||r.source_second<946684800||r.source_second>=4102444800){count(&n->rejected);return -EACCES;}
  if(r.decision<m->retired_publication||(p==n->choice&&n->publication&&r.decision<n->publication)){count(&n->rejected);return -ESTALE;}
  /* A nonselected authenticated source may prove incumbent service. This
   * evidence participates only in election; it never installs a time map. */
  m->serving_until=permit;m->serving_epoch=r.source_epoch;
  if(p!=n->choice){m->retired_publication=(uint32_t)r.decision;return 0;}
  m->retired_publication=(uint32_t)r.decision;n->publication=(uint32_t)r.decision;n->view=(struct v12_dc_coarse){.available=true,.relative_ready=!!(r.flags&4),.generation=r.source_epoch,.utc_second=r.source_second,.evidence_us=evidence,.valid_until_us=permit};
  /* HELLO authenticated the causal local/remote offset interval. An accepted
   * SERVICE binds UTC to its evidence instant; preserve the WHOLE interval,
   * not its arrival time. Further hold grows via the same JOIN anchor rule. */
  uint64_t upper_evidence=mapped(r.evidence_us,m->offset_high+(int64_t)drift);
  if(upper_evidence>=evidence&&upper_evidence-evidence<200000&&
     r.source_second*1000000>=upper_evidence&&UINT64_MAX-(r.source_second*1000000-upper_evidence)>=now){
   /* An interval's upper edge can lie just after receipt. That is uncertainty,
    * not noncausality: project its lower UTC endpoint with checked subtraction.
    * Refusing such first SERVICEs stranded coarse freshness once sparse ACK
    * delivery correctly suppressed redundant retries of the same permit. */
   uint64_t width=upper_evidence-evidence,utc=r.source_second*1000000-upper_evidence+now;
   uint64_t uncertainty=(width+1)/2+n->map_bound_us+100;
   if(uncertainty<100000){
   n->freshness=(struct r2_join_time){.utc_us=utc+width/2,.until=permit,.uncertainty_us=(uint32_t)uncertainty};
   n->freshness_at=now;n->freshness_root=m->identity;n->freshness_boot=r.root_boot;n->freshness_epoch=r.source_epoch;
   }
  }
  m->candidate_until=permit;n->service_until=permit;n->root_boot=r.root_boot;count(&n->service_receipts);response(n,p,&r,R2_AUTH_ACK,expiry);
 }else if(r.kind==R2_AUTH_ACK){
  if(!n->local_gm||r.root!=n->self||r.root_boot!=n->boot||r.coordinator_boot!=n->boot||r.source_epoch!=n->candidate.generation||r.decision!=n->publication)return -ESTALE;
  bool issued=false;for(unsigned i=0;i<R2_ISSUED_RECORDS;i++)if(m->issued[i].sequence==r.source_second&&m->issued[i].publication==r.decision&&now<m->issued[i].expiry){issued=true;
   /* The exchange must arrive before its original record expiry. Its
    * delivery evidence lasts only through the immutable source permit it
    * acknowledged, not through the transport record's acceptance deadline. */
   if(m->issued[i].permit==n->service_until||m->acked_publication!=m->issued[i].publication||(m->acked_permit!=n->service_until&&m->acked_permit<m->issued[i].permit))m->acked_permit=m->issued[i].permit;
   m->acked_publication=m->issued[i].publication;
   if(n->service_acked_until<m->acked_permit)n->service_acked_until=m->acked_permit;}
  if(!issued)return -ESTALE;
 }else if(n->gm5.enabled){
  int managed=gm5_management_receive(n,p,&r,permit,expiry);if(managed)return managed;
 }else{
  if(!live(n,p)||!n->ready||n->coordinator>=n->count||r.root!=n->choice||r.root_boot!=n->root_boot||r.source_epoch!=n->view.generation||r.source_second!=descriptor(n)||!r.decision||r.decision>UINT32_MAX)return -ESTALE;
  uint64_t cb=n->coordinator==n->self?n->boot:n->peer[n->coordinator].boot;if(r.coordinator_boot!=cb)return -ESTALE;
  if(r.kind==R2_AUTH_PREPARE&&p==n->coordinator){
   if(r.decision<n->management_generation)return -ESTALE;
   n->management_generation=(uint32_t)r.decision;n->management_hash=r.source_second;n->management_pending_until=expiry;response(n,p,&r,R2_AUTH_READY,expiry);
  }else if(r.kind==R2_AUTH_READY&&n->coordinator==n->self&&r.decision==n->management_generation&&now<n->management_pending_until){
   if(r.flags&1)n->management_ready|=1u<<p;
  }else if(r.kind==R2_AUTH_COMMIT&&p==n->coordinator&&r.decision>=n->management_generation&&r.flags==1){
   if(permit<=now||r.permit_until_us-r.created_us>coordination_lease(n))return -ESTALE;
   /* A complete fresh commit repairs a locally cleared handshake. The common
    * checks above bind the authenticated current coordinator boot, root boot,
    * source epoch and complete current descriptor; original TTL/lease and
    * monotonic receive sequence still apply. Local source/map readiness is
    * independently required for HOT. Requiring a remembered PREPARE here can
    * strand a receiver after its READY reached the coordinator just before
    * a transient local role clear. No new RF retries or stale proof extension. */
   n->management_generation=(uint32_t)r.decision;n->management_hash=r.source_second;
   n->management_until=permit;if(synced(n))n->hot_ready=true;else withdraw_hot(n,32);n->management_committed=true;count(&n->management_commits);
  }else return -ESTALE;
 }
 m->alive=received;m->member=true;m->member_at=received;
 if(p==n->choice)n->root_health_at=received;
 count(&n->authenticated_rx);return 0;
}
static struct v12_dc_coarse local_source(struct r2_network *n,const struct r2_clock *clock,uint64_t now)
{
  struct v12_dc_coarse source={0};
  if(clock->view.source_available&&clock->last_pair_edge>=0&&clock->last_pair_utc>0){
    uint64_t expires=(uint64_t)clock->last_pair_edge+R2_PPS_FRESH_US;
    uint64_t gps=(uint64_t)clock->last_rmc+R2_GPS_FRESH_US;if(gps<expires)expires=gps;
    source=(struct v12_dc_coarse){.available=now<expires,.qualified=false,
      .utc_second=(uint64_t)clock->last_pair_utc/1000000,.evidence_us=(uint64_t)clock->last_pair_edge,
      .valid_until_us=expires,.generation=clock->view.source_epoch};
  }else if(clock->view.valid&&clock->view.fix_active&&clock->view.pps_present&&
           !clock->stopped&&!clock->fatal&&clock->last_rmc>=0&&clock->last_pps>=0&&
           clock->last_rmc_utc>0&&clock->last_rmc>=clock->last_rmc_utc%1000000){
    /* Functional GPS coarse service is independent of uncommissioned PPS/UTC
     * association. Anchor UTC to original UART arrival, never label a PPS edge
     * with an inferred second. Every receipt remains explicitly unqualified. */
    uint64_t expires=(uint64_t)clock->last_rmc+R2_GPS_FRESH_US;
    uint64_t pps=(uint64_t)clock->last_pps+R2_PPS_FRESH_US;if(pps<expires)expires=pps;
    source=(struct v12_dc_coarse){.available=now<expires,.qualified=false,
      /* Normalize fractional RMC into the existing whole-second/map pair.
       * Backdating the map anchor preserves the exact coarse UTC value and
       * conservatively increases age. Never round away fractional time or
       * extend the original GPS/PPS expiry. This is not PPS association. */
      .utc_second=(uint64_t)clock->last_rmc_utc/1000000,
      .evidence_us=(uint64_t)(clock->last_rmc-clock->last_rmc_utc%1000000),
      .valid_until_us=expires,.generation=clock->view.source_epoch};
  }
  /* Only the time owner supplies this snapshot. Keep original eligibility and
   * expiry above, but publish the same UTC interpretation as the fine stream. */
  if(source.available&&clock->translated_epoch==source.generation&&clock->translated_epoch&&
     clock->translated_at_us>=0&&now>=(uint64_t)clock->translated_at_us&&clock->translated_utc_us>=0){
    uint64_t utc=(uint64_t)clock->translated_utc_us+now-(uint64_t)clock->translated_at_us;
    if(now>=utc%1000000){source.utc_second=utc/1000000;source.evidence_us=now-utc%1000000;}
  }
  source.relative_ready=source.available&&n->relative_source;
  return source;
}
bool r2_network_coordination_direct(const struct r2_network *n,const struct r2_gm_coordination *c,uint64_t now)
{
 if(!r2_gm_coordination_live(c,n->identity,now)||!c->boot)return false;
 /* Only the serialized BLE owner may set this after current direct JOIN and
  * same-root physical PA map proof. LoRa publication always leaves it false. */
 if(c->direct)return true;
 for(unsigned p=0;p<n->count;p++){
  const struct r2_net_peer *m=&n->peer[p];
  if(p!=n->self&&c->root==m->identity&&c->boot==m->boot&&
     (c->epoch==m->candidate_epoch||(m->source_withdrawn&&!m->candidate_epoch))&&
     m->connected&&m->authenticated&&m->member&&m->map_valid&&
     (m->capabilities&3)==3&&now>=m->seen&&now-m->seen<10000000&&
     now>=m->alive&&now-m->alive<coordination_lease(n)&&
     now>=m->member_at&&now-m->member_at<R2_V14_MEMBERSHIP_US)return true;
 }
 return false;
}

static uint32_t milliseconds(uint64_t us);
#include "gm_standby.inc"

void r2_network_tick(struct r2_network *n,const struct r2_clock *clock,uint64_t now)
{r2_network_tick_coordinated(n,clock,now,NULL);}
void r2_network_tick_coordinated(struct r2_network *n,const struct r2_clock *clock,uint64_t now,const struct r2_gm_coordination *coord)
{
 if(!clock_now(n,now))return;
 bool constrained=r2_gm_coordination_live(coord,n->identity,now);
 bool direct=constrained&&r2_network_coordination_direct(n,coord,now);
 uint64_t elapsed=n->follower_at?now-n->follower_at:0;
 if(n->follower_at){n->follower_utc+=elapsed;n->follower_at=now;}
 uint64_t previous_source_until=n->candidate.valid_until_us;
 uint32_t previous_source_epoch=n->candidate.generation;
 n->candidate=local_source(n,clock,now);
 bool physical=r2_admission_update(&n->admission,(n->gm5.enabled||!constrained||direct)&&(n->gm5.enabled||n->profile_root==255||n->self!=n->profile_client)&&(n->gm_mask&(1u<<n->self)),n->gnss_mask&(1u<<n->self),n->candidate.available,n->candidate.generation,n->candidate.valid_until_us,now);
 bool own=physical&&(!constrained||direct);
 if(own&&(previous_source_until!=n->candidate.valid_until_us||previous_source_epoch!=n->candidate.generation)){
  /* Newly available source evidence is change-driven; never wait behind the
   * sparse liveness cadence near the old1.5s original permission deadline. */
  for(unsigned p=0;p<n->count;p++)if(n->peer[p].connected)n->peer[p].next_hello=0;
 }
 if(n->gm5.enabled){n->gm5.negative_root=constrained?coord->root:0;n->gm5.negative_boot=constrained?coord->boot:0;n->gm5.negative_epoch=constrained?coord->epoch:0;gm5_tick(n,physical,now,coord);return;}
 if(constrained&&!direct){
  n->standby_authorized=n->takeover_pending=false;
  n->pair=(struct r2_pair){.root=255,.standby=255};
 }
 /* Prior HOT authorization is a separate recovery fact, not current HOT UI.
  * A precision gap cannot fabricate root failure. Only accepted fresh direct
  * root liveness advances root_health_at, even when a new map is too wide.
  * Pair reservation + existing fence
  * still delay serving after the 2s suspicion point. */
 if(n->standby_authorized){
  if(!own||n->candidate.generation!=n->standby_source_epoch||
     (n->root_boot!=n->standby_root_boot&&!(n->takeover_pending&&n->choice==n->self))){
   n->standby_authorized=n->takeover_pending=false;
  }else if(n->takeover_pending&&n->choice==n->self){
   /* Retain only the prior HOT takeover already fenced against its old root.
    * An unrelated unresolved JOIN cannot revoke that recovery authorization. */
  }else if((bound(n,n->choice)&&n->peer[n->choice].source_withdrawn)||
    (n->root_health_at&&now>=n->root_health_at&&now-n->root_health_at>=coordination_lease(n))){
   /* Explicit authenticated withdrawal is different from lost heartbeats.
    * The same3s output fence still outlasts the original1.5s source permit. */
   if(!n->takeover_pending){n->takeover_pending=true;n->takeover_started=now;
    /* Prior HOT permits early local reselection, not global exclusivity.
     * The ordinary topology fence below still precedes replacement output. */
    n->takeover_not_before=r2_sat_add(now,3000000);
    if(n->pair.until>now)n->pair.until=now;}
  }else n->takeover_pending=false;
 }
 struct r2_ranked_clock ranks[4]={0};uint8_t members=0;
 for(unsigned p=0;p<n->count;p++){
  bool self=p==n->self,connected=self||bound(n,p);
  ranks[p]=(struct r2_ranked_clock){.quality=self?n->quality:n->peer[p].quality,.boot=self?n->boot:n->peer[p].boot,.wire=p,.gm_capable=!!(n->gm_mask&(1u<<p)),.gnss_capable=!!(n->gnss_mask&(1u<<p)),.independent_ready=self?own:now<n->peer[p].candidate_until,.authenticated=connected};
  if(p==n->profile_client)ranks[p].independent_ready=false;
  if((self||live(n,p))&&ranks[p].gm_capable&&ranks[p].gnss_capable&&ranks[p].independent_ready)members|=1u<<p;
 }
 struct r2_elected_roles roles=r2_election_rank(ranks,n->count);
 if(n->profile_root<n->count){
  unsigned p=n->profile_root;bool eligible=ranks[p].gm_capable&&ranks[p].gnss_capable&&ranks[p].independent_ready&&ranks[p].authenticated;
  roles.gm=eligible?p:255;
  struct r2_ranked_clock rest[4];memcpy(rest,ranks,sizeof rest);rest[p].independent_ready=false;
  roles.coordinator=eligible?r2_election_rank(rest,n->count).gm:255;
  if(eligible&&roles.coordinator==255)roles.coordinator=p;
  roles.standby=eligible&&roles.coordinator!=p?roles.coordinator:255;
 }
 /* Preserve actual serving incumbents before a HOT pair exists. Merely
  * qualifying a candidate does not make it an incumbent. Independent serving
  * partitions still converge by stable identity, as established pairs do. */
 unsigned serving_incumbent=255;
 if(n->profile_root==255){
  unsigned incumbent=255;uint64_t best=UINT64_MAX;
  for(unsigned p=0;p<n->count;p++){
   bool serving=p==n->self?(n->local_gm&&n->ready&&own&&now<n->service_until):
    (ranks[p].authenticated&&ranks[p].independent_ready&&now<n->peer[p].serving_until&&n->peer[p].serving_epoch==n->peer[p].candidate_epoch);
   if(serving&&ranks[p].quality.identity<best){incumbent=p;best=ranks[p].quality.identity;}
  }
  serving_incumbent=incumbent;
  /* Authenticated public reception fences self-promotion while the direct
   * link is being admitted. It does not confer control authority on a peer. */
  bool observed=n->observed_root&&n->observed_root!=n->identity&&now<n->observed_until;
  if(incumbent==255&&observed){
   for(unsigned p=0;p<n->count;p++)if(p!=n->self&&n->peer[p].identity==n->observed_root&&
    n->peer[p].boot==n->observed_boot&&ranks[p].authenticated&&ranks[p].independent_ready&&
    n->peer[p].candidate_epoch==n->observed_epoch){incumbent=p;break;}
   if(incumbent==255)roles=(struct r2_elected_roles){255,255,255};
  }
  if(incumbent<n->count){
   roles.gm=incumbent;struct r2_ranked_clock rest[4];memcpy(rest,ranks,sizeof rest);
   rest[incumbent].independent_ready=false;
   roles.coordinator=roles.standby=r2_election_rank(rest,n->count).gm;
  }
 }
 if(n->profile_root==255&&n->pair.root<n->count&&n->pair.standby<n->count){
  unsigned pr=n->pair.root,ps=n->pair.standby;
  bool root_current=ranks[pr].boot==n->pair.root_boot;
  bool standby_current=ranks[ps].boot==n->pair.standby_boot;
  bool root_ok=root_current&&ranks[pr].gm_capable&&ranks[pr].gnss_capable&&(pr==n->self?own:preference_live(n,pr));
  bool standby_ok=standby_current&&ranks[ps].gm_capable&&ranks[ps].gnss_capable&&(ps==n->self?own:preference_live(n,ps));
  if(now<n->pair.until&&root_current&&standby_current){roles.gm=pr;roles.coordinator=roles.standby=ps;}
  else{
   /* Past HOT preference cannot split two actual serving partitions after
    * its reservation expires. A fresh authenticated lower-ID serving claim
    * uses the same ordinary incumbent tie; a passive spare does not. */
   bool serving_preempts=serving_incumbent<n->count&&serving_incumbent!=pr&&
     ranks[serving_incumbent].quality.identity<ranks[pr].quality.identity;
   /* Surviving partner keeps precedence after reservation expires or a reboot
    * fences the old incarnation. A still-qualified established partner keeps
    * its place; timing-permission expiration alone cannot replace its identity
    * with a newcomer. Negative source, boot, disconnect or liveness loss releases
    * preference. This never renews an expired reservation or source permit. */
   if((root_ok||standby_ok)&&!serving_preempts){
    roles.gm=root_ok?pr:ps;
    if(root_ok&&standby_ok)roles.coordinator=roles.standby=ps;
    else{
     struct r2_ranked_clock rest[4];memcpy(rest,ranks,sizeof rest);rest[roles.gm].independent_ready=false;
     roles.coordinator=roles.standby=r2_election_rank(rest,n->count).gm;
    }
   }
   /* Eligibility can preserve role preference, never manufacture a fresh
    * established pair. Only the actual HOT commit below can create one.
    * Until then an expired unchanged reservation stays expired; changed roles
    * clear it rather than advertising an unproved replacement as established. */
   if(roles.gm!=pr||roles.coordinator!=ps||!(root_ok||standby_ok)){
    pair_trace(n,2,roles.gm,roles.coordinator);
    n->pair=(struct r2_pair){.root=255,.standby=255};
   }
  }
 }

 if(!discovery_current(n))n->discovery_pair.until=0;
 bool remembered_primary=discovery_primary_live(n);
 if(n->profile_root==255&&(n->discovery_pair.until>now||remembered_primary)){
  const struct r2_group_pair_hint *h=&n->discovery_pair;
  bool actual=n->pair.until>now&&n->pair.root<n->count&&n->pair.standby<n->count;
  uint64_t old_root=actual?ranks[n->pair.root].quality.identity:UINT64_MAX;
  uint64_t old_standby=actual?ranks[n->pair.standby].quality.identity:UINT64_MAX;
  bool same=actual&&old_root==h->root&&old_standby==h->standby&&
    n->pair.root_boot==h->root_boot&&n->pair.standby_boot==h->standby_boot&&n->pair.epoch==h->epoch;
  if(!same&&actual&&(h->root>old_root||(h->root==old_root&&h->standby>old_standby))){
   /* An ignored losing hint is not a history of yielding to that root. */
   n->discovery_pair=(struct r2_group_pair_hint){0};remembered_primary=false;
  }else if(n->discovery_pair.until>now&&!same&&(!actual||h->root<old_root||(h->root==old_root&&h->standby<old_standby))){
   /* A paired incumbent precedes an unpaired newcomer regardless of its ID.
    * Only an independently admitted known root can become our control root.
    * Unknown standby identity never becomes our coordinator/HOT partner. */
   roles=(struct r2_elected_roles){255,255,255};
   for(unsigned p=0;p<n->count;p++)if(p!=n->self&&ranks[p].quality.identity==h->root&&
      ranks[p].boot==h->root_boot&&ranks[p].authenticated&&ranks[p].independent_ready&&n->peer[p].candidate_epoch==h->epoch)roles.gm=p;
  }
 }
 /* Timing expiry alone must not restart a previously yielded spare. Keep
  * existing primary choices and live HOT reservations; suppress only a new
  * self-promotion, never mint timing/management/HOT authority from memory. */
 if(n->profile_root==255&&remembered_primary&&roles.gm==n->self&&
    !(n->pair.until>now&&n->pair.root==n->self)){
  roles=(struct r2_elected_roles){255,255,255};
  for(unsigned p=0;p<n->count;p++)if(p!=n->self&&ranks[p].quality.identity==n->discovery_pair.root&&
     ranks[p].boot==n->discovery_pair.root_boot&&ranks[p].authenticated&&ranks[p].independent_ready)roles.gm=p;
 }

 /* Last election fence: quality, old pair/discovery preference, explicit test
  * profiles and prior HOT takeover cannot select another root. A direct peer
  * still proves every existing source/service/management/timing permission.
  * No synthetic peer, boot, map or service receipt is installed from LoRa. */
 if(constrained){
  bool replacement=false;uint64_t retired_until=0;
  if(direct&&roles.gm==n->self)for(unsigned p=0;p<n->count;p++){
   const struct r2_net_peer *m=&n->peer[p];
   if(p!=n->self&&m->identity==coord->root&&m->boot==coord->boot&&
      m->source_withdrawn&&!m->candidate_epoch&&m->connected&&m->authenticated&&
      now>=m->alive&&now-m->alive<coordination_lease(n)){
    replacement=true;retired_until=m->serving_until;break;
   }
  }
  /* Current DIRECT authenticated explicit withdrawal may complete ordinary
   * coordinated replacement. Silence/expired maps alone never grant it. The
   * old original service permit and existing topology/takeover fences still
   * precede local output, even if this node previously served a partition. */
  if(replacement){if(n->fence<retired_until)n->fence=retired_until;}
  else{
   struct r2_elected_roles selected=roles;
   roles=(struct r2_elected_roles){255,255,255};
   if(direct)for(unsigned p=0;p<n->count;p++)if(p!=n->self&&
    r2_gm_coordination_matches(coord,n->peer[p].identity,n->peer[p].boot,n->peer[p].candidate_epoch)&&
    ranks[p].authenticated&&ranks[p].independent_ready&&ranks[p].gm_capable&&ranks[p].gnss_capable){
    /* LoRa constrains the root, not its already selected HOT partner. The
     * ordinary policy above owns pair reservation, identity preference and
     * partner replacement. Re-ranking here made a capable newcomer clear an
     * established same-root HOT handshake on every constrained follower. */
    if(selected.gm==p)roles=selected;
    else{
     roles.gm=p;struct r2_ranked_clock rest[4];memcpy(rest,ranks,sizeof rest);
     rest[p].independent_ready=false;
     roles.coordinator=roles.standby=r2_election_rank(rest,n->count).gm;
    }
    break;
   }
  }
 }
 uint64_t root=roles.gm==255?0:roles.gm==n->self?n->boot:n->peer[roles.gm].boot;
 uint32_t epoch=roles.gm==255?0:roles.gm==n->self?n->candidate.generation:n->peer[roles.gm].candidate_epoch;
 if(roles.gm!=n->choice||root!=n->root_boot||(n->view.available&&epoch!=n->view.generation)){
  #if defined(R2_SERVICE_NORMAL)
  if(n->choice<n->count&&n->choice!=n->self){const struct r2_net_peer *old=&n->peer[n->choice];
   printk("R2_AUTHORITY_CHANGE self=%u now=%llu old=%u new=%u old_boot=%llu new_boot=%llu connected=%u auth=%u alive=%llu seen=%llu candidate_until=%llu service_until=%llu old_epoch=%u new_epoch=%u\n",n->self,(unsigned long long)now,n->choice,roles.gm,(unsigned long long)n->root_boot,(unsigned long long)root,old->connected,old->authenticated,(unsigned long long)old->alive,(unsigned long long)old->seen,(unsigned long long)old->candidate_until,(unsigned long long)n->service_until,n->view.generation,epoch);
  }
#endif
  if(roles.gm<n->count&&n->freshness.utc_us&&(n->freshness_root!=ranks[roles.gm].quality.identity||n->freshness_boot!=root||n->freshness_epoch!=epoch))n->freshness=(struct r2_join_time){0};
  authority_clear(n);n->choice=roles.gm;n->root_boot=root;n->fence=now+3000000;count(&n->topologies);
 }
 uint64_t cb=roles.coordinator==255?0:roles.coordinator==n->self?n->boot:n->peer[roles.coordinator].boot;
 if(roles.coordinator!=n->coordinator||cb!=n->coordinator_boot){
  management_clear(n);n->management_generation=0;n->coordinator=roles.coordinator;n->coordinator_boot=cb;
 }
 if(n->takeover_pending&&roles.gm==n->self&&n->fence<n->takeover_not_before)n->fence=n->takeover_not_before;
 n->members_mask=members;
 if(roles.gm==n->self&&own&&now>=n->fence&&
    (n->local_gm||n->takeover_pending||!unresolved_join_candidate(n))){
  if(!n->local_gm){if(n->next_publication==UINT32_MAX){r2_network_stop(n,now);return;}n->publication=++n->next_publication;}
  if(n->view.valid_until_us!=n->candidate.valid_until_us||n->view.generation!=n->candidate.generation)
   for(unsigned p=0;p<n->count;p++)n->peer[p].next_service=0;
  n->local_gm=n->ready=true;n->view=n->candidate;n->service_until=n->candidate.valid_until_us;
  /* Lost liveness releases inhibition immediately, but a primary returning
   * during the original promotion fence must still find our yield history.
   * Discard it only when this legitimate local promotion actually completes. */
  if(!remembered_primary)n->discovery_pair=(struct r2_group_pair_hint){0};
 }else if(roles.gm==n->self){n->ready=n->local_gm=false;n->view.available=false;
 }else if(roles.gm!=n->self){
  n->local_gm=false;n->ready=roles.gm!=255&&now>=n->fence&&n->view.available&&n->publication&&now<n->service_until&&n->view.generation==epoch;
 }
 n->follower_valid=n->ready&&!n->local_gm;
 if(n->follower_valid){
  uint64_t target=n->view.utc_second*1000000+now-n->view.evidence_us;
  if(!n->follower_at)n->follower_utc=target;
  else{uint64_t limit=elapsed/200;if(target>n->follower_utc)n->follower_utc+=earlier(target-n->follower_utc,limit);else n->follower_utc-=earlier(n->follower_utc-target,limit);}
  n->follower_at=n->follower_last_good=now;n->follower_until=n->service_until;count(&n->follower_updates);
 }
 n->follower_clock_valid=n->follower_last_good&&now-n->follower_last_good<R2_HOLDOVER_US;
 if(!n->ready){management_clear(n);return;}
 if(n->management_hash&&n->management_hash!=descriptor(n))management_clear(n);
 bool pair_source=n->choice<n->count&&n->coordinator<n->count&&n->choice!=n->coordinator&&
  ranks[n->choice].independent_ready&&ranks[n->coordinator].independent_ready&&
  ranks[n->choice].authenticated&&ranks[n->coordinator].authenticated;
 if(now>=n->management_until||!pair_source||!synced(n))withdraw_hot(n,0);
 if(n->coordinator==n->self){
  if(!pair_source){withdraw_hot(n,0);n->management_committed=false;}
  if(!n->management_hash||(!n->management_committed&&now>=n->management_pending_until)){
   if(n->management_generation==UINT32_MAX){r2_network_stop(n,now);return;}
   ++n->management_generation;n->management_hash=descriptor(n);n->management_pending_until=now+coordination_lease(n);n->management_ready=0;
   for(unsigned p=0;p<n->count;p++)n->peer[p].next_management=0;
  }
  if(pair_source)n->management_ready|=1u<<n->self;else n->management_ready&=~(1u<<n->self);
  uint8_t required=(1u<<n->choice)|(1u<<n->coordinator);
  if((n->management_ready&required)==required&&pair_source){
   n->hot_ready=synced(n);n->management_committed=true;n->management_until=now+coordination_lease(n);
   n->management_pending_until=n->management_until;
   if(n->hot_ready){pair_trace(n,3,n->choice,n->self);n->pair=(struct r2_pair){n->root_boot,n->boot,now+R2_PAIR_RESERVE_US,n->view.generation,n->choice,n->self};}
  }
 }

 if(n->hot_ready&&n->coordinator==n->self&&n->choice!=n->self&&own){
  n->standby_authorized=true;n->standby_root_boot=n->root_boot;
  n->standby_source_epoch=n->candidate.generation;
 }
}
void r2_network_snapshot(const struct r2_network *n,struct v12_dc_snapshot *s)
{
 memset(s,0,sizeof(*s));s->self_wire=n->self;s->coordinator_wire=n->coordinator;s->coordinator=n->coordinator==n->self;
 s->elected_root=n->choice;s->root_boot=n->root_boot;s->hot_standby=n->hot_ready?n->coordinator:255;
 s->config_epoch=R2_AUTH_CONFIG;s->decision=n->publication;s->last_now_us=n->now;s->coarse=n->view;
 s->role_until_us=s->view_until_us=s->service_until_us=n->service_until;s->local_gm_ready=n->ready&&n->local_gm;
 s->functional_service=n->follower_valid;
 for(unsigned p=0;p<n->count;p++)if(s->local_gm_ready&&bound(n,p)&&n->peer[p].acked_publication==n->publication&&n->now<n->peer[p].acked_permit)s->serving=true;
 s->relative_precision_permitted=n->ready&&n->view.relative_ready;s->running=!n->stopped;s->authenticated=n->ready;
 s->role=s->local_gm_ready?V12_DC_CONTROL_GM:n->follower_valid?(s->hot_standby==n->self?V12_DC_STANDBY:V12_DC_FOLLOWER):V12_DC_CLIENT;
 s->phase=n->ready?V12_DC_RUNNING:V12_DC_IDLE;s->accepted=n->authenticated_rx;s->refused=n->rejected;s->elections=n->topologies;
 s->commits=n->management_commits;s->service_receipts=n->service_receipts;s->service_peer=n->choice;
 for(unsigned p=0;p<n->count;p++)if(n->members_mask&(1u<<p))++s->members;
}
static uint32_t milliseconds(uint64_t us){return us/1000>UINT32_MAX?UINT32_MAX:(uint32_t)(us/1000);}
bool r2_network_next_ready(struct r2_network *n,uint8_t available,uint8_t *peer,uint8_t *bytes,bool *is_hello)
{
 if(n->stopped)return false;
 if(n->gm5.enabled)return gm5_next(n,available,peer,bytes,is_hello);
 for(unsigned i=0;i<n->count;i++){
  unsigned p=(n->next_peer+i)%n->count;
  if(p==n->self||!(available&(1u<<p))||!n->peer[p].connected)continue;
  struct r2_net_peer *m=&n->peer[p];struct r2_auth_record r;bool h=false;
  /* Source permission gets priority, but cannot starve an overdue HELLO.
   * Without that bound a congested retry stream can expire candidacy and
   * make peers reject otherwise-fresh SERVICE from the established source. */
  if(bound(n,p)&&n->ready&&n->local_gm&&n->now>=m->next_service&&
     (m->acked_publication!=n->publication||m->acked_permit!=n->service_until)&&
     (n->now<m->next_hello||n->now-m->next_hello<50000)){
   r=record(n,p,R2_AUTH_SERVICE);r.decision=n->publication;r.source_epoch=n->view.generation;r.source_second=n->view.utc_second;r.evidence_us=n->view.evidence_us;
   r.permit_until_us=n->view.valid_until_us;r.flags=1|(n->view.relative_ready?4:0);
  }else if(bound(n,p)&&m->response_pending&&m->response.kind==R2_AUTH_ACK&&n->now<m->response.expires_us){r=m->response;}
  else if(n->now>=m->next_hello){
   if(n->hello_sequence==UINT64_MAX){r2_network_stop(n,n->now);return false;}
   r=record(n,p,R2_AUTH_HELLO);r.root=255;r.coordinator_boot=n->boot;r.challenge=m->own_nonce;r.sequence=++n->hello_sequence;
   r.decision=m->nonce?m->nonce:1;r.root_boot=m->boot;r.source_second=m->hello_created;r.evidence_us=m->hello_received?m->hello_received:n->now;
   r.priority1=n->quality.priority1;r.clock_class=n->quality.clock_class;r.accuracy=n->quality.accuracy;r.priority2=n->quality.priority2;r.variance=n->quality.variance;
   r.boot_age_ms=milliseconds(n->now-n->admission.boot_at);r.source_age_ms=n->admission.observing?milliseconds(n->now-n->admission.stable_at):0;
   if(n->admission.eligible&&n->candidate.available&&n->now<n->candidate.valid_until_us){r.root=n->self;r.source_epoch=n->candidate.generation;r.permit_until_us=n->candidate.valid_until_us;r.flags=1;}
   if(n->pair.until>n->now&&(n->self==n->pair.root||n->self==n->pair.standby)){
    r.pair_root=n->pair.root;r.pair_standby=n->pair.standby;r.pair_root_boot=n->pair.root_boot;r.pair_standby_boot=n->pair.standby_boot;r.pair_epoch=n->pair.epoch;r.pair_until_us=n->pair.until;
   }
   h=true;
  }else if(!bound(n,p))continue;
  else if(m->response_pending&&n->now<m->response.expires_us){r=m->response;}
  else if(n->ready&&n->coordinator==n->self&&n->now<n->management_pending_until&&n->now>=m->next_management){
   bool committed=n->management_committed&&(n->management_ready&(1u<<p));
   r=record(n,p,committed?R2_AUTH_COMMIT:R2_AUTH_PREPARE);r.coordinator_boot=n->boot;r.decision=n->management_generation;r.source_epoch=n->view.generation;r.source_second=n->management_hash;r.flags=committed?1:0;
   r.expires_us=earlier(r.expires_us,n->management_pending_until);
   if(committed)r.permit_until_us=n->management_until;
  }else continue;
  if(!h){if(n->tx_sequence==UINT64_MAX){r2_network_stop(n,n->now);return false;}r.sequence=++n->tx_sequence;}
  if(encode_control(n,bytes,&r,m->tx_key))return false;
  n->next_peer=(p+1)%n->count;*peer=p;*is_hello=h;return true;
 }
 return false;
}
bool r2_network_next(struct r2_network *n,uint8_t *p,uint8_t *bytes,bool *h){return r2_network_next_ready(n,255,p,bytes,h);}
void r2_network_sent(struct r2_network *n,bool h,uint8_t p,const uint8_t *bytes)
{
 if(p>=n->count)return;
 struct r2_auth_record r;char name[25];uint8_t caps;struct r2_net_peer *m=&n->peer[p];if(decode_control(n,&r,bytes,r2_control_wire_size(bytes),n->self,m->tx_key,name,&caps,NULL))return;
 if(n->gm5.enabled){
  count(&n->gm5.control_tx);
  /* Accepted traffic advances exactly one class in the four-class order. */
  if(h)n->next_peer=2;
  else if(r.kind==R2_AUTH_SERVICE)n->next_peer=3;
  else if(r.kind==R2_AUTH_COMMIT||r.kind==R2_AUTH_PREPARE)n->next_peer=0;
  else n->next_peer=1;
 }
 if(h){m->next_hello=n->now+(m->authenticated&&n->now>=m->seen&&n->now-m->seen<1000000?(n->gm5.enabled?R2_GM5_PROBE_US:400000):200000);m->sent[m->sent_head++%4]=r.created_us;}
 else if(r.kind==R2_AUTH_SERVICE){unsigned i=m->issued_head++%R2_ISSUED_RECORDS;m->issued[i].sequence=r.sequence;m->issued[i].expiry=r.expires_us;m->issued[i].permit=r.permit_until_us;m->issued[i].publication=(uint32_t)r.decision;
   /* G5 uses the shared probe cadence; legacy remains100ms. Neither
    * retries nor responses renew original record/source expiry. */
   m->next_service=n->now+(n->gm5.enabled?R2_GM5_PROBE_US:100000);}
 else if(r.kind==R2_AUTH_ACK||r.kind==R2_AUTH_READY)m->response_pending=false;
 else m->next_management=n->now+(n->gm5.enabled?R2_GM5_PROBE_US:(r.kind==R2_AUTH_COMMIT?500000:100000));
}
void r2_network_stop(struct r2_network *n,uint64_t now){n->now=now;authority_clear(n);n->stopped=true;n->follower_clock_valid=false;}

/* Best-effort storage must not start on the tail of healthy source permission.
 * For a GM every currently bound recipient must have acknowledged this original
 * permit, not merely an earlier SERVICE from the same publication. */
bool r2_network_storage_ready(const struct r2_network *n)
{
 if(!n->ready||n->stopped)return true;
 if(n->service_until<=n->now||n->service_until-n->now<1000000)return false;
 /* A follower must enqueue its pending source ACK before yielding radio
  * opportunities to an erase. Transport completion is checked by storage. */
 if(n->follower_valid&&n->choice<n->count&&n->peer[n->choice].response_pending&&
    n->peer[n->choice].response.kind==R2_AUTH_ACK)return false;
 if(n->local_gm)for(unsigned p=0;p<n->count;p++)if(bound(n,p)&&
  (n->peer[p].acked_publication!=n->publication||n->peer[p].acked_permit<n->service_until)){
#if defined(R2_SERVICE_NORMAL)
   static uint64_t report;
   if(((n->now>=80000000&&n->now<84000000)||(n->now>=142000000&&n->now<144000000))&&n->now>=report){report=n->now+100000;printk("R2_STORE_WAIT self=%u peer=%u t=%llu pub=%u ackpub=%u permit=%llu ackpermit=%llu alive=%llu\n",n->self,p,(unsigned long long)n->now,n->publication,n->peer[p].acked_publication,(unsigned long long)n->service_until,(unsigned long long)n->peer[p].acked_permit,(unsigned long long)n->peer[p].alive);}
#endif
   return false;
  }
 return true;
}

uint32_t r2_network_hot_refusal(const struct r2_network *n)
{
 if(n->hot_ready)return 0;
 uint32_t why=0;unsigned pair[2]={n->choice,n->coordinator};
 if(pair[0]>=n->count||pair[1]>=n->count)why|=1;
 if(!n->ready||n->now>=n->service_until)why|=2;
 for(unsigned i=0;i<2;i++){
  unsigned p=pair[i];if(p>=n->count)continue;
  bool fresh=p==n->self?n->admission.eligible&&n->candidate.available&&n->now<n->candidate.valid_until_us:n->now<n->peer[p].candidate_until;
  if(!fresh)why|=4u<<i;
  if(p!=n->self&&!bound(n,p))why|=16;
 }
 if(!synced(n))why|=32;
 if(n->now>=n->management_until)why|=64;
 if(!n->management_committed)why|=128;
 return why?why:256;
}

void r2_network_joined(struct r2_network *n,unsigned p,const struct r2_join_exchange *e,uint64_t now){
 if(!n||!e||p>=n->count||p==n->self||!e->accepted||!e->control||!e->peer_boot||!e->nonce||!e->remote_nonce)return;
 if(n->discovery_sender==p)n->discovery_pair.until=0;
 struct r2_net_peer *m=&n->peer[p];memset(m,0,sizeof *m);
 m->identity=e->peer;m->boot=e->peer_boot;m->own_nonce=e->nonce;m->nonce=e->remote_nonce;
 m->connected=m->authenticated=m->member=true;m->seen=m->alive=m->member_at=now;
 m->capabilities=e->peer_capabilities;memcpy(m->name,e->name,sizeof m->name);
 r2_group_pair_key(m->tx_key,&n->group,n->identity,e->peer);r2_group_pair_key(m->rx_key,&n->group,e->peer,n->identity);
 /* HELLO still needs a causal echo before any offset or source claim is used. */
 m->map_valid=false;
}
