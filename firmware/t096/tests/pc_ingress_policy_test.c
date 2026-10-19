/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "pc_experiment.h"
#include "rc_stream_quality.h"
#include <assert.h>
#include <stdio.h>
static bool upstream_usable,relay_fallback,parent_known=true,parent_synced=true;
static bool parent_relay,parent_locked=true,parent_failed,parent_ineligible;
static unsigned parent_admission=2,parent_index,rebinds;
static uint64_t upstream_bad_since,last_context_progress,lt_attempts,lt_budget_until,lt_attempt_ready;
static struct rc5_routes routes={.count=2,.node={{.id=42}}};
static struct rc5_state timing;
static struct rc5_rate_fit fit={.valid=true};
static struct rc5_clock local_clock;
static struct rc5_origin current_origin={.expires=UINT64_MAX};
static struct r2_link_quality delivery;
static struct rc_stream_history stream_history;
static struct rc_stream_sample stream_sample;
static bool admitted=true;
static bool rc5_clock_capture_fake(const struct rc5_clock *c,const struct rc5_state *s,uint64_t tick,struct rc5_stamp *stamp){(void)c;(void)s;(void)tick;(void)stamp;return false;}
#define rc5_clock_capture rc5_clock_capture_fake
static bool parent_acquiring(uint64_t now){(void)now;return false;}
static bool parent_source_wait(uint64_t tick){(void)tick;return false;}
static bool upstream_admitted(uint64_t tick){(void)tick;return admitted;}
static void parent_rebind_request(uint64_t tick){(void)tick;++rebinds;}
/* Projection is irrelevant to these parent policy bodies; the real expired
 * delivered-map result is supplied at the clock-capture boundary above. */
bool rc5_map_project(const struct rc5_map *m,uint64_t tick,uint64_t *ns,uint64_t *elapsed){(void)m;(void)tick;(void)ns;(void)elapsed;return false;}
#include "pc_ingress_policy_under_test.h"
int main(void){
 r2_link_quality_bind(&delivery,42,43,1);
 for(unsigned i=1;i<=12;i++){
  uint64_t us=(uint64_t)i*1000000,tick=us*16;
  timing.bound=true;timing.context_until=tick+160000000;
  timing.reference.valid=true;timing.reference.completed_at=tick-1000;
  r2_link_quality_rx(&delivery,1,(uint16_t)i,us);
  delivery_tick(us,tick);
  assert(!upstream_usable); /* Map/output is still expired after accounting. */
  assert(!relay_fallback&&!parent_loss_due(us));
  parent_recovery_poll(tick);assert(!rebinds);
 }
 uint64_t tick=timing.reference.completed_at+8000000;
 parent_recovery_poll(tick);assert(rebinds==1); /* Real absent ingress recovers. */
 timing.reference.completed_at=tick;timing.context_until=tick;
 admitted=false;parent_recovery_poll(tick);assert(rebinds==2); /* Expiry wins. */
 puts("PASS actual PC parent policies: fresh ingress retains parent with expired output map; no stalled-fit JOIN; true outage/context expiry recover");
}
