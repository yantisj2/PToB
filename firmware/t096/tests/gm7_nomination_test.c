/* Actual production nomination function; synthetic peer observations, no hardware. */
#include "../protocol/gm7_owner.c"
#include <assert.h>
#include <stdio.h>
static struct gm7_peer peer(struct gm7_owner *o,uint64_t id,uint64_t until,unsigned variance){
 struct gm7_peer p={.used=true,.identity=id,.boot=id+100,.kind=GM7_STATUS,
 .flags=GM7_ELIGIBLE|GM7_SHADOW|GM7_CAPABLE|GM7_HOT};
 p.received.control=(struct gm7_control){.sender=id,.boot=id+100,.sequence=1,
 .birth_us=o->scope.birth,.founder=o->scope.founder,.term=o->scope.term,
 .active=o->scope.active,.active_boot=o->scope.boot,.timing_epoch=o->scope.timing_epoch,
 .source_gen=3,.capture_gen=4,.train_gen=o->scope.train,.shadow_gen=1,
 .priority1=128,.clock_class=2,.accuracy=255,.priority2=128,.variance=variance};
 p.received.control_until=until;return p;
}
static struct gm7_owner setup(void){
 struct gm7_owner o={.identity=81,.boot=97,.serving=true,.now=100000000,
 .scope={.birth=1792800000000000,.founder=81,.term=1,.active=81,.boot=97,
 .timing_epoch=1,.source_gen=3,.capture_gen=4,.train=1},
 .source={.quality={128,2,255,128,65535}}};
 o.peer[0]=peer(&o,900,o.now+4500000,65535);nominate(&o);assert(o.nominee==900);return o;
}
int main(void){
 struct gm7_owner o=setup();uint64_t start=o.now;
 o.peer[1]=peer(&o,901,start+100000000,65535);
 o.now+=4500000;nominate(&o);assert(o.nominee==900);
 assert(!remote_prepared(&o,&o.peer[0]) && o.nominee_ready_at==start);
 o.now=start+GM7_NOMINEE_GRACE_US-1;nominate(&o);assert(o.nominee==900);
 o.now++;nominate(&o);assert(o.nominee==901 && o.nominee_reason==GM7_N_STATUS_TIMEOUT);
 puts("PASS stale STATUS retains preference only; grace expiry selects ready replacement with cause");
 o=setup();o.peer[0].flags &= ~GM7_SHADOW;nominate(&o);assert(o.nominee==900);
 o.now+=1000000;o.peer[0].flags |= GM7_SHADOW;nominate(&o);assert(o.nominee_ready_at==o.now);
 o.peer[0].flags &= ~GM7_ELIGIBLE;nominate(&o);assert(!o.nominee && o.nominee_reason==GM7_N_INELIGIBLE);
 puts("PASS transient SHADOW loss recovers; explicit ineligibility removes immediately");
 o=setup();o.peer[0].received.control.term++;nominate(&o);assert(!o.nominee && o.nominee_reason==GM7_N_SCOPE);
 o=setup();o.peer[0].quarantined=true;nominate(&o);assert(!o.nominee && o.nominee_reason==GM7_N_DEPARTED);
 puts("PASS scope change and boot quarantine cannot retain stale preference");
 o=setup();o.peer[0].received.control_until=o.now+20000000;
 o.peer[0].received.control.variance=100;o.peer[1]=peer(&o,901,o.now+20000000,99);
 nominate(&o);o.now+=10000000;nominate(&o);assert(o.nominee==900);
 o.peer[1].received.control.priority1=127;nominate(&o);assert(o.nominee==900);
 o.peer[0].received.control_until=o.peer[1].received.control_until=o.now+20000000;
 o.now+=9999999;nominate(&o);assert(o.nominee==900);
 o.now++;nominate(&o);assert(o.nominee==901 && o.nominee_reason==GM7_N_POLICY);
 puts("PASS marginal variance is sticky; configured priority must persist10s");
 o=setup();o.peer[1]=peer(&o,899,o.now+20000000,65535);
 o.peer[0].received.control_until=o.now+20000000;
 nominate(&o);o.now+=10000000;nominate(&o);assert(o.nominee==900);
 puts("PASS equal-quality lower identity never preempts current ready HOT");
}
