/* SPDX-License-Identifier: Apache-2.0 */
#include "rc_gm_adapter.h"
#include <limits.h>
#include <string.h>
bool rc5_gm_accept(struct rc5_gm_context *s,const struct r2_join_record *r,uint64_t proof,uint64_t received,uint64_t tick){
 struct r2_join_time time;
 if(!s||!r||r->control||r->sender!=r->root||r->sender_boot!=r->root_boot||
    !r2_group_join_rc_time(&time,r,proof,received,r->source_bound_us))return false;
 *s=(struct rc5_gm_context){.source=*r,.time=time,.at_tick=tick,.authenticated=true,.rebind=true};return true;
}
bool rc5_gm_packet(struct rc5_gm_context *s,struct rc5_state *state,struct rc5_packet *out,const uint8_t *wire,size_t size,const struct r2_group_config *group,uint64_t tick){
 if(!s||!state||!out||!s->authenticated||tick<s->at_tick||tick-s->at_tick>=RC5_CONTEXT_TICKS)return false;
 uint64_t ids[4]={s->source.root,0,0,0};struct r2_relay_packet p;
 if(r2_group_relay_decode3(&p,wire,size,group,ids)||p.route.depth||p.origin.root||p.route.transmitter||p.route.parent||
    p.origin.utc_offset_ns<0)return false;
 if(p.origin.boot!=s->source.root_boot||p.route.boot!=s->source.sender_boot||p.origin.source_epoch!=s->source.source_epoch){s->authenticated=false;return false;}
 if(p.route.train<s->source.publication)return false;
 uint64_t elapsed=(tick-s->at_tick)/16;
 if(s->time.utc_us>UINT64_MAX-elapsed)return false;
 uint64_t utc=s->time.utc_us+elapsed,margin=s->time.uncertainty_us+elapsed*300/1000000+1;
 if(utc>UINT64_MAX/1000||margin>UINT64_MAX/1000)return false;
 uint64_t coarse=utc*1000,spread=margin*1000,offset=(uint64_t)p.origin.utc_offset_ns;
 if(coarse<offset||coarse-offset<spread||coarse-offset>UINT64_MAX-spread-RC5_SOURCE_NS)return false;
 uint64_t lo=coarse-offset-spread,hi=coarse-offset+spread;
 /* Preserve the received GM envelope exactly; translation authenticates the
  * RC's provenance statement, not a new GM signature or source issue time. */
 struct rc5_packet q={.origin={.root=s->source.root,.boot=p.origin.boot,.epoch=p.origin.source_epoch,
  .issued=p.origin.created_ns,.expires=p.origin.expires_ns,.utc_offset=p.origin.utc_offset_ns,
  .frame=p.origin.frame_ns,.hold_us=p.origin.hold_horizon_us,.hold_ns=p.origin.hold_budget_ns},
  .sender=state->routes.node[state->self].parent,.boot=p.route.boot,.train=p.route.train,.publication=p.route.publication,.epoch=p.route.capture_epoch,.count=p.count};
 for(unsigned i=0;i<p.count;i++){
  if(p.correction[i].ticks>UINT64_MAX/125)return false;
  q.correction[i]=(struct rc5_correction){.publication=p.correction[i].publication,.event=p.correction[i].event,.channel=p.correction[i].channel,
   .generation=p.correction[i].generation,.tx_ns=p.correction[i].ticks*125/2,.bound=RC5_UNKNOWN};
 }
 if(rc5_origin_sign(q.proof,&q.origin,&state->routes,group))return false;
 if(s->rebind){
  if(!p.route.publication)return false;
  struct rc5_admission a={.parent_boot=p.route.boot,.root_boot=p.origin.boot,.root_epoch=p.origin.source_epoch,
   .parent_train=p.route.train,.parent_epoch=p.route.capture_epoch,.publication_floor=p.route.publication-1,.now_tick=tick,
   .root_lo_ns=lo,.root_hi_ns=hi,.root_expires_ns=hi+RC5_SOURCE_NS};
  if(!rc5_bind(state,&a))return false;
  s->rebind=false;
 }
 *out=q;return true;
}
