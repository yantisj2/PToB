/* SPDX-License-Identifier: Apache-2.0 */
#include "rc_shadow.h"
#include "capture.h"
#include <zephyr/kernel.h>
#include <limits.h>
#include <zephyr/sys/atomic.h>
static struct k_spinlock lock;
static struct rc_shadow_history history;
static struct rc_shadow_record queue[RC_SHADOW_QUEUE],records[RC_SHADOW_RECORDS];
static unsigned qhead,qtail,qcount,rhead,rcount;
#if defined(R2_BOUNDARY_FIELD) || defined(R2_SHADOW_TEST_BOTH)
static atomic_t requested=1;
static struct rc_shadow_status stats={.enabled=true};
#else
/* Normal clocks collect only during an explicitly started UI session. */
static atomic_t requested;
static struct rc_shadow_status stats;
#endif
static atomic_t serving;
static uint64_t last_edge,last_epoch;
static struct {struct r2_capture edge;int64_t utc,at;uint32_t association,reason;bool fix,pps;} refs[RC_SHADOW_QUEUE];
static unsigned ref_head,ref_count;
static void sat(uint32_t *v){if(*v<UINT32_MAX)++*v;}
void r2_shadow_normal_map(const struct rc_shadow_map *map){
 if(!map||!map->normal||!map->normal_map.valid||!map->epoch||!map->root||!map->root_boot||map->normal_until<=map->lo)return;
 struct rc_shadow_map m=*map;struct r2_capture now;
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(stats.enabled&&r2_capture_now(&now)&&now.epoch==m.epoch&&now.tick>=m.lo&&now.tick<m.normal_until){
  m.active=now.tick;history.map[history.head]=m;history.head=(history.head+1)%RC_SHADOW_HISTORY;
  if(history.count<RC_SHADOW_HISTORY)++history.count;
 }
 k_spin_unlock(&lock,key);
}
void r2_shadow_normal_withdraw(void){
 k_spinlock_key_t key=k_spin_lock(&lock);history.count=history.head=0;k_spin_unlock(&lock,key);
}
void r2_shadow_map(const struct rc5_clock *c,const struct rc5_state *s,uint64_t lo,uint64_t epoch){
 if(!c->count||!epoch)return;
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(!stats.enabled){k_spin_unlock(&lock,key);return;}
 struct rc_shadow_map m={.map=c->map[(c->head+3)%4],.lo=lo,.epoch=epoch,
  .parent=s->routes.node[s->routes.node[s->self].parent].id,.parent_boot=s->parent_boot,
  .root=s->reference.origin.root,.root_boot=s->root_boot,.root_epoch=s->root_epoch,
  .parent_epoch=s->parent_epoch,.train=s->parent_train,.accepted=s->accepted,
  .issued=s->reference.origin.issued,.source_expires=s->reference.origin.expires,.utc_offset=s->reference.origin.utc_offset};
 struct r2_capture now;
 if(r2_capture_now(&now)&&now.epoch==epoch&&now.tick>=lo){
  m.active=now.tick;history.map[history.head]=m;history.head=(history.head+1)%RC_SHADOW_HISTORY;
  if(history.count<RC_SHADOW_HISTORY)++history.count;
 }
 k_spin_unlock(&lock,key);
}
bool r2_shadow_enabled(void){return atomic_get(&requested)&&!atomic_get(&serving);}
void r2_shadow_edge(struct r2_capture edge,int64_t arrival){
 if(!r2_shadow_enabled())return;
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(!stats.enabled){sat(&stats.disabled);k_spin_unlock(&lock,key);return;}
 if(edge.epoch==last_epoch&&edge.tick==last_edge){sat(&stats.duplicates);k_spin_unlock(&lock,key);return;}
 last_epoch=edge.epoch;last_edge=edge.tick;sat(&stats.edges);
 if(qcount==RC_SHADOW_QUEUE)sat(&stats.drops);
 else {queue[qhead]=(struct rc_shadow_record){.edge=edge,.arrival_us=arrival,.sequence=stats.edges,.association=R2_ASSOC_UNKNOWN};qhead=(qhead+1)%RC_SHADOW_QUEUE;++qcount;}
 k_spin_unlock(&lock,key);
}
#if defined(R2_BOUNDARY_FIELD) || defined(R2_SHADOW_FIELD_MAP) || defined(R2_SHADOW_TEST_BOTH)
static void __attribute__((noinline)) shadow_field_evaluate(struct rc_shadow_record *r){
   /* This RC-only frame must never exist on the normal main-thread path. */
   struct rc5_clock c={.map={r->map.map},.count=1,.head=1,.hold_ticks=RC_SHADOW_HOLD_TICKS,
    .root_boot=r->map.root_boot,.root_epoch=r->map.root_epoch,.parent_boot=r->map.parent_boot,.parent_epoch=r->map.parent_epoch};
   struct rc5_state s={.bound=true,.root_boot=c.root_boot,.root_epoch=c.root_epoch,.parent_boot=c.parent_boot,.parent_epoch=c.parent_epoch};
   struct rc5_stamp stamp;
   if(rc5_clock_capture(&c,&s,r->edge.tick,&stamp)&&stamp.ns<=INT64_MAX){
    if(!r->map.source_expires||stamp.ns>=r->map.source_expires){r->reason=RC_SHADOW_EXPIRED;return;}
    r->valid=true;r->relay_ns=stamp.ns;r->pll=stamp.pll_tracking;
    uint64_t fraction=stamp.ns%1000000000;r->phase_ns=fraction>=500000000?(int64_t)fraction-1000000000:(int64_t)fraction;
    int64_t ns=(int64_t)stamp.ns,offset=r->map.utc_offset;
    if((offset>0&&ns>INT64_MAX-offset)||(offset<0&&ns<INT64_MIN-offset)){r->valid=false;r->reason=RC_SHADOW_ARITHMETIC;}
    else r->utc_ns=ns+offset;
   }else r->reason=RC_SHADOW_ARITHMETIC;
}
#endif
static void shadow_poll(bool allow_field){
 (void)allow_field;
 if(!r2_shadow_enabled())return;
 for(unsigned i=0;i<RC_SHADOW_QUEUE;i++){
  struct rc_shadow_record r;bool have=false;
  k_spinlock_key_t key=k_spin_lock(&lock);
  if(qcount){r=queue[qtail];qtail=(qtail+1)%RC_SHADOW_QUEUE;--qcount;r.reason=rc_shadow_select(&history,r.edge,&r.map);have=true;}
  k_spin_unlock(&lock,key);if(!have)break;
  if(r.reason==RC_SHADOW_OK){
   if(r.map.normal){
    int64_t ns=0;
    if(r2_servo_mapped(&r.map.normal_map,r.edge.tick,&ns)&&ns>=0){
     r.valid=true;r.relay_ns=(uint64_t)ns;uint64_t fraction=r.relay_ns%1000000000;
     r.phase_ns=fraction>=500000000?(int64_t)fraction-1000000000:(int64_t)fraction;
     if((r.map.utc_offset>0&&ns>INT64_MAX-r.map.utc_offset)||(r.map.utc_offset<0&&ns<INT64_MIN-r.map.utc_offset)){r.valid=false;r.reason=RC_SHADOW_ARITHMETIC;}
     else r.utc_ns=ns+r.map.utc_offset;
    }else r.reason=RC_SHADOW_ARITHMETIC;
   }else{
#if defined(R2_BOUNDARY_FIELD) || defined(R2_SHADOW_FIELD_MAP) || defined(R2_SHADOW_TEST_BOTH)
   if(allow_field)shadow_field_evaluate(&r);else r.reason=RC_SHADOW_ARITHMETIC;
#else
   r.reason=RC_SHADOW_ARITHMETIC;
#endif
   }
  }
  key=k_spin_lock(&lock);
  if(rcount==RC_SHADOW_RECORDS)sat(&stats.overwritten);else ++rcount;
  records[rhead]=r;rhead=(rhead+1)%RC_SHADOW_RECORDS;
  k_spin_unlock(&lock,key);
 }
}
void r2_shadow_poll(void){shadow_poll(true);}
void r2_shadow_poll_normal(void){shadow_poll(false);}
void r2_shadow_reference(struct r2_capture edge,const struct r2_clock *c){
 /* Later receiver metadata is linked to the original edge; it never changes
  * the frozen evaluated relay clock or enters its estimator. */
 k_spinlock_key_t key=k_spin_lock(&lock);if(!stats.enabled){k_spin_unlock(&lock,key);return;}unsigned at=RC_SHADOW_QUEUE;
 for(unsigned i=0;i<ref_count;i++){unsigned q=(ref_head+RC_SHADOW_QUEUE-1-i)%RC_SHADOW_QUEUE;
  if(refs[q].edge.epoch==edge.epoch&&refs[q].edge.tick==edge.tick){at=q;break;}}
 if(at==RC_SHADOW_QUEUE){at=ref_head;ref_head=(ref_head+1)%RC_SHADOW_QUEUE;if(ref_count<RC_SHADOW_QUEUE)++ref_count;}
 refs[at].edge=edge;refs[at].utc=c->last_rmc_utc;refs[at].at=c->last_rmc;
 refs[at].association=c->view.association;refs[at].reason=c->view.reason;refs[at].fix=c->view.fix_active;refs[at].pps=c->view.pps_present;
 k_spin_unlock(&lock,key);
}
static void disabled_clear(void){history.count=history.head=0;qcount=qhead=qtail=0;ref_count=ref_head=0;}
void r2_shadow_enable(bool enabled){
 if((bool)atomic_get(&requested)==enabled)return;
 k_spinlock_key_t key=k_spin_lock(&lock);atomic_set(&requested,enabled);
 stats.enabled=enabled&&!atomic_get(&serving);if(!stats.enabled)disabled_clear();k_spin_unlock(&lock,key);
}
void r2_shadow_serving(bool gm){
 if((bool)atomic_get(&serving)==gm)return;
 k_spinlock_key_t key=k_spin_lock(&lock);atomic_set(&serving,gm);
 stats.enabled=atomic_get(&requested)&&!gm;if(!stats.enabled)disabled_clear();k_spin_unlock(&lock,key);
}
bool r2_shadow_read(uint32_t after,struct rc_shadow_record *r,struct rc_shadow_status *out){
 struct r2_capture now={0};(void)r2_capture_now(&now);
 bool found=false;k_spinlock_key_t key=k_spin_lock(&lock);*out=stats;out->now_tick=now.tick;out->epoch=now.epoch;
 for(unsigned i=0;i<rcount;i++){const struct rc_shadow_record *q=&records[(rhead+RC_SHADOW_RECORDS-rcount+i)%RC_SHADOW_RECORDS];if(q->sequence>after){
   uint64_t now_us=k_ticks_to_us_floor64(k_uptime_ticks());
   if(q->arrival_us<0||now_us<(uint64_t)q->arrival_us||now_us-(uint64_t)q->arrival_us<800000)break;
   *r=*q;
   for(unsigned j=0;j<ref_count;j++){unsigned k=(ref_head+RC_SHADOW_QUEUE-1-j)%RC_SHADOW_QUEUE;
    if(refs[k].edge.epoch==r->edge.epoch&&refs[k].edge.tick==r->edge.tick){r->association=refs[k].association;r->reference_reason=refs[k].reason;r->reference_seen=true;
     r->reference_fix=refs[k].fix;r->reference_pps=refs[k].pps;r->rmc_utc_us=refs[k].utc;r->rmc_at_us=refs[k].at;break;}}
   found=true;break;}}
 k_spin_unlock(&lock,key);return found;
}
