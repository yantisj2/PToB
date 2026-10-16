/* SPDX-License-Identifier: Apache-2.0 */
#include "rc_fixed.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#define PUT r2_group_put
#define GET r2_group_get
uint32_t rc4_bound_add(uint32_t a,uint32_t b){return a==RC4_UNKNOWN||b==RC4_UNKNOWN||a>=RC4_UNKNOWN-b?RC4_UNKNOWN:a+b;}
bool rc4_routes_valid(const struct rc4_routes *r){
 if(!r||r->count<2||r->count>RC4_NODES||!r->node[0].transmitter||r->node[0].slot||r->node[0].depth||r->node[0].parent)return false;
 unsigned slots=1;
 for(unsigned i=0;i<r->count;i++){
  const struct rc4_node *n=&r->node[i];if(!n->id)return false;
  for(unsigned j=0;j<i;j++)if(n->id==r->node[j].id)return false;
  if(!i)continue;
  if(n->parent>=i||!r->node[n->parent].transmitter)return false;
  if(n->transmitter){
   if(n->depth!=r->node[n->parent].depth+1||n->depth>2||!n->slot||n->slot>3||n->slot<=r->node[n->parent].slot||(slots&(1u<<n->slot)))return false;
   slots|=1u<<n->slot;
  }else if(n->slot!=255||n->depth!=r->node[n->parent].depth)return false;
 }
 return true;
}
static int route_tag(uint8_t out[16],const struct rc4_routes *r,const struct r2_group_config *g){
 if(!rc4_routes_valid(r))return -EINVAL;
 uint8_t body[1+12*RC4_NODES]={0};body[0]=r->count;
 for(unsigned i=0;i<r->count;i++){uint8_t *p=body+1+12*i;PUT(p,r->node[i].id,8);p[8]=r->node[i].parent;p[9]=r->node[i].slot;p[10]=r->node[i].depth;p[11]=r->node[i].transmitter;}
 return r2_group_mac(out,g,"RC4ROUTE",body,sizeof body);
}
static bool valid_origin(const struct rc4_origin *o,const struct rc4_routes *r){
 return o->root==r->node[0].id&&o->boot&&o->epoch&&o->expires>o->issued&&o->expires-o->issued<=RC4_SOURCE_NS&&o->frame<=o->issued&&(!o->hold_us)==(!o->hold_ns)&&o->hold_us<=10000000&&o->hold_ns!=RC4_UNKNOWN;
}
static void origin_write(uint8_t *p,const struct rc4_origin *o){
 PUT(p,o->root,8);PUT(p+8,o->boot,8);PUT(p+16,o->epoch,4);PUT(p+20,o->issued,8);PUT(p+28,o->expires,8);PUT(p+36,(uint64_t)o->utc_offset,8);PUT(p+44,o->frame,8);PUT(p+52,o->hold_us,4);PUT(p+56,o->hold_ns,4);
}
static void origin_read(struct rc4_origin *o,const uint8_t *p){
 *o=(struct rc4_origin){.root=GET(p,8),.boot=GET(p+8,8),.epoch=GET(p+16,4),.issued=GET(p+20,8),.expires=GET(p+28,8),.utc_offset=(int64_t)GET(p+36,8),.frame=GET(p+44,8),.hold_us=GET(p+52,4),.hold_ns=GET(p+56,4)};
}
int rc4_origin_sign(uint8_t proof[16],const struct rc4_origin *o,const struct rc4_routes *r,const struct r2_group_config *g){
 uint8_t body[68],tag[16];if(!proof||!o||route_tag(tag,r,g)||!valid_origin(o,r))return -EINVAL;
 memcpy(body,tag,8);origin_write(body+8,o);return r2_group_mac(proof,g,"RC4ORIGN",body,sizeof body);
}
static bool same_origin(const struct rc4_origin *a,const struct rc4_origin *b){uint8_t x[60],y[60];origin_write(x,a);origin_write(y,b);return !memcmp(x,y,sizeof x);}
static bool packet_valid(const struct rc4_packet *p,const struct rc4_routes *r){
 if(!p||!rc4_routes_valid(r)||p->sender>=r->count||!r->node[p->sender].transmitter||!valid_origin(&p->origin,r)||!p->boot||!p->train||!p->publication||!p->epoch||p->count>2)return false;
 for(unsigned i=0;i<p->count;i++){
  const struct rc4_correction *c=&p->correction[i];
  if(!c->publication||c->publication>=p->publication||c->channel>36||c->root_ns<p->origin.issued||c->root_ns>=p->origin.expires||c->tx_ns<c->root_ns||c->tx_ns>=p->origin.expires||c->tx_ns-c->root_ns>=RC4_REFERENCE_NS)return false;
  if(i&&c->publication==p->correction[0].publication&&c->event==p->correction[0].event&&c->channel==p->correction[0].channel)return false;
 }
 return true;
}
int rc4_encode(uint8_t ad[RC4_BYTES],const struct rc4_packet *p,const struct rc4_routes *r,const struct r2_group_config *g){
 uint8_t tag[16],proof[16];if(!ad||!packet_valid(p,r)||route_tag(tag,r,g)||rc4_origin_sign(proof,&p->origin,r,g)||memcmp(proof,p->proof,16))return -EINVAL;
 memset(ad,0,RC4_BYTES);ad[0]=189;ad[1]=ad[2]=ad[3]=255;ad[4]='R';ad[5]='C';ad[6]=4;ad[7]=p->count;PUT(ad+8,g->epoch,4);origin_write(ad+12,&p->origin);memcpy(ad+72,p->proof,16);
 memcpy(ad+88,tag,8);ad[96]=p->sender;ad[97]=(r->node[p->sender].depth<<4)|r->node[p->sender].slot;PUT(ad+98,p->boot,8);PUT(ad+106,p->train,4);PUT(ad+110,p->publication,4);PUT(ad+114,p->epoch,4);
 for(unsigned i=0;i<p->count;i++){uint8_t *w=ad+118+27*i;const struct rc4_correction *c=&p->correction[i];PUT(w,c->publication,4);PUT(w+4,c->event,2);w[6]=c->channel;PUT(w+7,c->root_ns,8);PUT(w+15,c->tx_ns,8);PUT(w+23,c->bound,4);}
 return r2_group_mac(ad+174,g,"RC4PACKT",ad,174);
}
int rc4_decode(struct rc4_packet *p,const uint8_t *ad,size_t n,const struct rc4_routes *r,const struct r2_group_config *g){
 uint8_t tag[16],proof[16];if(!p||!ad||n!=RC4_BYTES||route_tag(tag,r,g)||ad[0]!=189||ad[1]!=255||ad[2]!=255||ad[3]!=255||ad[4]!='R'||ad[5]!='C'||ad[6]!=4||ad[7]>2||GET(ad+8,4)!=g->epoch||memcmp(tag,ad+88,8)||ad[172]||ad[173])return -EINVAL;
 if(!r2_group_verify(ad+174,g,"RC4PACKT",ad,174))return -EACCES;
 memset(p,0,sizeof *p);origin_read(&p->origin,ad+12);memcpy(p->proof,ad+72,16);p->sender=ad[96];p->count=ad[7];p->boot=GET(ad+98,8);p->train=GET(ad+106,4);p->publication=GET(ad+110,4);p->epoch=GET(ad+114,4);
 if(p->sender>=r->count||ad[97]!=((r->node[p->sender].depth<<4)|r->node[p->sender].slot)||rc4_origin_sign(proof,&p->origin,r,g)||memcmp(proof,p->proof,16))return -EACCES;
 for(unsigned i=0;i<2;i++){const uint8_t *w=ad+118+27*i;if(i>=p->count){for(unsigned j=0;j<27;j++)if(w[j])return -EINVAL;continue;}
  p->correction[i]=(struct rc4_correction){.publication=GET(w,4),.event=GET(w+4,2),.channel=w[6],.root_ns=GET(w+7,8),.tx_ns=GET(w+15,8),.bound=GET(w+23,4)};}
 return packet_valid(p,r)?0:-EINVAL;
}
void rc4_init(struct rc4_state *s,const struct rc4_routes *r,unsigned self){memset(s,0,sizeof *s);if(rc4_routes_valid(r)&&self<r->count){s->routes=*r;s->self=self;}}
void rc4_withdraw(struct rc4_state *s){memset(s->pending,0,sizeof s->pending);memset(&s->reference,0,sizeof s->reference);}
/* Declared conservative local oscillator envelope, separate from learned rate.
 * 100us comparison guard covers coarse per-link capture/propagation reference
 * uncertainty only. Physical use must establish this bound independently. */
bool rc4_coarse_interval(const struct rc4_state *s,uint64_t tick,uint64_t *out_lo,uint64_t *out_hi){
 if(!s||!s->bound||!out_lo||!out_hi)return false;
 const struct rc4_admission *a=&s->admission;
 uint64_t delta=tick>=a->now_tick?tick-a->now_tick:a->now_tick-tick;
 if(delta>RC4_CONTEXT_TICKS)return false;
 uint64_t nominal=delta*125/2;
 uint64_t low=(nominal/1000300000)*1000000000+(nominal%1000300000)*1000000000/1000300000;
 uint64_t high=(nominal/999700000)*1000000000+((nominal%999700000)*1000000000+999699999)/999700000;
 uint64_t lo=a->root_lo_ns,hi=a->root_hi_ns;
 if(tick>=a->now_tick){if(hi>UINT64_MAX-high)return false;lo+=low;hi+=high;}
 else {lo=lo>high?lo-high:0;if(hi<low)return false;hi-=low;}
 *out_lo=lo;*out_hi=hi;return true;
}
static bool fresh_time(const struct rc4_state *s,uint64_t tick,uint64_t ns){
 uint64_t lo,hi;if(!rc4_coarse_interval(s,tick,&lo,&hi))return false;
 lo=lo>100000?lo-100000:0;if(hi>UINT64_MAX-100000)return false;hi+=100000;
 return ns>=lo&&ns<=hi;
}
static bool source_deadline(const struct rc4_state *s,uint64_t expires,uint64_t now,uint64_t *until){
 uint64_t lo,hi;if(!rc4_coarse_interval(s,now,&lo,&hi)||expires<=hi)return false;
 uint64_t remaining=expires-hi;if(remaining>RC4_SOURCE_NS)remaining=RC4_SOURCE_NS;
 uint64_t ticks=(remaining*16/1000)*999700000/1000000000;
 if(!ticks||now>UINT64_MAX-ticks)return false;
 *until=now+ticks;return true;
}
bool rc4_bind(struct rc4_state *s,const struct rc4_admission *a){
 if(!s||!a||!a->parent_boot||!a->root_boot||!a->root_epoch||!a->parent_train||!a->parent_epoch||a->root_hi_ns<a->root_lo_ns||a->root_hi_ns-a->root_lo_ns>1200000000||a->root_expires_ns<=a->root_hi_ns||a->root_expires_ns-a->root_hi_ns>RC4_SOURCE_NS)return false;
 uint64_t remaining=a->root_expires_ns-a->root_hi_ns;
 /* A slow local counter must expire EARLIER, never extend absolute permission. */
 uint64_t ticks=(remaining*16/1000)*999700000/1000000000;
 uint64_t context_ticks=RC4_CONTEXT_TICKS*999700000/1000000000;
 if(!ticks||a->now_tick>UINT64_MAX-context_ticks)return false;
 bool same=s->bound&&s->parent_boot==a->parent_boot&&s->root_boot==a->root_boot&&s->root_epoch==a->root_epoch;
 if(same&&(a->now_tick<s->admission.now_tick||a->parent_train<s->parent_train||a->parent_epoch<s->parent_epoch))return false;
 if(!same){rc4_withdraw(s);s->publication_high=0;s->event_seen=false;s->last_root_ns=s->last_parent_ns=0;}
 else if(a->parent_epoch!=s->parent_epoch){rc4_withdraw(s);s->publication_high=0;s->event_seen=false;}
 else if(a->parent_train!=s->parent_train){memset(s->pending,0,sizeof s->pending);s->publication_high=0;s->event_seen=false;}
 if(a->publication_floor>s->publication_high){s->publication_high=a->publication_floor;s->event_seen=false;}
 s->parent_boot=a->parent_boot;s->root_boot=a->root_boot;s->root_epoch=a->root_epoch;s->parent_train=a->parent_train;s->parent_epoch=a->parent_epoch;
 s->admission=*a;s->valid_until=a->now_tick+ticks;s->context_until=a->now_tick+context_ticks;s->bound=true;return true;
}
bool rc4_receive(struct rc4_state *s,const struct rc4_packet *p,uint64_t rx,uint16_t event,uint8_t channel,uint64_t now){
 if(!s||!s->bound||now>=s->context_until||!packet_valid(p,&s->routes)||p->boot!=s->parent_boot||p->origin.boot!=s->root_boot||p->origin.epoch!=s->root_epoch||p->train<s->parent_train||p->epoch<s->parent_epoch||!s->self||p->sender!=s->routes.node[s->self].parent||channel>36||rx>now||now-rx>=8000000){if(s)++s->rejected;return false;}
 if(s->parent_boot!=p->boot||s->parent_train!=p->train||s->parent_epoch!=p->epoch){
  /* A recreated PA train changes future event association, not an already
   * accepted timestamp. Retain same-timebase holdover at its ORIGINAL expiry.
   * A parent capture epoch change still fences the old timing reference. */
  if(s->parent_boot!=p->boot||s->parent_epoch!=p->epoch)rc4_withdraw(s);
  else memset(s->pending,0,sizeof s->pending);
  s->parent_boot=p->boot;s->parent_train=p->train;s->parent_epoch=p->epoch;s->publication_high=0;s->event_seen=false;
 }
 if((p->train==s->admission.parent_train&&p->publication<=s->admission.publication_floor)||p->publication<s->publication_high||(p->publication==s->publication_high&&s->event_seen&&(uint16_t)(event-s->event_high)>=32768)||
    (p->publication==s->publication_high&&s->event_seen&&event==s->event_high)){++s->rejected;return false;}
 s->publication_high=p->publication;s->event_high=event;s->event_seen=true;
 bool accepted=false;
 for(unsigned i=0;i<p->count;i++)for(unsigned j=0;j<RC4_HISTORY;j++){
  struct rc4_pending *q=&s->pending[j];const struct rc4_correction *c=&p->correction[i];
  if(!q->used||q->packet.publication!=c->publication||q->event!=c->event||q->channel!=c->channel)continue;
  if(now<q->rx||now-q->rx>=8000000||!fresh_time(s,q->rx,c->tx_ns)||!same_origin(&p->origin,&q->packet.origin)){q->used=false;++s->rejected;continue;}
  if((!s->reference.valid||q->rx>s->reference.rx)&&c->root_ns>=s->last_root_ns&&c->tx_ns>=s->last_parent_ns){
   uint64_t until;
   if(!source_deadline(s,p->origin.expires,now,&until)){q->used=false;++s->rejected;continue;}
   s->valid_until=until;
   s->last_root_ns=c->root_ns;s->last_parent_ns=c->tx_ns;
   s->reference=(struct rc4_reference){.origin=p->origin,.rx=q->rx,.completed_at=now,.root_ns=c->root_ns,.parent_tx_ns=c->tx_ns,.bound=c->bound,.valid=true};memcpy(s->reference.proof,p->proof,16);++s->accepted;accepted=true;
  }
  q->used=false;
 }
 /* Identical original re-delivery cannot recreate a consumed transaction. */
 for(unsigned j=0;j<RC4_HISTORY;j++)if(s->pending[j].used&&s->pending[j].packet.publication==p->publication&&s->pending[j].event==event&&s->pending[j].channel==channel)return accepted;
 struct rc4_pending *q=&s->pending[s->head++%RC4_HISTORY];if(q->used)++s->evicted;
 *q=(struct rc4_pending){.packet=*p,.rx=rx,.available=now,.event=event,.channel=channel,.used=true};return accepted;
}
bool rc4_forward(const struct rc4_state *s,uint64_t tx,uint64_t available_at,const struct rc4_rate *r,uint32_t publication,uint16_t event,uint8_t channel,struct rc4_correction *out){
 if(!s||!s->bound||tx>=s->context_until||tx>=s->valid_until||!s->self||!s->routes.node[s->self].transmitter||!r||!out||!publication||channel>36||!s->reference.valid||tx<available_at||tx<s->reference.completed_at||tx<s->reference.rx||tx-s->reference.rx>=8000000||r->ppb<-300000||r->ppb>300000)return false;
 uint64_t nominal=(tx-s->reference.rx)*125/2;
 uint64_t residence=nominal*1000000000/(uint64_t)(1000000000+(int64_t)r->ppb);
 if(s->reference.parent_tx_ns>UINT64_MAX-residence-r->link_ns)return false;
 uint64_t ns=s->reference.parent_tx_ns+residence+r->link_ns;
 if(r->rx_tx_bias_ns<-100000||r->rx_tx_bias_ns>100000||(r->rx_tx_bias_ns<0&&ns<(uint64_t)(-(int64_t)r->rx_tx_bias_ns))||(r->rx_tx_bias_ns>0&&ns>UINT64_MAX-(uint64_t)r->rx_tx_bias_ns))return false;
 if(r->rx_tx_bias_ns<0)ns-=(uint64_t)(-(int64_t)r->rx_tx_bias_ns);else ns+=(uint64_t)r->rx_tx_bias_ns;
 if(!fresh_time(s,tx,ns)||ns>=s->reference.origin.expires||ns<s->reference.root_ns||ns-s->reference.root_ns>=RC4_REFERENCE_NS)return false;
 uint32_t bound=rc4_bound_add(s->reference.bound,r->link_bound);bound=rc4_bound_add(bound,r->bias_bound_ns);bound=rc4_bound_add(bound,r->capture_ns);bound=rc4_bound_add(bound,r->capture_ns);
 uint64_t rate_error=(residence*r->uncertainty_ppb+999999999)/1000000000;
 bound=rc4_bound_add(bound,r->uncertainty_ppb==RC4_UNKNOWN||rate_error>=RC4_UNKNOWN?RC4_UNKNOWN:(uint32_t)rate_error);
 *out=(struct rc4_correction){.publication=publication,.event=event,.channel=channel,.root_ns=s->reference.root_ns,.tx_ns=ns,.bound=bound};return true;
}
bool rc4_demand_begin(struct rc4_demand *d,uint64_t local,uint64_t peer,uint64_t challenge,uint64_t now){
 if(!d||!local||!peer||!challenge||now>UINT64_MAX-2000000)return false;
 if(d->local_boot!=local)*d=(struct rc4_demand){.local_boot=local};
 if(d->pending&&now<d->challenge_until)return false;
 if(challenge<=d->challenge_highwater)return false;
 d->challenge_highwater=challenge;
 if(d->peer_boot!=peer){d->peer_boot=peer;d->highwater=0;}
 d->challenge=challenge;d->challenge_until=now+2000000;d->pending=true;return true;
}
bool rc4_demand_accept(struct rc4_demand *d,uint64_t local,uint64_t peer,uint64_t challenge,uint32_t sequence,uint32_t lease,uint64_t now){
 if(!d||!d->pending||d->local_boot!=local||d->peer_boot!=peer||d->challenge!=challenge||now>=d->challenge_until||!sequence||sequence<=d->highwater||!lease||lease>RC4_LEASE_US||now>UINT64_MAX-lease)return false;
 d->highwater=sequence;if(now+lease>d->deadline)d->deadline=now+lease;d->pending=false;d->challenge=0;return true;
}
uint32_t rc4_upstream_lease(uint64_t remaining){
 /* Fast child / slow parent worst case: (1-300ppm)/(1+300ppm).
  *150.1ms also covers the150ms local exchange at a slow oscillator. */
 if(remaining>RC4_LEASE_US)remaining=RC4_LEASE_US;
 remaining=remaining*999400/1000000;
 if(remaining<=150100)return 0;
 remaining-=150100;return (uint32_t)remaining;
}
bool rc4_demand_live(const struct rc4_demand *d,uint64_t now){return d&&d->deadline&&now<d->deadline;}
bool rc4_read(const struct rc4_state *s,uint64_t now,const struct rc4_rate *r,uint64_t *ns,uint32_t *bound){
 if(!s||!s->bound||now>=s->context_until||now>=s->valid_until||!r||!ns||!bound||!s->reference.valid||now<s->reference.completed_at||now<s->reference.rx||now-s->reference.rx>=24000000||r->ppb<-300000||r->ppb>300000)return false;
 uint64_t nominal=(now-s->reference.rx)*125/2;
 uint64_t elapsed=nominal*1000000000/(uint64_t)(1000000000+(int64_t)r->ppb);
 if(s->reference.parent_tx_ns>UINT64_MAX-elapsed-r->link_ns)return false;
 *ns=s->reference.parent_tx_ns+elapsed+r->link_ns;
 if(r->rx_tx_bias_ns<-100000||r->rx_tx_bias_ns>100000||(r->rx_tx_bias_ns<0&&*ns<(uint64_t)(-(int64_t)r->rx_tx_bias_ns))||(r->rx_tx_bias_ns>0&&*ns>UINT64_MAX-(uint64_t)r->rx_tx_bias_ns))return false;
 if(r->rx_tx_bias_ns<0)*ns-=(uint64_t)(-(int64_t)r->rx_tx_bias_ns);else *ns+=(uint64_t)r->rx_tx_bias_ns;
 if(!fresh_time(s,now,*ns)||*ns>=s->reference.origin.expires)return false;
 *bound=rc4_bound_add(s->reference.bound,r->link_bound);*bound=rc4_bound_add(*bound,r->capture_ns);*bound=rc4_bound_add(*bound,r->capture_ns);*bound=rc4_bound_add(*bound,r->bias_bound_ns);
 uint64_t e=(elapsed*r->uncertainty_ppb+999999999)/1000000000;
 *bound=rc4_bound_add(*bound,r->uncertainty_ppb==RC4_UNKNOWN||e>=RC4_UNKNOWN?RC4_UNKNOWN:(uint32_t)e);return true;
}
bool rc4_rate_add(struct rc4_rate_fit *f,const struct rc4_reference *r){
 if(!f||!r||!r->valid)return false;
 unsigned last=(f->head+15)%16;if(f->count&&r->rx<=f->rx[last])return false;
 f->rx[f->head]=r->rx;f->ns[f->head]=r->parent_tx_ns;f->head=(f->head+1)%16;if(f->count<16)++f->count;
 unsigned first=(f->head+16-f->count)%16;
 if(f->count<4||r->rx<=f->rx[first]||r->parent_tx_ns<=f->ns[first])return false;
 uint64_t d=r->parent_tx_ns-f->ns[first];if(d<500000000)return false;
 uint64_t l=(r->rx-f->rx[first])*125/2;
 int64_t error=(int64_t)l-(int64_t)d;
 if(error>100000000||error<-100000000)return false;
 int64_t ppb=error*1000000000/(int64_t)d;
 if(ppb<-300000||ppb>300000)return false;
 f->ppb=(int32_t)ppb;f->valid=true;return true;
}

static bool control_valid(const struct rc4_control *c,const struct rc4_routes *r){
 if(!c||!rc4_routes_valid(r)||c->sender>=r->count||!c->sender_boot||c->type<RC4_CAPABILITY||c->type>RC4_RESPONSE)return false;
 if(c->type==RC4_CAPABILITY){
  if(!r->node[c->sender].transmitter||c->target!=255||!c->next_challenge||c->target_boot||c->challenge||c->nonce||c->sequence||c->lease_us)return false;
 }else{
  if(c->target>=r->count||!c->target_boot||!c->challenge||!c->nonce||!c->sequence||!c->lease_us||c->lease_us>RC4_LEASE_US)return false;
  if(c->type==RC4_REQUEST){if(!c->sender||r->node[c->sender].parent!=c->target||c->next_challenge)return false;}
  else if(!c->target||r->node[c->target].parent!=c->sender||!c->next_challenge)return false;
 }
 if(c->ready){if(c->type!=RC4_RESPONSE||!c->train||!c->capture_epoch||!c->root_boot||!c->root_epoch||c->root_hi_ns<c->root_lo_ns||c->root_hi_ns-c->root_lo_ns>1200000000||c->root_expires_ns<=c->root_hi_ns||c->root_expires_ns-c->root_hi_ns>RC4_SOURCE_NS)return false;}
 else if(c->train||c->capture_epoch||c->publication||c->root_boot||c->root_epoch||c->root_lo_ns||c->root_hi_ns||c->root_expires_ns)return false;
 return true;
}
int rc4_control_encode(uint8_t ad[RC4_CONTROL_BYTES],const struct rc4_control *c,const struct rc4_routes *r,const struct r2_group_config *g){
 uint8_t tag[16];if(!ad||!control_valid(c,r)||route_tag(tag,r,g))return -EINVAL;
 memset(ad,0,RC4_CONTROL_BYTES);ad[0]=135;ad[1]=ad[2]=ad[3]=255;ad[4]='R';ad[5]='Q';ad[6]=4;ad[7]=c->type;ad[8]=c->sender;ad[9]=c->target;ad[10]=c->ready;PUT(ad+12,g->epoch,4);
 PUT(ad+16,c->sender_boot,8);PUT(ad+24,c->target_boot,8);PUT(ad+32,c->challenge,8);PUT(ad+40,c->nonce,8);PUT(ad+48,c->sequence,4);PUT(ad+52,c->train,4);PUT(ad+56,c->capture_epoch,4);PUT(ad+60,c->publication,4);PUT(ad+64,c->root_boot,8);PUT(ad+72,c->root_epoch,4);PUT(ad+76,c->root_lo_ns,8);PUT(ad+84,c->root_hi_ns,8);PUT(ad+92,c->root_expires_ns,8);PUT(ad+100,c->lease_us,4);PUT(ad+104,c->next_challenge,8);memcpy(ad+112,tag,8);
 return r2_group_mac(ad+120,g,"RC4LOCAL",ad,120);
}
int rc4_control_decode(struct rc4_control *c,const uint8_t *ad,size_t n,const struct rc4_routes *r,const struct r2_group_config *g){
 uint8_t tag[16];if(!c||!ad||n!=RC4_CONTROL_BYTES||ad[0]!=135||ad[1]!=255||ad[2]!=255||ad[3]!=255||ad[4]!='R'||ad[5]!='Q'||ad[6]!=4||ad[10]>1||ad[11]||GET(ad+12,4)!=g->epoch||route_tag(tag,r,g)||memcmp(tag,ad+112,8))return -EINVAL;
 if(!r2_group_verify(ad+120,g,"RC4LOCAL",ad,120))return -EACCES;
 *c=(struct rc4_control){.type=ad[7],.sender=ad[8],.target=ad[9],.ready=ad[10],.sender_boot=GET(ad+16,8),.target_boot=GET(ad+24,8),.challenge=GET(ad+32,8),.nonce=GET(ad+40,8),.sequence=GET(ad+48,4),.train=GET(ad+52,4),.capture_epoch=GET(ad+56,4),.publication=GET(ad+60,4),.root_boot=GET(ad+64,8),.root_epoch=GET(ad+72,4),.root_lo_ns=GET(ad+76,8),.root_hi_ns=GET(ad+84,8),.root_expires_ns=GET(ad+92,8),.lease_us=GET(ad+100,4),.next_challenge=GET(ad+104,8)};
 return control_valid(c,r)?0:-EINVAL;
}
