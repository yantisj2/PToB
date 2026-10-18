/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual matcher/control/recovery functions; simulated owner time, no radio. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "rc_dynamic.h"
#include "rc_admission_policy.h"
#include "rc_stream_quality.h"
#include "link_quality.h"
#include "timing_events.h"
static struct rc7_receiver receiver;
#define timing receiver.clock
static struct rc7_path upstream_path;
static struct rc5_routes routes;
static struct r2_group_config group={.id=7,.epoch=9,.psk={1,2,3}};
static struct rc_stream_history stream_history;
static struct rc_stream_sample stream_sample;
static struct r2_link_quality delivery;
static struct rc7_control lt_request;
static uint64_t parent_count0_at,self_identity=99,local_boot=88,fake_tick;
static uint64_t lt_renew_at,lt_budget_until,lt_attempt_ready,lt_next,lt_deadline,
    lt_request_at,last_context_progress,upstream_retry_at;
static unsigned lt_attempts,lt_sequence,lt_rejects,lt_bindings,parent_index=1,
    node=2,upstream_recoveries,parent_admission=2,advertises,pending,admissions;
static bool startup_admitted,parent_locked,parent_known,parent_relay,parent_synced,creating,
    upstream_usable,lt_acquiring,lt_paused;
static void *lt_conn;
static unsigned lt_phase;
enum {LT_CAP,LT_WRITE,LT_REPLY};
static uint64_t lt_now(void){return fake_tick;}
static uint64_t lt_random(void){return 7;}
static bool upstream_admitted(uint64_t tick){return timing.bound&&tick<timing.context_until;}
static void lt_quiesce(bool p){lt_paused=p;}
static void lt_advertise(uint64_t tick){(void)tick;++advertises;}
static void check(bool ok,const char *why){(void)why;assert(ok);}
static void candidate_defer(uint64_t id,uint64_t now){(void)id;(void)now;}
static bool stop(void){return true;}
static bool layout_path(const struct rc7_path *path){upstream_path=*path;return true;}
static void startup_complete(void){++admissions;}
static void parent_wait(unsigned stage){pending=stage;}
static void event_fake(unsigned k,int r,uint64_t p,uint32_t d,uint32_t a,uint32_t b)
{(void)k;(void)r;(void)p;(void)d;(void)a;(void)b;}
#define r2_timing_event event_fake
#include "rc_source_wait_under_test.h"
#define T(us) ((uint64_t)(us)*16)
static struct rc7_path path(void){
 return (struct rc7_path){.source_epoch=3,.depth=1,
  .node={{.id=11,.boot=12,.train=1,.epoch=4},
         {.id=22,.boot=23,.train=1,.epoch=4,.slot=1,.role=1}}};
}
static void setup(uint64_t tick){
 receiver=(struct rc7_receiver){0};upstream_path=path();
 struct rc5_admission a={.parent_boot=23,.root_boot=12,.root_epoch=3,
  .parent_train=1,.parent_epoch=4,.publication_floor=1,.now_tick=tick,
  .root_lo_ns=999900000,.root_hi_ns=1000100000,.root_expires_ns=2500000000};
 assert(rc7_bind(&receiver,&upstream_path,self_identity,true,2,&a));routes=timing.routes;
 parent_index=1;node=2;startup_admitted=parent_locked=parent_known=parent_relay=parent_synced=true;
 timing.accepted=100; /* Prior established corrections, currently withdrawn. */
 creating=upstream_usable=lt_acquiring=lt_paused=false;lt_conn=NULL;
 parent_count0_at=upstream_retry_at=lt_budget_until=lt_attempt_ready=lt_next=0;
 lt_attempts=lt_bindings=lt_rejects=admissions=pending=advertises=0;
 last_context_progress=tick;fake_tick=tick;
 lt_renew_at=tick+rc_admission_relay_renew_ticks();
}
static struct rc7_packet packet(unsigned publication){
 return(struct rc7_packet){.origin={.root=11,.boot=12,.epoch=3,
  .issued=1000000000,.expires=2500000000},.sender=22,.boot=23,
  .train=1,.epoch=4,.publication=publication,.depth=1,.slot=1};
}
static bool wire_receive(struct rc7_packet p,const struct rc7_path *wire_path,
                         uint64_t rx,uint16_t event,bool forge){
 uint8_t wire[190];assert(!rc7_origin_sign(p.proof,&p.origin,&group));
 assert(!rc7_encode(wire,&p,wire_path,&group));if(forge)wire[7]^=1;
 struct rc7_packet decoded;
 if(rc7_decode(&decoded,wire,sizeof wire,&upstream_path,&group))return false;
 (void)parent_receive_packet(&decoded,rx,event,2,rx+16);return true;
}
static void capability(struct rc7_path changed,uint64_t tick,bool forge){
 struct rc7_control c={.type=RC7_CAPABILITY,.sender=22,.sender_boot=23,
  .next_challenge=24,.publishing=true,.path=changed},decoded;
 uint8_t wire[144];assert(!rc7_capability_encode(wire,&c,&group));if(forge)wire[50]^=1;
 if(!rc7_capability_decode(&decoded,wire,sizeof wire,&group)&&parent_path_usable(&decoded.path))
  parent_scope_observed(&decoded,tick);
}
static void reply(bool ready,const struct rc7_path *next,uint64_t tick){
 fake_tick=tick;lt_request_at=tick-16;lt_phase=LT_REPLY;
 lt_request=(struct rc7_control){.type=RC7_REQUEST,.sender=self_identity,.sender_boot=local_boot,
  .target=22,.target_boot=23,.challenge=24,.nonce=25,.sequence=26};
 struct rc7_control c={.type=RC7_RESPONSE,.sender=22,.sender_boot=23,
  .target=self_identity,.target_boot=local_boot,.challenge=24,.nonce=25,.sequence=26};
 if(ready){c.ready=true;c.path=*next;c.publication_floor=1;c.root_lo_ns=999900000;
  c.root_hi_ns=1000100000;c.source_expires_ns=2500000000;}
 lt_apply_read(&c);
}
static void field_sequence(void){
 /* Recorded FIELD observations are inputs, not RF simulation. The original
  * bind instant was not exported:575s is an explicit test population chosen
  * so nominal240s renewal crosses the outage. Actual starts were805.123,
  *825.128,865.204s; old PENDING control took1.824/.548s, new READY1.187s.
  * New-path packets appeared by826.276s. Keep every original start fence. */
 setup(T(575000000));const uint64_t until=timing.context_until;
 uint64_t starts[2]={0},began[3]={0},end=0;unsigned n=0;bool finished=false,scope_seen=false;
 struct rc7_path next=upstream_path;next.node[0].id=33;next.node[0].boot=34;
 next.source_epoch=5;next.node[1].train=next.node[1].epoch=2;
 for(uint64_t us=805123000;us<870000000&&!finished;us+=1000){
  uint64_t now=T(us);fake_tick=now;
  if(us<817336000){
   parent_synced=true;
   if((us-805123000)%62000==0)assert(wire_receive(packet(2),&upstream_path,now,++lt_sequence,false));
  }else parent_synced=us>=826276000;
  if(us>=826276000&&!scope_seen){capability(next,now,false);scope_seen=true;}
  if(lt_conn){
   assert(now<=end+16000); /* Every fake transaction stays inside original2s. */
   if(now>=end){
    bool ready=us>=826276000;uint64_t original=timing.admission.now_tick;
    reply(ready,&next,now);lt_conn=NULL;
    if(ready){assert(lt_bindings==1&&timing.admission.now_tick==now);finished=true;}
    else{assert(!lt_bindings&&pending==1&&timing.admission.now_tick==original&&timing.context_until==until);}
   }
   continue;
  }
  parent_recovery_poll(now);
  bool need=parent_known&&!creating&&!lt_paused&&now>=lt_renew_at;
  if(lt_source_wait(now,need)||!need||!rc_admission_start_allowed(starts,now)||now<lt_budget_until)continue;
  assert(n<3);began[n]=now;starts[0]=starts[1];starts[1]=now;++n;++lt_attempts;
  lt_conn=&n;uint64_t duration=us>=826276000?1187000:n==1?1824000:548000;
  end=now+T(duration);lt_deadline=now+T(2000000);assert(end<lt_deadline);
 }
 printf("FIELD policy replay starts_us=%llu,%llu,%llu bindings=%u; assumed bind575s, not hardware\n",
  (unsigned long long)(began[0]/16),(unsigned long long)(began[1]/16),
  (unsigned long long)(began[2]/16),lt_bindings);fflush(stdout);
 /* Original source-blind policy consumes805/825starts and waits until865.
  * Candidate conserves the first recovery start, still renews at240s/815s,
  * and admits the recovered path on the second legal start around835s. */
 assert(finished&&n==2&&began[0]>=T(815000000)&&began[0]<T(815100000));
 assert(began[1]-began[0]>=T(20000000)&&began[1]<T(835100000));
 assert(!parent_count0_at&&!timing.accepted&&!timing.reference.valid);
}
static void matcher_cases(void){
 setup(T(1000000));uint64_t now=T(2000000),until=timing.context_until;
 assert(wire_receive(packet(2),&upstream_path,now,10,false));assert(parent_count0_at==now&&parent_source_wait(now+16));
 /* Identical PA bytes at a NEW physical event are legitimate admitted
  * originals; exact duplicate tuple/replayed event is not fresh evidence. */
 assert(wire_receive(packet(2),&upstream_path,now+100,11,false));assert(parent_count0_at==now+100);
 assert(wire_receive(packet(2),&upstream_path,now+200,11,false));assert(parent_count0_at==now+100);
 assert(wire_receive(packet(1),&upstream_path,now+300,12,false));assert(parent_count0_at==now+100);
 assert(wire_receive(packet(3),&upstream_path,now+400,13,false));uint64_t hint=parent_count0_at;
 assert(!wire_receive(packet(4),&upstream_path,now+500,14,true)&&parent_count0_at==hint);
 struct rc7_packet nonzero=packet(4);nonzero.count=1;nonzero.correction[0]=(struct rc5_correction){.publication=3,.event=13,.channel=2,.generation=1,.tx_ns=1000000000};
 assert(wire_receive(nonzero,&upstream_path,now+600,15,false)&&!parent_count0_at);
 assert(timing.accepted==100&&!timing.reference.valid&&timing.context_until==until);
 /* Public matcher arithmetic, pending ring wrap, future RX and badchannel
  * all execute the same production wrapper, not a mirrored count0 test. */
 timing.head=UINT32_MAX;assert(wire_receive(packet(5),&upstream_path,now+700,16,false));assert(!timing.head&&parent_count0_at==now+700);
 struct rc7_packet p=packet(6);(void)parent_receive_packet(&p,now+1000,17,37,now+1100);assert(parent_count0_at==now+700);
 (void)parent_receive_packet(&p,now+2000,17,2,now+1000);assert(parent_count0_at==now+700);
 assert(!parent_source_wait(now+700+T(500000)));
 puts("PASS actual encoded/authenticated matcher: newcount0, repeatedPAevent vs duplicate/replay, forged MAC, nonzero no authority, ringwrap, invalidRX/channel");
}
static void boundaries_and_scope(void){
 setup(T(1000000));uint64_t now=T(2000000);assert(wire_receive(packet(2),&upstream_path,now,10,false));
 assert(parent_source_wait(now+T(499999))&&!parent_source_wait(now+T(500000)));
 uint64_t renewal=timing.admission.now_tick+rc_admission_relay_renew_ticks();parent_count0_at=renewal-16;
 assert(parent_source_wait(renewal-1)&&!parent_source_wait(renewal));
 parent_count0_at=timing.context_until-16;assert(!parent_source_wait(timing.context_until));
 assert(!rc_admission_source_wait(true,true,true,UINT64_MAX-100,UINT64_MAX,UINT64_MAX-1,UINT64_MAX));
 assert(!rc_admission_source_wait(true,true,true,0,UINT64_MAX,UINT64_MAX,UINT64_MAX-1));
 assert(!rc_admission_source_wait(true,true,true,now+1,timing.context_until,now,now));
 parent_count0_at=now;parent_synced=false;assert(!parent_source_wait(now));parent_synced=true;
 startup_admitted=false;assert(!parent_source_wait(now));startup_admitted=true;
 parent_locked=false;assert(!parent_source_wait(now));parent_locked=true;
 timing.accepted=0;assert(!parent_source_wait(now));timing.accepted=100;
 parent_relay=false;assert(!parent_source_wait(now));parent_relay=true;
 parent_known=false;assert(!parent_source_wait(now));parent_known=true;
 timing.bound=false;assert(!parent_source_wait(now));timing.bound=true;
 parent_count0_at=0;assert(!parent_source_wait(now));
 /* Authenticated changed path arrives during original cooldown/inflight ACL.
  * New-path nonzero cannot pass old admission/path MAC; its decoded capability
  * independently latches change. Continuing old-pathcount0 cannot mask it. */
 struct rc7_path next=upstream_path;next.source_epoch=5;next.node[0].boot=44;
 struct rc7_packet p=packet(3);p.origin.boot=44;p.origin.epoch=5;p.count=1;
 p.correction[0]=(struct rc5_correction){.publication=2,.event=10,.channel=2,.generation=1,.tx_ns=1000000000};
 parent_count0_at=now;assert(!wire_receive(p,&next,now+16,11,false)&&parent_count0_at==now);
 capability(next,now+16,true);assert(parent_count0_at==now);
 upstream_retry_at=now+T(10000000);lt_conn=&next;lt_budget_until=now+T(20000000);lt_attempt_ready=now+80000;lt_attempts=2;
 uint64_t budget=lt_budget_until,jitter=lt_attempt_ready,renew=lt_renew_at;
 capability(next,now+32,false);assert(parent_count0_at==UINT64_MAX&&!parent_source_wait(now+32));
 assert(lt_conn==&next&&lt_budget_until==budget&&lt_attempt_ready==jitter&&lt_attempts==2&&lt_renew_at==renew);
 assert(wire_receive(packet(4),&upstream_path,now+64,12,false)&&parent_count0_at==UINT64_MAX);
 assert(!lt_source_wait(now+64,true));
 lt_conn=NULL;capability(next,now+80,false);assert(parent_count0_at==UINT64_MAX&&lt_budget_until==budget);
 reply(true,&next,now+100);assert(!parent_count0_at&&lt_bindings==1&&timing.bound&&!timing.accepted);
 puts("PASS actual scope/control:500msfreshness,240srenewal,originalexpiry,overflow/startup/PA/unbound; forged/changedCAP+inflight/cooldown cannot refresh hint or reset budgets; actual READYbind clears it");
}
int main(void){field_sequence();matcher_cases();boundaries_and_scope();puts("PASS actual source-wait recovery policy; no hardware/precision permission expansion");}
