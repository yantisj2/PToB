/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Production RC7 scalar publication/query bodies + actual RC5 projection. */
#include "gm7_service_fake.h"
#include "ble_service.h"
#include "rf_clock.h"
#include "rc_boundary.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct r2_service_status status;
static struct rc5_clock local_clock;
static struct rc5_state timing;
static struct rc5_origin current_origin;
static bool running=true;
static uint32_t capture_epoch=7;
static uint64_t discipline_at=15000000;
static atomic_t fault,stop_requested;
K_MUTEX_DEFINE(state_lock);
#define RC_FIELD_HOLD_TICKS UINT64_C(8000000)
#include "rf_rc_under_test.h"
int main(void){
 struct r2_capture at={16000000,7,R2_CAPTURE_NOW};uint64_t now=1000000;
 local_clock=(struct rc5_clock){.head=1,.count=1,.root_boot=88,.root_epoch=9,.parent_boot=99,.parent_epoch=10};
 struct rc5_map *m=&local_clock.map[0];*m=(struct rc5_map){.effective=15000000,.anchor_tick=14000000,.anchor_ns=UINT64_C(1000000000013),.ppb=1000,.phase_ns=123,.generation=77};
 timing=(struct rc5_state){.root_boot=88,.root_epoch=9,.parent_boot=99,.parent_epoch=10,.context_until=23000000};
 current_origin=(struct rc5_origin){.root=11,.boot=88,.epoch=9,.utc_offset=INT64_C(1700000000000000000)};
 uint64_t expected,elapsed;assert(rc5_map_project(m,at.tick,&expected,&elapsed));
 status=(struct r2_service_status){.active=true,.follower_clock_valid=true,.time_sampled_us=now,.service_until=now+500000,.root_boot=88,.source_epoch=9};
 status.relay.root_identity=11;status.relay.upstream_train=12;status.relay.source_created_ns=expected-100000000;
 status.relay.source_expires_ns=expected+100000000;
 struct rc5_stamp stamp={.generation=77};rf_clock_publish(at,true,&stamp);
 assert(rf_rc.epoch==7&&rf_rc.until_tick==23000000);
 struct rc5_clock old=local_clock;struct rc5_state prior=timing;struct r2_service_status previous=status;
 struct r2_rf_clock_sample s;r2_service_rf_clock(&s,at,now);
 assert(s.available&&s.fresh&&s.utc&&s.rate_valid&&!s.provisional&&s.root==11&&s.boot==88&&s.train==12&&s.source_epoch==9);
 assert(s.time_ns==expected+(uint64_t)current_origin.utc_offset&&s.time_ns%1000!=0);
 assert(s.rate_ppb==-876);assert(s.fresh_until_us>now&&s.fresh_until_us<=now+100001);
 assert(!memcmp(&old,&local_clock,sizeof old)&&!memcmp(&prior,&timing,sizeof prior)&&!memcmp(&previous,&status,sizeof previous));
 r2_service_rf_clock(&s,(struct r2_capture){at.tick,8,R2_CAPTURE_NOW},now);assert(!s.available);
 status.relay.held_time=true;r2_service_rf_clock(&s,at,now);assert(!s.available);status.relay.held_time=false;
 status.relay.source_expires_ns=expected;r2_service_rf_clock(&s,at,now);assert(!s.available);status=previous;
 status.relay.source_created_ns=expected+1;r2_service_rf_clock(&s,at,now);assert(!s.available);status=previous;
 atomic_set(&stop_requested,1);r2_service_rf_clock(&s,at,now);assert(!s.available);atomic_clear(&stop_requested);
 status.closing=true;r2_service_rf_clock(&s,at,now);assert(!s.available);status=previous;
 current_origin.boot++;rf_clock_publish(at,true,&stamp);assert(!rf_rc.epoch);current_origin.boot--;
 rf_clock_publish(at,false,&stamp);assert(!rf_rc.epoch);
 puts("PASS actual RC7 selected-map scalar publication/query; exactns+deliveredrate, originalscope/counter/context/source bounds, no held/source/owner mutation");return 0;
}
