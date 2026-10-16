/* SPDX-License-Identifier: Apache-2.0 */
#include "rc_boundary.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#define PUT r2_group_put
#define GET r2_group_get
uint32_t rc5_bound_add(uint32_t a,uint32_t b){return a==RC5_UNKNOWN||b==RC5_UNKNOWN||a>=RC5_UNKNOWN-b?RC5_UNKNOWN:a+b;}
bool rc5_routes_valid(const struct rc5_routes *r){
 if(!r||r->count<2||r->count>RC5_NODES||!r->node[0].transmitter||r->node[0].slot||r->node[0].depth||r->node[0].parent)return false;
 unsigned slots=1;
 for(unsigned i=0;i<r->count;i++){
  const struct rc5_node *n=&r->node[i];if(!n->id)return false;
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
static int route_tag(uint8_t out[16],const struct rc5_routes *r,const struct r2_group_config *g){
 if(!rc5_routes_valid(r))return -EINVAL;
 uint8_t body[1+12*RC5_NODES]={0};body[0]=r->count;
 for(unsigned i=0;i<r->count;i++){uint8_t *p=body+1+12*i;PUT(p,r->node[i].id,8);p[8]=r->node[i].parent;p[9]=RC5_PROFILE==4?(r->node[i].transmitter?r->node[i].depth:255):r->node[i].slot;p[10]=r->node[i].depth;p[11]=r->node[i].transmitter;}
 return r2_group_mac(out,g,RC5_PROFILE==4?"RC6ROUTE":"RC5ROUTE",body,sizeof body);
}
static bool valid_origin(const struct rc5_origin *o,const struct rc5_routes *r){
 return o->root==r->node[0].id&&o->boot&&o->epoch&&o->expires>o->issued&&o->expires-o->issued<=RC5_SOURCE_NS&&o->frame<=o->issued&&(!o->hold_us)==(!o->hold_ns)&&o->hold_us<=10000000&&o->hold_ns!=RC5_UNKNOWN;
}
static void origin_write(uint8_t *p,const struct rc5_origin *o){
 PUT(p,o->root,8);PUT(p+8,o->boot,8);PUT(p+16,o->epoch,4);PUT(p+20,o->issued,8);PUT(p+28,o->expires,8);PUT(p+36,(uint64_t)o->utc_offset,8);PUT(p+44,o->frame,8);PUT(p+52,o->hold_us,4);PUT(p+56,o->hold_ns,4);
}
static void origin_read(struct rc5_origin *o,const uint8_t *p){
 *o=(struct rc5_origin){.root=GET(p,8),.boot=GET(p+8,8),.epoch=GET(p+16,4),.issued=GET(p+20,8),.expires=GET(p+28,8),.utc_offset=(int64_t)GET(p+36,8),.frame=GET(p+44,8),.hold_us=GET(p+52,4),.hold_ns=GET(p+56,4)};
}
int rc5_origin_sign(uint8_t proof[16],const struct rc5_origin *o,const struct rc5_routes *r,const struct r2_group_config *g){
 uint8_t body[68],tag[16];if(!proof||!o||route_tag(tag,r,g)||!valid_origin(o,r))return -EINVAL;
 memcpy(body,tag,8);origin_write(body+8,o);return r2_group_mac(proof,g,RC5_PROFILE==4?"RC6ORIGN":"RC5ORIGN",body,sizeof body);
}
static bool packet_valid(const struct rc5_packet *p,const struct rc5_routes *r){
 if(!p||!rc5_routes_valid(r)||p->sender>=r->count||!r->node[p->sender].transmitter||!valid_origin(&p->origin,r)||!p->boot||!p->train||!p->publication||!p->epoch||p->count>2)return false;
 for(unsigned i=0;i<p->count;i++){
  const struct rc5_correction *c=&p->correction[i];
  if(!c->publication||c->publication>=p->publication||c->channel>36||!c->generation)return false;
  if(i&&c->publication==p->correction[0].publication&&c->event==p->correction[0].event&&c->channel==p->correction[0].channel)return false;
 }
 return true;
}
int rc5_encode(uint8_t ad[RC5_BYTES],const struct rc5_packet *p,const struct rc5_routes *r,const struct r2_group_config *g){
 uint8_t tag[16],proof[16];if(!ad||!packet_valid(p,r)||route_tag(tag,r,g)||rc5_origin_sign(proof,&p->origin,r,g)||memcmp(proof,p->proof,16))return -EINVAL;
 memset(ad,0,RC5_BYTES);ad[0]=189;ad[1]=ad[2]=ad[3]=255;ad[4]='R';ad[5]='C';ad[6]=RC5_VERSION;ad[7]=p->count;ad[172]=RC5_PROFILE;PUT(ad+8,g->epoch,4);origin_write(ad+12,&p->origin);memcpy(ad+72,p->proof,16);
 memcpy(ad+88,tag,8);ad[96]=p->sender;ad[97]=(r->node[p->sender].depth<<4)|r->node[p->sender].slot;PUT(ad+98,p->boot,8);PUT(ad+106,p->train,4);PUT(ad+110,p->publication,4);PUT(ad+114,p->epoch,4);
 for(unsigned i=0;i<p->count;i++){uint8_t *w=ad+118+27*i;const struct rc5_correction *c=&p->correction[i];PUT(w,c->publication,4);PUT(w+4,c->event,2);w[6]=c->channel;PUT(w+7,c->generation,4);PUT(w+11,c->age_us,4);PUT(w+15,c->tx_ns,8);PUT(w+23,c->bound,4);}
 return r2_group_mac(ad+174,g,RC5_PROFILE==4?"RC6PACKT":"RC5PACKT",ad,174);
}
int rc5_decode(struct rc5_packet *p,const uint8_t *ad,size_t n,const struct rc5_routes *r,const struct r2_group_config *g){
 uint8_t tag[16],proof[16];if(!p||!ad||n!=RC5_BYTES||route_tag(tag,r,g)||ad[0]!=189||ad[1]!=255||ad[2]!=255||ad[3]!=255||ad[4]!='R'||ad[5]!='C'||ad[6]!=RC5_VERSION||ad[7]>2||GET(ad+8,4)!=g->epoch||memcmp(tag,ad+88,8)||ad[172]!=RC5_PROFILE||ad[173])return -EINVAL;
 if(!r2_group_verify(ad+174,g,RC5_PROFILE==4?"RC6PACKT":"RC5PACKT",ad,174))return -EACCES;
 memset(p,0,sizeof *p);origin_read(&p->origin,ad+12);memcpy(p->proof,ad+72,16);p->sender=ad[96];p->slot=ad[97]&15;p->count=ad[7];p->boot=GET(ad+98,8);p->train=GET(ad+106,4);p->publication=GET(ad+110,4);p->epoch=GET(ad+114,4);
 if(p->sender>=r->count||(RC5_PROFILE==4?((ad[97]>>4)!=r->node[p->sender].depth||(ad[97]&15)>3||((ad[97]&15)==0&&p->sender!=0)):ad[97]!=((r->node[p->sender].depth<<4)|r->node[p->sender].slot))||rc5_origin_sign(proof,&p->origin,r,g)||memcmp(proof,p->proof,16))return -EACCES;
 for(unsigned i=0;i<2;i++){const uint8_t *w=ad+118+27*i;if(i>=p->count){for(unsigned j=0;j<27;j++)if(w[j])return -EINVAL;continue;}
  p->correction[i]=(struct rc5_correction){.publication=GET(w,4),.event=GET(w+4,2),.channel=w[6],.generation=GET(w+7,4),.age_us=GET(w+11,4),.tx_ns=GET(w+15,8),.bound=GET(w+23,4)};}
 return packet_valid(p,r)?0:-EINVAL;
}
void rc5_init(struct rc5_state *s,const struct rc5_routes *r,unsigned self){memset(s,0,sizeof *s);if(rc5_routes_valid(r)&&self<r->count){s->routes=*r;s->self=self;}}
void rc5_withdraw(struct rc5_state *s){memset(s->pending,0,sizeof s->pending);memset(&s->reference,0,sizeof s->reference);}
/* Declared conservative local oscillator envelope, separate from learned rate.
 * 100us comparison guard covers coarse per-link capture/propagation reference
 * uncertainty only. Physical use must establish this bound independently. */
bool rc5_coarse_interval(const struct rc5_state *s,uint64_t tick,uint64_t *out_lo,uint64_t *out_hi){
 if(!s||!s->bound||!out_lo||!out_hi)return false;
 const struct rc5_admission *a=&s->admission;
 uint64_t delta=tick>=a->now_tick?tick-a->now_tick:a->now_tick-tick;
 if(delta>RC5_CONTEXT_TICKS)return false;
 uint64_t nominal=delta*125/2;
 uint64_t low=(nominal/1000300000)*1000000000+(nominal%1000300000)*1000000000/1000300000;
 uint64_t high=(nominal/999700000)*1000000000+((nominal%999700000)*1000000000+999699999)/999700000;
 uint64_t lo=a->root_lo_ns,hi=a->root_hi_ns;
 if(tick>=a->now_tick){if(hi>UINT64_MAX-high)return false;lo+=low;hi+=high;}
 else {lo=lo>high?lo-high:0;if(hi<low)return false;hi-=low;}
 *out_lo=lo;*out_hi=hi;return true;
}
static bool fresh_time(const struct rc5_state *s,uint64_t tick,uint64_t ns){
 uint64_t lo,hi;if(!rc5_coarse_interval(s,tick,&lo,&hi))return false;
 lo=lo>100000?lo-100000:0;if(hi>UINT64_MAX-100000)return false;hi+=100000;
 return ns>=lo&&ns<=hi;
}
static bool source_deadline(const struct rc5_state *s,uint64_t expires,uint64_t now,uint64_t *until){
 uint64_t lo,hi;if(!rc5_coarse_interval(s,now,&lo,&hi)||expires<=hi)return false;
 uint64_t remaining=expires-hi;if(remaining>RC5_SOURCE_NS)remaining=RC5_SOURCE_NS;
 uint64_t ticks=(remaining*16/1000)*999700000/1000000000;
 if(!ticks||now>UINT64_MAX-ticks)return false;
 *until=now+ticks;return true;
}
bool rc5_bind(struct rc5_state *s,const struct rc5_admission *a){
 if(!s||!a||!a->parent_boot||!a->root_boot||!a->root_epoch||!a->parent_train||!a->parent_epoch||a->root_hi_ns<a->root_lo_ns||a->root_hi_ns-a->root_lo_ns>1200000000||a->root_expires_ns<=a->root_hi_ns||a->root_expires_ns-a->root_hi_ns>RC5_SOURCE_NS)return false;
 uint64_t remaining=a->root_expires_ns-a->root_hi_ns;
 /* A slow local counter must expire EARLIER, never extend absolute permission. */
 uint64_t ticks=(remaining*16/1000)*999700000/1000000000;
 uint64_t context_ticks=RC5_CONTEXT_TICKS*999700000/1000000000;
 if(!ticks||a->now_tick>UINT64_MAX-context_ticks)return false;
 bool same=s->bound&&s->parent_boot==a->parent_boot&&s->root_boot==a->root_boot&&s->root_epoch==a->root_epoch;
 if(same&&(a->now_tick<s->admission.now_tick||a->parent_train<s->parent_train||a->parent_epoch<s->parent_epoch))return false;
 if(!same){rc5_withdraw(s);s->publication_high=0;s->event_seen=false;s->last_parent_ns=0;}
 else if(a->parent_epoch!=s->parent_epoch){rc5_withdraw(s);s->publication_high=0;s->event_seen=false;}
 else if(a->parent_train!=s->parent_train){memset(s->pending,0,sizeof s->pending);s->publication_high=0;s->event_seen=false;}
 if(a->publication_floor>s->publication_high){s->publication_high=a->publication_floor;s->event_seen=false;}
 s->parent_boot=a->parent_boot;s->root_boot=a->root_boot;s->root_epoch=a->root_epoch;s->parent_train=a->parent_train;s->parent_epoch=a->parent_epoch;
 s->admission=*a;s->valid_until=a->now_tick+ticks;s->context_until=a->now_tick+context_ticks;s->bound=true;return true;
}
bool rc5_receive(struct rc5_state *s,const struct rc5_packet *p,uint64_t rx,uint16_t event,uint8_t channel,uint64_t now){
 if(!s||!s->bound||now>=s->context_until||!packet_valid(p,&s->routes)||p->boot!=s->parent_boot||p->origin.boot!=s->root_boot||p->origin.epoch!=s->root_epoch||p->train<s->parent_train||p->epoch<s->parent_epoch||!s->self||p->sender!=s->routes.node[s->self].parent||channel>36||rx>now||now-rx>=8000000){if(s)++s->rejected;return false;}
 if(s->parent_boot!=p->boot||s->parent_train!=p->train||s->parent_epoch!=p->epoch){
  /* A recreated PA train changes future event association, not an already
   * accepted timestamp. Retain same-timebase holdover at its ORIGINAL expiry.
   * A parent capture epoch change still fences the old timing reference. */
  if(s->parent_boot!=p->boot||s->parent_epoch!=p->epoch)rc5_withdraw(s);
  else memset(s->pending,0,sizeof s->pending);
  s->parent_boot=p->boot;s->parent_train=p->train;s->parent_epoch=p->epoch;s->publication_high=0;s->event_seen=false;
 }
 if((p->train==s->admission.parent_train&&p->publication<=s->admission.publication_floor)||p->publication<s->publication_high||(p->publication==s->publication_high&&s->event_seen&&(uint16_t)(event-s->event_high)>=32768)||
    (p->publication==s->publication_high&&s->event_seen&&event==s->event_high)){++s->rejected;return false;}
 s->publication_high=p->publication;s->event_high=event;s->event_seen=true;
 bool accepted=false;
 for(unsigned i=0;i<p->count;i++)for(unsigned j=0;j<RC5_HISTORY;j++){
  struct rc5_pending *q=&s->pending[j];const struct rc5_correction *c=&p->correction[i];
  if(!q->used||q->packet.publication!=c->publication||q->event!=c->event||q->channel!=c->channel)continue;
  if(now<q->rx||now-q->rx>=8000000||!fresh_time(s,q->rx,c->tx_ns)||p->origin.boot!=q->packet.origin.boot||p->origin.epoch!=q->packet.origin.epoch){q->used=false;++s->rejected;continue;}
  if((!s->reference.valid||q->rx>s->reference.rx)&&c->tx_ns>=s->last_parent_ns){
   uint64_t until;
   /* Root envelope expiry is provenance, not local held-service permission.
    * Admission/replay checks above still gate every new parent sample. */
   if(source_deadline(s,p->origin.expires,now,&until))s->valid_until=until;
   s->last_parent_ns=c->tx_ns;
   s->reference=(struct rc5_reference){.origin=p->origin,.rx=q->rx,.completed_at=now,.generation=c->generation,.age_us=c->age_us,.parent_tx_ns=c->tx_ns,.bound=c->bound,.valid=true,
    .original_available=q->available,.followup_rx=rx,.publication=c->publication,.event=c->event,.channel=c->channel};memcpy(s->reference.proof,p->proof,16);++s->accepted;accepted=true;
  }
  q->used=false;
 }
 /* Identical original re-delivery cannot recreate a consumed transaction. */
 for(unsigned j=0;j<RC5_HISTORY;j++)if(s->pending[j].used&&s->pending[j].packet.publication==p->publication&&s->pending[j].event==event&&s->pending[j].channel==channel)return accepted;
 struct rc5_pending *q=&s->pending[s->head++%RC5_HISTORY];if(q->used)++s->evicted;
 *q=(struct rc5_pending){.packet=*p,.rx=rx,.available=now,.event=event,.channel=channel,.used=true};return accepted;
}
bool rc5_demand_begin(struct rc5_demand *d,uint64_t local,uint64_t peer,uint64_t challenge,uint64_t now){
 if(!d||!local||!peer||!challenge||now>UINT64_MAX-2000000)return false;
 if(d->local_boot!=local)*d=(struct rc5_demand){.local_boot=local};
 if(d->pending&&now<d->challenge_until)return false;
 if(challenge<=d->challenge_highwater)return false;
 d->challenge_highwater=challenge;
 if(d->peer_boot!=peer){d->peer_boot=peer;d->highwater=0;}
 d->challenge=challenge;d->challenge_until=now+2000000;d->pending=true;return true;
}
bool rc5_demand_accept(struct rc5_demand *d,uint64_t local,uint64_t peer,uint64_t challenge,uint32_t sequence,uint32_t lease,uint64_t now){
 if(!d||!d->pending||d->local_boot!=local||d->peer_boot!=peer||d->challenge!=challenge||now>=d->challenge_until||!sequence||sequence<=d->highwater||!lease||lease>RC5_LEASE_US||now>UINT64_MAX-lease)return false;
 d->highwater=sequence;if(now+lease>d->deadline)d->deadline=now+lease;d->pending=false;d->challenge=0;return true;
}
uint32_t rc5_upstream_lease(uint64_t remaining){
 /* Fast child / slow parent worst case: (1-300ppm)/(1+300ppm).
  *2.001s covers the2s transaction default at a slow oscillator. */
 if(remaining>RC5_LEASE_US)remaining=RC5_LEASE_US;
 remaining=remaining*999400/1000000;
 if(remaining<=2001000)return 0;
 remaining-=2001000;return (uint32_t)remaining;
}
bool rc5_demand_live(const struct rc5_demand *d,uint64_t now){return d&&d->deadline&&now<d->deadline;}
bool rc5_rate_add(struct rc5_rate_fit *f,const struct rc5_reference *r){
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

bool rc5_rate_rms(const struct rc5_rate_fit *f,uint32_t *ns){
 if(ns)*ns=RC5_UNKNOWN;
 if(!f||!ns||!f->valid||f->count<4||f->count>16||f->head>=16)return false;
 unsigned first=(f->head+16-f->count)%16;int64_t residual[16],sum=0;
 for(unsigned j=0;j<f->count;j++){
  unsigned i=(first+j)%16;
  if(f->rx[i]<f->rx[first]||f->ns[i]<f->ns[first])return false;
  uint64_t ticks=f->rx[i]-f->rx[first],d=f->ns[i]-f->ns[first];
  if(ticks>(uint64_t)INT64_MAX/125||d>INT64_MAX)return false;
  int64_t local=(int64_t)(ticks*125/2),remote=(int64_t)d;
  uint64_t rate=f->ppb<0?(uint64_t)(-(int64_t)f->ppb):(uint64_t)f->ppb;
  if(rate&&d/1000000000>(uint64_t)INT64_MAX/rate)return false;
  uint64_t magnitude=d/1000000000*rate+(d%1000000000)*rate/1000000000;
  if(magnitude>INT64_MAX)return false;
  int64_t correction=f->ppb<0?-(int64_t)magnitude:(int64_t)magnitude,error=local-remote;
  if((correction>0&&error<INT64_MIN+correction)||(correction<0&&error>INT64_MAX+correction))return false;
  residual[j]=error-correction;
  if((residual[j]>0&&sum>INT64_MAX-residual[j])||(residual[j]<0&&sum<INT64_MIN-residual[j]))return false;
  sum+=residual[j];
 }
 int64_t mean=sum/(int64_t)f->count;uint64_t squares=0;
 for(unsigned j=0;j<f->count;j++){
  if((mean>0&&residual[j]<INT64_MIN+mean)||(mean<0&&residual[j]>INT64_MAX+mean))return false;
  int64_t e=residual[j]-mean;uint64_t a=e<0?(uint64_t)(-(e+1))+1:(uint64_t)e;
  if(a&&a>UINT64_MAX/a)return false;
  uint64_t square=a*a;if(squares>UINT64_MAX-square)return false;squares+=square;
 }
 uint64_t value=squares/f->count,root=0,bit=UINT64_C(1)<<62;
 while(bit>value)bit>>=2;
 while(bit){if(value>=root+bit){value-=root+bit;root=(root>>1)+bit;}else root>>=1;bit>>=2;}
 if(root>=RC5_UNKNOWN)return false;
 *ns=(uint32_t)root;return true;
}

static bool control_valid(const struct rc5_control *c,const struct rc5_routes *r){
 if(!c||!rc5_routes_valid(r)||c->sender>=r->count||!c->sender_boot||c->type<RC5_CAPABILITY||c->type>RC5_RESPONSE)return false;
 if(c->type==RC5_CAPABILITY){
  if(!r->node[c->sender].transmitter||c->target!=255||!c->next_challenge||c->target_boot||c->challenge||c->nonce||c->sequence||c->lease_us)return false;
 }else{
  if(c->target>=r->count||!c->target_boot||!c->challenge||!c->nonce||!c->sequence||!c->lease_us||c->lease_us>RC5_LEASE_US)return false;
  if(c->type==RC5_REQUEST){if(!c->sender||r->node[c->sender].parent!=c->target||c->next_challenge)return false;}
  else if(!c->target||r->node[c->target].parent!=c->sender||!c->next_challenge)return false;
 }
#if defined(R2_RC_AUTO)
 struct rc5_routes claimed=*r;
 for(unsigned i=0;i<claimed.count;i++){unsigned slot=(c->slots>>(2*i))&3;if(!i||!claimed.node[i].transmitter){if(slot)return false;}else claimed.node[i].slot=slot;}
 if(!rc5_routes_valid(&claimed)||(claimed.count<8&&(c->slots>>(2*claimed.count))))return false;
#endif
 if(c->operation>1||(c->operation&&c->type!=RC5_REQUEST)||(c->publishing&&c->type!=RC5_CAPABILITY))return false;
 if(c->ready){if(c->type!=RC5_RESPONSE||!c->train||!c->capture_epoch||!c->root_boot||!c->root_epoch||c->root_hi_ns<c->root_lo_ns||c->root_hi_ns-c->root_lo_ns>1200000000||c->root_expires_ns<=c->root_hi_ns||c->root_expires_ns-c->root_hi_ns>RC5_SOURCE_NS)return false;}
 else if(c->train||c->capture_epoch||c->publication||c->root_boot||c->root_epoch||c->root_lo_ns||c->root_hi_ns||c->root_expires_ns)return false;
 return true;
}
int rc5_control_encode(uint8_t ad[RC5_CONTROL_BYTES],const struct rc5_control *c,const struct rc5_routes *r,const struct r2_group_config *g){
 uint8_t tag[16];if(!ad||!control_valid(c,r)||route_tag(tag,r,g))return -EINVAL;
 memset(ad,0,RC5_CONTROL_BYTES);ad[0]=RC5_CONTROL_BYTES-1;ad[1]=ad[2]=ad[3]=255;ad[4]='R';ad[5]='Q';ad[6]=RC5_VERSION;ad[7]=c->type;ad[8]=c->sender;ad[9]=c->target;ad[10]=c->ready;ad[11]=RC5_PROFILE;PUT(ad+12,g->epoch,4);
 PUT(ad+16,c->sender_boot,8);PUT(ad+24,c->target_boot,8);PUT(ad+32,c->challenge,8);PUT(ad+40,c->nonce,8);PUT(ad+48,c->sequence,4);PUT(ad+52,c->train,4);PUT(ad+56,c->capture_epoch,4);PUT(ad+60,c->publication,4);PUT(ad+64,c->root_boot,8);PUT(ad+72,c->root_epoch,4);PUT(ad+76,c->root_lo_ns,8);PUT(ad+84,c->root_hi_ns,8);PUT(ad+92,c->root_expires_ns,8);PUT(ad+100,c->lease_us,4);PUT(ad+104,c->next_challenge,8);memcpy(ad+112,tag,8);
 #if defined(R2_RC_AUTO)
 PUT(ad+120,c->slots,2);ad[122]=c->operation;ad[123]=c->publishing;
#endif
 return r2_group_mac(ad+RC5_CONTROL_MAC,g,RC5_PROFILE==4?"RC6LOCAL":"RC5LOCAL",ad,RC5_CONTROL_MAC);
}
int rc5_control_decode(struct rc5_control *c,const uint8_t *ad,size_t n,const struct rc5_routes *r,const struct r2_group_config *g){
 uint8_t tag[16];if(!c||!ad||n!=RC5_CONTROL_BYTES||ad[0]!=RC5_CONTROL_BYTES-1||ad[1]!=255||ad[2]!=255||ad[3]!=255||ad[4]!='R'||ad[5]!='Q'||ad[6]!=RC5_VERSION||ad[10]>1||ad[11]!=RC5_PROFILE||GET(ad+12,4)!=g->epoch||route_tag(tag,r,g)||memcmp(tag,ad+112,8))return -EINVAL;
 if(!r2_group_verify(ad+RC5_CONTROL_MAC,g,RC5_PROFILE==4?"RC6LOCAL":"RC5LOCAL",ad,RC5_CONTROL_MAC))return -EACCES;
#if defined(R2_RC_AUTO)
 if(ad[122]>1||ad[123]>1||GET(ad+124,4))return -EINVAL;
#endif
 *c=(struct rc5_control){.type=ad[7],.sender=ad[8],.target=ad[9],.ready=ad[10],.sender_boot=GET(ad+16,8),.target_boot=GET(ad+24,8),.challenge=GET(ad+32,8),.nonce=GET(ad+40,8),.sequence=GET(ad+48,4),.train=GET(ad+52,4),.capture_epoch=GET(ad+56,4),.publication=GET(ad+60,4),.root_boot=GET(ad+64,8),.root_epoch=GET(ad+72,4),.root_lo_ns=GET(ad+76,8),.root_hi_ns=GET(ad+84,8),.root_expires_ns=GET(ad+92,8),.lease_us=GET(ad+100,4),.next_challenge=GET(ad+104,8)};
#if defined(R2_RC_AUTO)
 c->slots=GET(ad+120,2);c->operation=ad[122];c->publishing=ad[123];
#endif
 return control_valid(c,r)?0:-EINVAL;
}

void rc5_clock_reset(struct rc5_clock *c){if(c)memset(c,0,sizeof *c);}
static uint32_t rate_growth(uint64_t elapsed,uint32_t rate){
 if(rate==RC5_UNKNOWN)return RC5_UNKNOWN;
 uint64_t whole=elapsed/1000000000,part=elapsed%1000000000;
 if(rate&&whole>=RC5_UNKNOWN/rate)return RC5_UNKNOWN;
 uint64_t result=whole*rate+(part*rate+999999999)/1000000000;
 return result>=RC5_UNKNOWN?RC5_UNKNOWN:(uint32_t)result;
}
static bool map_value(const struct rc5_map *m,uint64_t tick,uint64_t *ns,uint64_t *elapsed){
 if(tick<m->anchor_tick||tick-m->anchor_tick>RC5_MAP_SPAN_TICKS)return false;
 uint64_t nominal=(tick-m->anchor_tick)*125/2;
 uint64_t denominator=(uint64_t)(1000000000+(int64_t)m->ppb);
 uint64_t whole=nominal/denominator,part=(nominal%denominator)*1000000000/denominator;
 if(whole>(UINT64_MAX-part)/1000000000)return false;
 *elapsed=whole*1000000000+part;
 if(m->anchor_ns>UINT64_MAX-*elapsed)return false;
 *ns=m->anchor_ns+*elapsed;
 uint64_t segment=*elapsed<1000000000?*elapsed:1000000000;
 int64_t phase=(int64_t)segment*m->phase_ns/1000000000;
 if(phase<0){if(*ns<(uint64_t)(-phase))return false;*ns-=(uint64_t)(-phase);}
 else {if(*ns>UINT64_MAX-(uint64_t)phase)return false;*ns+=(uint64_t)phase;}
 return true;
}
bool rc5_clock_discipline(struct rc5_clock *c,const struct rc5_state *s,const struct rc5_rate_fit *fit,const struct rc5_rate *r){
 if(!c||!s||!r||!fit||!fit->valid||fit->ppb< -300000||fit->ppb>300000||!s->bound||!s->reference.valid||s->reference.completed_at>=s->context_until||r->rx_tx_bias_ns< -100000||r->rx_tx_bias_ns>100000)return false;
 const struct rc5_reference *q=&s->reference;
 if(c->count&&(c->root_boot!=s->root_boot||c->root_epoch!=s->root_epoch||c->parent_boot!=s->parent_boot||c->parent_epoch!=s->parent_epoch)){c->count=0;c->output_seen=false;}
 if(c->count&&q->completed_at<=c->map[(c->head+3)%4].effective)return false;
 uint64_t anchor=q->parent_tx_ns;
 if(anchor>UINT64_MAX-r->link_ns)return false;
 anchor+=r->link_ns;
 if((r->rx_tx_bias_ns<0&&anchor<(uint64_t)(-(int64_t)r->rx_tx_bias_ns))||(r->rx_tx_bias_ns>0&&anchor>UINT64_MAX-(uint64_t)r->rx_tx_bias_ns))return false;
 if(r->rx_tx_bias_ns<0)anchor-=(uint64_t)(-(int64_t)r->rx_tx_bias_ns);else anchor+=(uint64_t)r->rx_tx_bias_ns;
 struct rc5_map next={.effective=q->completed_at,.anchor_tick=q->rx,.anchor_ns=anchor,.ppb=fit->ppb,.age_us=q->age_us,.bound=q->bound,.rate_bound=r->uncertainty_ppb,.capture_bound=r->capture_ns};
 next.bound=rc5_bound_add(next.bound,r->link_bound);next.bound=rc5_bound_add(next.bound,r->bias_bound_ns);next.bound=rc5_bound_add(next.bound,r->capture_ns);
 if(c->count){uint64_t old_ns,new_ns,elapsed;const struct rc5_map *old=&c->map[(c->head+3)%4];
  if(map_value(old,next.effective,&old_ns,&elapsed)&&map_value(&next,next.effective,&new_ns,&elapsed)){
   uint64_t jump=old_ns>new_ns?old_ns-new_ns:new_ns-old_ns;
   if(jump>1000){
    /* Functional reacquisition is an explicitly unqualified discontinuity.
     * Historical maps remain immutable; precision acceptance is separate. */
    ++c->step_fences;c->output_seen=false;next.bound=RC5_UNKNOWN;
   }else{
   /* Continuous handover: phase is removed over a one-second causal slew,
    * never a backward/forward timestamp step. Rate denominator is bounded. */
   int64_t phase=new_ns>=old_ns?(int64_t)(new_ns-old_ns):-(int64_t)(old_ns-new_ns);
   int64_t slewed=(int64_t)fit->ppb-phase;
   if(slewed< -300000||slewed>300000)return false;
   uint64_t age=(uint64_t)next.age_us+elapsed/1000+(elapsed%1000!=0);
   next.age_us=age>=UINT32_MAX?UINT32_MAX:(uint32_t)age;
   next.bound=rc5_bound_add(next.bound,rate_growth(elapsed,next.rate_bound));
   next.anchor_tick=next.effective;next.anchor_ns=old_ns;next.phase_ns=(int32_t)phase;next.continuous=true;
   next.bound=rc5_bound_add(next.bound,(uint32_t)jump);
   }
  }
 }
 if(c->generation==UINT32_MAX){c->count=0;return false;}
 next.generation=++c->generation;c->map[c->head]=next;c->head=(c->head+1)%4;if(c->count<4)++c->count;
 c->root_boot=s->root_boot;c->root_epoch=s->root_epoch;c->parent_boot=s->parent_boot;c->parent_epoch=s->parent_epoch;return true;
}
bool rc5_map_project(const struct rc5_map *m,uint64_t tick,uint64_t *ns,uint64_t *elapsed){
 return m&&ns&&elapsed&&tick>=m->effective&&map_value(m,tick,ns,elapsed);
}
bool rc5_clock_capture(const struct rc5_clock *c,const struct rc5_state *s,uint64_t tick,struct rc5_stamp *out){
 if(out)*out=(struct rc5_stamp){.bound=RC5_UNKNOWN,.state=RC5_UNAVAILABLE,.quality=RC5_UNQUALIFIED};
 if(!c||!s||!out||!c->count||!s->bound||c->root_boot!=s->root_boot||c->root_epoch!=s->root_epoch||c->parent_boot!=s->parent_boot||c->parent_epoch!=s->parent_epoch)return false;
 const struct rc5_map *m=NULL;
 for(unsigned i=0;i<c->count;i++){const struct rc5_map *q=&c->map[(c->head+3-i)%4];if(q->effective<=tick){m=q;break;}}
 if(!m||(c->hold_ticks&&tick-m->effective>=c->hold_ticks))return false;
 uint64_t ns,elapsed;if(!map_value(m,tick,&ns,&elapsed))return false;
 uint64_t age=(uint64_t)m->age_us+elapsed/1000+(elapsed%1000!=0);if(age>UINT32_MAX)age=UINT32_MAX;
 uint32_t bound=rc5_bound_add(m->bound,m->capture_bound);
 bound=rc5_bound_add(bound,rate_growth(elapsed,m->rate_bound));
 *out=(struct rc5_stamp){.ns=ns,.generation=m->generation,.age_us=(uint32_t)age,.bound=bound,.state=age<250000?RC5_LOCKED:RC5_HELD,.quality=bound==RC5_UNKNOWN||bound>RC5_CLOCK_BOUND_NS?RC5_UNQUALIFIED:RC5_BOUNDED,.pll_tracking=m->continuous};return true;
}
bool rc5_clock_read(struct rc5_clock *c,const struct rc5_state *s,uint64_t tick,struct rc5_stamp *out){
 if(!rc5_clock_capture(c,s,tick,out))return false;
 if(c->output_seen&&(tick<c->last_tick||out->ns<c->last_ns)){++c->step_fences;c->count=0;out->state=RC5_UNAVAILABLE;out->quality=RC5_UNQUALIFIED;return false;}
 c->last_tick=tick;c->last_ns=out->ns;c->output_seen=true;return true;
}

/* A foreign route table can still warn of occupancy. This does not admit its
 * sender, source, route or time. Hints carry no clock or role authority. */
bool rc6_occupancy_hint(const uint8_t *ad,size_t n,const struct r2_group_config *g,uint64_t own_boot,uint8_t *slot){
#if defined(R2_RC_AUTO)
 if(!ad||!slot||n!=144||ad[0]!=143||ad[4]!='R'||ad[5]!='Q'||ad[6]!=6||ad[7]!=RC5_CAPABILITY||ad[8]>=8||ad[11]!=4||GET(ad+12,4)!=g->epoch||!GET(ad+16,8)||GET(ad+16,8)==own_boot||ad[122]||ad[123]!=1||GET(ad+124,4))return false;
 if(!r2_group_verify(ad+128,g,"RC6LOCAL",ad,128))return false;
 *slot=(GET(ad+120,2)>>(2*ad[8]))&3;return *slot>=1&&*slot<=3;
#else
 (void)ad;(void)n;(void)g;(void)own_boot;(void)slot;return false;
#endif
}
