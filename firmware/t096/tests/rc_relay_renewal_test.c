/* Copyright (c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "rc_dynamic.h"
#include "rc_admission_policy.h"
#define R2_TIMING_EVENTS_TEST 1
#include "timing_events.h"
static uint64_t lt_renew_at,lt_budget_until,nonce;
static unsigned lt_attempts,parent_index;
static bool startup_admitted,parent_known=true,parent_relay=true;
static struct rc5_state timing;
static struct rc5_routes routes;
static uint64_t lt_random(void){return nonce;}
void r2_timing_event(unsigned kind,int result,uint64_t peer,uint32_t detail,uint32_t a,uint32_t b){
 (void)result;(void)peer;(void)detail;(void)a;(void)b;assert(kind==R2_TIMING_RC_RETRY);
}
#include "rc_relay_retry_under_test.h"
static struct rc5_admission admission(void){
 return (struct rc5_admission){.parent_boot=2,.root_boot=2,.root_epoch=3,
  .parent_train=1,.parent_epoch=4,.now_tick=16000000,
  .root_lo_ns=999900000,.root_hi_ns=1000100000,.root_expires_ns=2500000000};
}
static void replay_failed_renewals(void){
 const struct rc5_admission a=admission();memset(&timing,0,sizeof timing);assert(rc5_bind(&timing,&a));
 const uint64_t until=timing.context_until;uint64_t starts[2]={0};
 uint64_t now=a.now_tick+rc_admission_relay_renew_ticks();
 // Saved C1 failures:2085ms ATT refusal then27207ms backoff; next2001ms
 // connect timeout then24397ms backoff. Repeat at policy-relative times,
 // preserving actual durations and rolling start gates; no RF simulation.
 const uint64_t durations[]={2085000,2001000},backoffs[]={27207000,24397000};
 startup_admitted=true;parent_relay=true;lt_attempts=0;
 for(unsigned i=0;i<2;i++){
  while(!rc_admission_start_allowed(starts,now))now+=16000;
  assert(now<until);starts[0]=starts[1];starts[1]=now;++lt_attempts;
  now+=durations[i]*16;nonce=backoffs[i]-20000000;
  lt_retry_after(now,0,0);assert(lt_renew_at==now+backoffs[i]*16);
  assert(timing.context_until==until);now=lt_renew_at;
 }
 while(!rc_admission_start_allowed(starts,now))now+=16000;
 assert(now-starts[0]>=UINT64_C(960000000)&&now-starts[1]>=UINT64_C(320000000));
 // A new original2s challenge still fits the ORIGINAL authentication context.
 // Starting at300s fails this same numerical deadline, even after retry cap.
 assert(now+UINT64_C(2000000)*16<until);
 assert(timing.context_until==until&&timing.admission.now_tick==a.now_tick);
 printf("PASS recorded-duration RC7 renewal replay: next_start_us=%llu original_context_left_us=%llu\n",
  (unsigned long long)((now-a.now_tick)/16),(unsigned long long)((until-now)/16));
}
int main(void){
 replay_failed_renewals();
 const struct rc5_admission a=admission();assert(rc5_bind(&timing,&a));const uint64_t until=timing.context_until;
 startup_admitted=true;parent_relay=true;lt_attempts=2;nonce=20000000;
 uint64_t tick=until-UINT64_C(15000000)*16;
 lt_retry_after(tick,0,0);assert(lt_renew_at==until-UINT64_C(3000000)*16&&timing.context_until==until);
 // Batch reset does not move authentication expiry or reset absolute starts.
 uint64_t starts[2]={tick-UINT64_C(21000000)*16,tick-UINT64_C(1000000)*16};
 lt_attempts=3;lt_retry_after(tick,0,0);assert(!lt_attempts&&lt_budget_until==lt_renew_at);
 assert(!rc_admission_start_allowed(starts,lt_renew_at));
 for(tick=until-UINT64_C(3000000)*16;tick<=until+16;tick+=UINT64_C(1000000)*16){
  lt_retry_after(tick,0,0);assert(lt_renew_at==tick&&timing.context_until==until);
 }
 // Neither urgent retries nor original-source lifetime confer time. Execute
 // the production codec/matcher refusal at the unchanged context boundary.
 struct rc7_path path={.source_epoch=3,.node={{.id=1,.boot=2,.train=1,.epoch=4}}};
 struct rc7_receiver receiver={0};assert(rc7_bind(&receiver,&path,5,true,1,&a));
 struct rc7_packet packet={.origin={.root=1,.boot=2,.epoch=3,.issued=1000000000,.expires=2500000000},
  .sender=1,.boot=2,.train=1,.epoch=4,.publication=1};
 assert(!rc7_receive(&receiver,&packet,until,10,2,until)&&!receiver.clock.accepted);
 assert(receiver.clock.context_until==until&&receiver.clock.admission.root_expires_ns==a.root_expires_ns);
 // Other admission classes keep their earlier jitter policy.
 parent_relay=false;startup_admitted=true;lt_attempts=1;lt_retry_after(until,0,0);
 assert(lt_renew_at==until+rc_admission_warm_retry_us(nonce)*16);
 parent_relay=true;startup_admitted=false;lt_retry_after(until,0,0);
 assert(lt_renew_at==until+rc_admission_retry_us(lt_attempts,nonce)*16);
 startup_admitted=true;timing.bound=false;lt_retry_after(until,0,0);
 assert(lt_renew_at==until+rc_admission_warm_retry_us(nonce)*16);
 assert(rc_admission_relay_retry_us(20000000,UINT64_MAX-160,UINT64_MAX)==0);
 puts("PASS actual RC7 retry caller: deadline reserve, expiry refusal, rolling budget, batches and GM/cold/unbound separation; no hardware");
}
