/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis. Actual selector and signed-role ingress. */
#define main previous_selector_main
#include "rc_startup_selection_test.c"
#undef main
#include "role_announcement.h"
static struct r2_group_config group={.id=5,.epoch=1,.psk={1}};
static struct {bool authenticated;struct {uint64_t root_boot;uint32_t source_epoch;} source;} gm_context;
#define RC_DYNAMIC_CAPABILITY_BYTES 144u
#define BT_DATA_MANUFACTURER_DATA 255
struct bt_data {uint8_t type,data_len;const uint8_t *data;};
static uint32_t k_uptime_get_32(void){return (uint32_t)(fake_now/1000);}
static int bt_addr_le_cmp(const bt_addr_le_t *a,const bt_addr_le_t *b){return memcmp(a,b,sizeof *a);}
#include "gm_role_under_test.h"
static struct discovery announcement(uint64_t id,uint64_t boot,uint32_t sequence,uint64_t at){
 struct r2_role_record r={.sender=id,.root=id,.sender_boot=boot,.root_boot=boot,
  .source_epoch=3,.sequence=sequence,.created_us=at,.valid_us=2000000,.flags=R2_ROLE_SERVING};
 uint8_t wire[R2_ROLE_AD_BYTES];assert(!r2_role_encode(wire,&r,&group));
 struct discovery d={0};struct bt_data ad={.type=255,.data_len=sizeof wire-2,.data=wire+2};
 fake_now=at;assert(!scan_data(&ad,&d));assert(d.len==sizeof wire-2&&d.role.received_ms==at/1000);
 assert(!memcmp(d.data,wire+2,sizeof wire-2));return d;
}
int main(void){
 /* A new serving GM beats a protected relay trial and its 10s collection. */
 bind(true);parent_admission=0;parent_synced=false;candidate_since=0;
 seen(22,false,false,-65,fake_now);struct discovery d=announcement(22,200,1,fake_now);
 gm_role_observed(&d,fake_now);assert(choose(fake_now)==22&&rc_gm_hint_fresh(&gm_failover,1000));
 assert(!upstream_usable&&!tracking); /* role reports grant no time */
 /* Even a healthy relay yields one direct authentication opportunity. */
 clear();parent_known=parent_relay=upstream_usable=true;routes.node[0].id=11;
 seen(22,false,false,-65,1000000);d=announcement(22,200,1,1000000);gm_role_observed(&d,1000000);assert(choose(1000000)==22);
 /* No relays: an old HOT/non-root cooldown and failure history do not hide promotion. */
 bind(false);parent_admission=0;seen(11,false,false,-30,fake_now);candidate_nonroot(11,fake_now);
 rc_stream_admission_failed(&stream_history,11,false,fake_now);
 d=announcement(11,100,1,fake_now);gm_role_observed(&d,fake_now);assert(choose(fake_now)==11);
 assert(rc_stream_blocked(&stream_history,11,false,fake_now)); /* history preserved */
 /* Selected state reaches the start gate despite old-parent starts1s/21s. */
 clear();seen(22,false,false,-30,22000000);d=announcement(22,200,1,22000000);gm_role_observed(&d,22000000);
 assert(parent_failover_waiting());parent_known=true;parent_relay=false;routes.node[0].id=22;
 gm_failover.flags|=RC_GM_SELECTED;assert(parent_failover_start()&&!parent_failover_waiting());
 uint64_t starts[2]={UINT64_C(1000000)*16,UINT64_C(21000000)*16};
 assert(!rc_admission_start_allowed(starts,UINT64_C(22000000)*16));
 parent_failover_attempt(true);assert(!parent_failover_start());
 d=announcement(22,200,2,23000000);gm_role_observed(&d,23000000);assert(!(gm_failover.flags&RC_GM_PENDING));
 d=announcement(33,300,1,24000000);seen(33,false,false,-30,24000000);gm_role_observed(&d,24000000);
 gm_failover.flags|=RC_GM_SELECTED;assert(!rc_gm_expedited(&gm_failover,24000));
 assert(!rc_gm_reserve_available(&gm_failover,81999)&&rc_gm_reserve_available(&gm_failover,82000));
 /* Stale queues, wrong endpoint/group/MAC, duplicate sequences, and overrides. */
 for(unsigned negative=0;negative<7;negative++){
  clear();seen(22,false,false,-30,1000000);d=announcement(22,200,1,1000000);
  if(negative==0)d.data[20]^=1;
  if(negative==1)d.address.bytes[0]=1;
  if(negative==2)group.epoch=2;
  if(negative==3)forced_parent=11;
  if(negative==4)exclude_direct=true;
  if(negative==5){gm_role_observed(&d,1000000);gm_failover.flags=RC_GM_TRIED;}
  gm_role_observed(&d,negative==6?3000000:1000000);
  assert(!(gm_failover.flags&RC_GM_PENDING));group.epoch=1;
 }
 /* Authenticated current direct scope and repeated old packets never retrigger. */
 clear();parent_known=true;routes.node[0].id=22;gm_context.authenticated=true;
 gm_context.source.root_boot=200;gm_context.source.source_epoch=3;
 seen(22,false,false,-30,1000000);d=announcement(22,200,1,1000000);gm_role_observed(&d,1000000);
 assert(!(gm_failover.flags&RC_GM_PENDING));
 gm_context.authenticated=false;
 /* Cooldown rollover remains bounded; an absent old locator cannot revive it. */
 clear();uint64_t start=(UINT64_C(1)<<32)*1000-2000000;
 seen(11,false,false,-30,start);candidate_defer(11,start);
 seen(11,false,false,-30,start+4000000);assert(candidates[0].cooling);
 seen(11,false,false,-30,start+5000000);assert(!candidates[0].cooling);
 assert(sizeof(struct candidate)==32&&sizeof(struct rc_gm_failover)==40);
 puts("PASS signed active-GM ingress/endpoint/MAC/replay/age; immediate direct preference/protected relay/nonroot cooldown; one extra admission per60s; no timing authority; fixed RAM");
}
