/* Actual RC GM admission body and unchanged coarse-time gate, cold context. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rc_gm_adapter.h"
#include "timing_events.h"
#include "rc_stream_quality.h"
#include "rc_admission_policy.h"
static struct rc_stream_history stream_history;
static struct rc_stream_sample stream_sample;
static struct rc5_gm_context gm_context;
static uint32_t gm_admission_diagnostic[7];
static int state_lock,locked;
#define K_FOREVER 0
static void k_mutex_lock(int *p,int forever){(void)p;(void)forever;assert(!locked);locked=1;}
static void k_mutex_unlock(int *p){(void)p;assert(locked);locked=0;}
/* This regression is initial admission only: no retained context is supplied. */
static bool gm_renew_time(struct r2_join_time *t,const struct r2_join_record *r,uint64_t p,uint64_t n,uint64_t tick){(void)t;(void)r;(void)p;(void)n;(void)tick;return false;}
static struct r2_join_exchange gm_exchange;
static struct r2_group_config group={.psk={1},.id=5,.epoch=1};
static uint64_t lt_deadline,lt_next,lt_budget_until,last_context_progress,lt_renew_at;
static unsigned lt_rejects,lt_bindings,lt_attempts,lt_phase,deferred,nonroot;
#define LT_GM_PROOF 1
static uint64_t lt_now(void){return UINT64_C(16000000);}
static void startup_complete(void){}
static void candidate_defer(uint64_t id,uint64_t now){(void)id;(void)now;++deferred;}
static void candidate_nonroot(uint64_t id,uint64_t now){(void)id;(void)now;++nonroot;}
#include "rc_gm_admission_under_test.h"
int main(void){
 struct r2_join_record r={.kind=R2_JOIN_ACCEPT,.sender=1,.receiver=2,.sender_boot=3,.receiver_boot=4,.client_nonce=5,.server_nonce=6,.sequence=7,.created_us=1000000,.root=1,.root_boot=3,.source_epoch=9,.publication=1,.utc_us=1792078397000000,.remaining_us=1500000,.source_flags=1,.source_bound_us=40000};
 strcpy(r.name,"source");
 assert(gm_accept(&r,980000,1000000,16000000));assert(gm_context.authenticated&&gm_context.rebind);
 assert(gm_admission_diagnostic[0]==1&&gm_admission_diagnostic[1]==5&&gm_admission_diagnostic[2]==20000&&gm_admission_diagnostic[6]==50104&&!locked);
 gm_context=(struct rc5_gm_context){0};
 assert(!gm_accept(&r,800000,1000000,16000000));assert(!gm_context.authenticated&&gm_admission_diagnostic[1]==4&&gm_admission_diagnostic[6]>100000);
 r.root=7;r.root_boot=8;assert(!gm_accept(&r,980000,1000000,16000000));assert(gm_admission_diagnostic[1]==3&&!gm_context.authenticated);
 r.root=1;r.root_boot=3;r.remaining_us=50000;assert(!gm_accept(&r,980000,1000000,16000000));assert(gm_admission_diagnostic[1]==4&&!gm_context.authenticated);
 r.root=r.root_boot=0;r.source_flags=0;
 assert(!gm_accept(&r,980000,1000000,16000000)&&gm_admission_diagnostic[1]==6);
 r.root=1;r.root_boot=4;r.source_flags=1;
 assert(!gm_accept(&r,980000,1000000,16000000)&&gm_admission_diagnostic[1]==7);
 gm_admission_record(NULL,2,1,1);assert(gm_admission_diagnostic[1]==1&&gm_admission_diagnostic[2]==UINT32_MAX&&gm_admission_diagnostic[6]==UINT32_MAX&&!locked);
 /* Real authenticated NO_SOURCE accepts exchange identity but preserves the
  * productive context; replay/MAC failures cannot cause nonroot cooldown. */
 r=(struct r2_join_record){.kind=R2_JOIN_ACCEPT,.sender=1,.receiver=2,.sender_boot=3,.receiver_boot=4,.client_nonce=5,.server_nonce=6,.sequence=7,.created_us=1000000,.echo_us=980000};strcpy(r.name,"source");
 gm_exchange=(struct r2_join_exchange){.self=2,.peer=1,.boot=4,.peer_boot=3,.nonce=5,.remote_nonce=6,.until=2000000,.proof_at=980000,.challenged=true};
 gm_context=(struct rc5_gm_context){.authenticated=true,.at_tick=123};
 uint8_t wire[164];assert(!r2_group_join_encode(wire,&r,&group));
 gm_apply(wire);assert(gm_admission_diagnostic[1]==6&&!deferred&&!nonroot&&gm_context.authenticated&&gm_context.at_tick==123);
 unsigned rejects=lt_rejects;gm_apply(wire);assert(lt_rejects==rejects+1&&gm_admission_diagnostic[1]==2&&!nonroot);
 wire[148]^=1;gm_apply(wire);assert(gm_admission_diagnostic[1]==1&&!nonroot&&gm_context.authenticated);
 puts("PASS cold GM admission: actual unchanged100ms gate, expiry subtraction, non-root refusal and bounded causal diagnostics; diagnostics grant no authority");return 0;
}
