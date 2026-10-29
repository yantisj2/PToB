/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_owner.h"
#include "gm7_bridge.h"
#include "time_zone_selector.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct gm7_owner owner;
static struct r2_group_config group;
static struct r2_tz_lease coarse_tz;
static struct {struct r2_tz tz;uint64_t tz_until_us;} status;
static struct {uint64_t utc_us;bool source;} c;
static struct {uint32_t map_bound_us;} roster;
static uint64_t identity=11,boot=22;
static const char *name="TEST";
static int stopping;
static bool resuming,output=true;
static int atomic_get(const int *value){return *value;}
bool gm7_owner_output(const struct gm7_owner *value,uint64_t now){
 (void)value;(void)now;return output;
}
/* External admission/clock boundaries; the code under test is copied from the
 * production owner. This fixture confers no real GNSS/timing authority. */
struct r2_gm7_admission_source {
 struct r2_tz tz;const struct r2_group_config *group;
 uint64_t identity,boot,utc_us,sampled_us,until_us;
 uint32_t timing_epoch,train,map_bound_us;const char *name;bool serving;
};
static struct r2_tz_selector selected={.selected=2,.confirmed_at=1000000001};
static struct r2_tz r2_time_tz_at(uint64_t now,uint64_t utc,bool valid){
 return r2_tz_selector_value(&selected,R2_TZ_AUTOMATIC,(struct r2_tz){0},now,utc,valid);
}
#include "gm7_tz_display_under_test.h"
int main(void){
 struct gm7_bridge b={.kernel_lo=1000000000,.kernel_hi=1000000001,.hf=9000000,.epoch=1};
 owner.scope.active=identity;owner.scope.timing_epoch=1;owner.scope.train=1;
 owner.source.sampled_us=b.hf-1000;owner.source.until_us=b.hf+1000000;
 /* Observed production case: own GM UTC is usable; follower c is absent. */
 assert(!c.source&&!c.utc_us);
 const uint64_t transition=UINT64_C(1793516400000000);
 owner.source.utc_us=transition-1-1000;
 struct gm7_source before=owner.source;
 publish_tz(b);assert(status.tz.id==2&&status.tz.offset_min==-300);
 assert(!memcmp(&before,&owner.source,sizeof before)&&!c.source&&!c.utc_us);
 owner.source.utc_us++;before=owner.source;
 publish_tz(b);assert(status.tz.id==2&&status.tz.offset_min==-360);
 assert(!memcmp(&before,&owner.source,sizeof before)&&!c.source&&!c.utc_us);
 stopping=1;publish_tz(b);assert(!status.tz.id);stopping=0;
 resuming=true;publish_tz(b);assert(!status.tz.id);resuming=false;
 output=false;publish_tz(b);assert(!status.tz.id);output=true;
 owner.source.utc_us=UINT64_MAX;publish_tz(b);assert(!status.tz.id);
 owner.source.utc_us=transition;owner.source.sampled_us=b.hf+1;
 publish_tz(b);assert(!status.tz.id);
 owner.source.sampled_us=b.hf;owner.source.until_us=b.hf-1;
 publish_tz(b);assert(!status.tz.id);
 owner.scope.active=33;
 assert(r2_tz_observe(&coarse_tz,(struct r2_tz){2,-300},b.kernel_hi,b.kernel_hi,R2_TZ_GM_PERIOD_US));
 publish_tz(b);assert(status.tz.id==2&&status.tz.offset_min==-300);
 puts("PASS actual serving-GM TZ publication uses own UTC without follower; DST/withdrawal preserve UTC");
}
