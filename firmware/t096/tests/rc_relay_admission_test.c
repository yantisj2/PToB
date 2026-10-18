/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis. Actual relay admission refusal body. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rc_dynamic.h"
#include "rc_admission_policy.h"
#include "link_quality.h"
#include "timing_events.h"
#include "rc_stream_quality.h"
static struct rc_stream_history stream_history;
static struct rc_stream_sample stream_sample;
static struct rc7_receiver receiver;
static struct rc7_path upstream_path;
#define timing receiver.clock
static struct rc5_routes routes;
static struct r2_link_quality delivery;
static struct rc7_control lt_request;
static uint64_t self_identity=99,local_boot=88,lt_next,lt_deadline,lt_renew_at,
  lt_request_at,lt_budget_until,last_context_progress,deferred,deferral_at;
static uint64_t parent_count0_at;
static uint32_t lt_sequence;
static unsigned parent_index,node,lt_phase,lt_rejects,lt_bindings,lt_attempts,
  stops,pending,admissions;
static bool parent_known=true,faulted;
enum {LT_CAP,LT_WRITE,LT_REPLY};
static uint64_t lt_now(void){return UINT64_C(16000000);}
static uint64_t lt_random(void){return 7;}
static void check(bool ok,const char *reason){(void)reason;if(!ok)faulted=true;}
static void candidate_defer(uint64_t id,uint64_t now){deferred=id;deferral_at=now;}
static bool stop(void){++stops;return true;}
static bool layout_path(const struct rc7_path *path){(void)path;return true;}
static void startup_complete(void){++admissions;}
static void parent_wait(unsigned stage){pending=stage;}
static void r2_timing_event_fake(unsigned k,int r,uint64_t p,uint32_t d,uint32_t a,uint32_t b)
{(void)k;(void)r;(void)p;(void)d;(void)a;(void)b;}
#define r2_timing_event r2_timing_event_fake
#include "rc_relay_admission_under_test.h"
int main(void){
  struct rc7_control c={.type=RC7_CAPABILITY,.sender=22,.sender_boot=23,
    .next_challenge=24,.path={.source_epoch=3,.depth=1,
      .node={{.id=11,.boot=12,.train=1,.epoch=4,.role=0},
             {.id=22,.boot=23,.train=1,.epoch=4,.slot=1,.role=1}}}};
  routes.node[0].id=22;lt_phase=LT_CAP;timing.bound=true;
  lt_apply_read(&c);
  assert(lt_phase==LT_WRITE&&!lt_rejects&&!deferred&&!stops&&parent_known);
  assert(lt_request.target==22&&lt_request.target_boot==23&&lt_request.sender==self_identity);
  // A valid authenticated path can become a loop between discovery and read.
  // Refuse it, withdraw time, and defer its locator without forcing GM-only recovery.
  c.path.node[0].id=self_identity;lt_phase=LT_CAP;lt_deadline=100;
  assert(rc7_path_valid(&c.path));lt_apply_read(&c);
  assert(lt_rejects==1&&!lt_deadline&&!parent_known&&!timing.bound&&stops==1);
  assert(deferred==22&&deferral_at==1000000&&!faulted);
  // A depth-two parent remains eligible for receive-only use.
  c.path.node[0].id=11;c.path.node[1].id=33;c.path.node[1].boot=34;
  c.path.depth=2;c.path.node[2]=(struct rc7_node){.id=22,.boot=23,.train=1,.epoch=4,.slot=3,.role=1};
  parent_known=true;lt_phase=LT_CAP;deferred=0;lt_apply_read(&c);
  assert(lt_phase==LT_WRITE&&lt_rejects==1&&!deferred&&parent_known);
  // PENDING grants no time and is handled within the parent's bounded trial.
  c=(struct rc7_control){.type=RC7_RESPONSE,.sender=lt_request.target,
    .target=self_identity,.sender_boot=lt_request.target_boot,.target_boot=local_boot,
    .challenge=lt_request.challenge,.nonce=lt_request.nonce,.sequence=lt_request.sequence};
  lt_phase=LT_REPLY;lt_apply_read(&c);
  assert(pending==1&&!admissions&&!lt_bindings&&!timing.bound&&parent_known&&!faulted);
  puts("PASS actual relay admission: loop rejection defers locator, depth-two parent is allowed, PENDING grants no authority");
}
