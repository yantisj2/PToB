/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_owner.h"
#include "gm7_bridge.h"
#include "time_zone.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
static struct gm7_owner owner;
static struct r2_tz_lease coarse_tz;
#include "gm7_tz_observer_under_test.h"
int main(void) {
 struct gm7_bridge b={.kernel_lo=1000000000,.kernel_hi=1000000001,
                      .hf=9000000,.epoch=1};
 uint64_t rx=b.hf-100,unused;
 assert(!gm7_bridge_to_kernel(b,rx,&unused)); /* past receipt is not a deadline */
 owner.scope.active=11;owner.scope.boot=22;
 struct gm7_packet p={.kind=GM7_ANNOUNCE};
 p.body.control.active=11;p.body.control.active_boot=22;p.body.control.tz=r2_tz_manual();
 /* received=0 is the actual owner's accepted/authenticated boundary. */
 under_test(p,0,b,rx);
 assert(r2_tz_current(&coarse_tz,b.kernel_hi).id==2);
 assert(coarse_tz.until_us==b.kernel_hi+3*R2_TZ_GM_PERIOD_US);
 uint64_t until=coarse_tz.until_us;
 under_test(p,-ESTALE,b,rx);assert(coarse_tz.until_us==until);
 coarse_tz=(struct r2_tz_lease){0};p.body.control.active=12;
 under_test(p,0,b,rx);assert(!coarse_tz.value.id);
 p.body.control.active=11;p.body.control.active_boot=23;
 under_test(p,0,b,rx);assert(!coarse_tz.value.id);
 p.body.control.active_boot=22;p.kind=GM7_STATUS;
 under_test(p,0,b,rx);assert(!coarse_tz.value.id);
 p.kind=GM7_ANNOUNCE;under_test(p,0,b,rx);
 assert(r2_tz_current(&coarse_tz,b.kernel_hi+500000).id==2);
 assert(!r2_tz_current(&coarse_tz,b.kernel_hi+750000).id);
 b.kernel_lo+=500000;b.kernel_hi+=500000;under_test(p,0,b,rx);
 assert(r2_tz_current(&coarse_tz,b.kernel_hi+749999).id==2);
 puts("PASS production standby GM coarse lease uses kernel uptime, selected root and accepted ANN gates");
}
