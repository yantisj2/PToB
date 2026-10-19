/* Execute the actual RC-only selector functions extracted by the runner. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint8_t bytes[7];} bt_addr_le_t;
static uint64_t self_identity=99,forced_parent;
static bool exclude_direct,parent_known;
static uint64_t fake_now;
static uint64_t mono_us(void){return fake_now;}
#include "rc_stream_quality.h"
#include "rc_search.h"
#include "rc_gm_failover.h"
#include "watchdog_policy.h"
#include "timing_events.h"
static unsigned failure_events;
static void selection_event(unsigned k,int result,uint64_t peer,uint32_t why,uint32_t stage,uint32_t age) {
 (void)k;(void)result;(void)peer;(void)stage;(void)age;
 if(why==4)++failure_events;
}
#define r2_timing_event selection_event
#include "rc_admission_policy.h"
static struct r2_link_quality delivery;
static struct {bool valid;} fit;
static struct {uint64_t expires;} current_origin;
static unsigned local_clock,timing;
struct rc5_stamp {uint64_t ns;bool pll_tracking;};
static bool captured,tracking;
static bool parent_synced;
static bool rc5_clock_capture(void *c,void *t,uint64_t tick,struct rc5_stamp *stamp){(void)c;(void)t;*stamp=(struct rc5_stamp){.ns=tick,.pll_tracking=tracking};return captured;}
static struct {struct {uint64_t id;} node[4];} routes;
#include "selection_under_test.h"
_Static_assert(sizeof(node)==1&&sizeof(parent_index)==1&&sizeof(parent_admission)==1,
               "Actual bounded owner storage");
static unsigned checks;
#define CHECK(x) do{assert(x);++checks;}while(0)

static void clear(void) {
 memset(candidates,0,sizeof candidates);candidate_since=upstream_bad_since=0;
 gm_failover=(struct rc_gm_failover){0};
 parent_selected_at=parent_stage_at=0;fake_now=0;
 upstream_usable=relay_fallback=startup_admitted=startup_attempted=false;
 exclude_direct=parent_known=parent_synced=parent_failed=parent_locked=parent_relay=parent_ineligible=false;
 forced_parent=parent_admission=0;delivery=(struct r2_link_quality){0};
 stream_history=(struct rc_stream_history){0};stream_sample=(struct rc_stream_sample){0};
 captured=tracking=fit.valid=false;current_origin.expires=UINT64_MAX;
}
static void seen(uint64_t id,bool relay,bool publishing,int8_t rssi,uint64_t now){bt_addr_le_t address={{0}};candidate_seen(&address,id,relay,publishing,rssi,now);}
static uint64_t choose(uint64_t now){struct candidate *c=candidate_choose(now,false);return c?c->id:0;}
static void select_parent(uint64_t id,bool relay) {
 parent_selected_at=parent_stage_at=upstream_bad_since=fake_now;
 parent_known=parent_synced=startup_attempted=startup_admitted=true;
 parent_failed=parent_locked=parent_ineligible=tracking=false;
 upstream_usable=relay_fallback=false;stream_sample=(struct rc_stream_sample){0};
 delivery=(struct r2_link_quality){0};
 parent_admission=2;parent_relay=relay;routes.node[0].id=id;
 captured=fit.valid=true;r2_link_quality_bind(&delivery,id,1,1);
 delivery_tick(fake_now,fake_now);
}
static void bind(bool relay) {
 clear();fake_now=1000000;candidate_since=1;select_parent(11,relay);
}
static void sample(unsigned seconds,bool flicker) {
 static uint16_t event;
 for(unsigned i=0;i<seconds*16;i++) {
  fake_now+=62500;tracking=!flicker||((fake_now/1000000)%2==0);
  r2_link_quality_rx(&delivery,1,++event,fake_now);
  delivery_tick(fake_now,fake_now);
 }
}
/* Complete actual host-owner exposure, with no controller airtime claim. */
static void collect(uint64_t start) {
 for(unsigned i=0;i<=160;i++)rc_search_observe(&parent_search,true,start+i*62500);
}
int main(void) {
 (void)node;
 clear();fake_now=1;parent_wait(1);CHECK(parent_admission==1);
 fake_now=2;parent_wait(2);CHECK(parent_admission==2&&parent_stage_at==2);
 fake_now=3;parent_wait(3);CHECK(parent_admission==3&&parent_stage_at==3);
 fake_now=4;parent_wait(1);CHECK(parent_admission==3&&parent_stage_at==3);
 clear();seen(22,true,false,-40,1000000);
 CHECK(!choose(4000000));collect(1000000);CHECK(choose(11000000)==22);
 // A selected parent ends collection; paused scans cannot manufacture exposure.
 candidate_defer(22,11000000);CHECK(!candidate_since&&!parent_search.owned);
 struct rc_search scan={0};rc_search_observe(&scan,true,1000000);
 rc_search_observe(&scan,false,2000000);rc_search_observe(&scan,true,3000000);
 CHECK(!scan.owned);CHECK(!rc_search_ready(&scan,1000000,60999999));
 CHECK(rc_search_ready(&scan,1000000,61000000));
 // Confidence-weighted usable time outranks one dB of extra RSSI.
 clear();startup_attempted=relay_fallback=true;
 seen(22,true,true,-40,1000000);seen(33,true,true,-41,1000000);
 struct rc_stream_row *bad=rc_stream_obtain(&stream_history,22,true,1000000);
 bad->usable=100;bad->delivery=500;bad->usable_samples=bad->delivery_samples=32;
 struct rc_stream_row *good=rc_stream_obtain(&stream_history,33,true,1000000);
 good->usable=good->delivery=1000;good->usable_samples=good->delivery_samples=32;
 collect(1000000);CHECK(choose(11000000)==33);
 // Every recovery recollects, retaining a locator from early in that scan.
 candidate_defer(33,11000000);seen(44,true,false,-30,12000000);
 CHECK(!choose(15000000));collect(12000000);CHECK(choose(22000000)==44);
 clear();seen(11,false,false,-60,1000000);seen(22,true,false,-30,1000000);
 collect(1000000);CHECK(choose(11000000)==11); // Cold GM preference preserved.
 // A positively authenticated non-root cannot trap direct-root search.
 bind(false);fake_now=4000000;candidate_nonroot(11,fake_now);
 CHECK(parent_ineligible);delivery_tick(fake_now,fake_now);CHECK(relay_fallback);
 // Full admission, then first PA establishes its single five-minute stage.
 bind(true);parent_admission=0;parent_synced=false;
 uint64_t selected=parent_selected_at;
 fake_now=selected+179000000;CHECK(parent_acquiring(fake_now));
 parent_wait(1);uint64_t pending=parent_stage_at;
 fake_now++;parent_wait(1);CHECK(parent_stage_at==pending);
 startup_complete();CHECK(parent_admission==2);
 fake_now=selected+250000000;parent_wait(3);
 uint64_t first_pa=parent_stage_at;
 fake_now=first_pa+299999999;parent_wait(3);
 CHECK(parent_stage_at==first_pa&&parent_acquiring(fake_now));
 fake_now++;CHECK(!parent_acquiring(fake_now));
 fake_now=selected+599999999;CHECK(!parent_acquiring(fake_now));
 parent_stage_at=fake_now;CHECK(parent_acquiring(fake_now));
 fake_now++;CHECK(!parent_acquiring(fake_now)); // Absolute ceiling wins.
 // Sustained lock holds through a long degraded episode and stronger peers.
 bind(true);sample(10,false);CHECK(parent_locked&&!relay_fallback);
 seen(22,false,false,-20,fake_now);collect(fake_now);
 CHECK(!choose(fake_now+10000000));
 tracking=false;delivery_tick(fake_now,fake_now);
 sample(299,true); // Flickering lock cannot erase an unstable episode.
 CHECK(!relay_fallback);
 tracking=false;sample(20,true);CHECK(relay_fallback);
 seen(33,true,false,-50,fake_now);collect(fake_now);
 CHECK(choose(fake_now+10000000)==33);unsigned events=failure_events;
 CHECK(events==1);CHECK(choose(fake_now+10000001)==33&&failure_events==events);
 CHECK(rc_stream_blocked(&stream_history,11,true,fake_now+10000001));
 // RC1 and RC2 are the same relay candidate class. The path-role suite checks
 // authenticated RC2 depth and leaf layout. Long intermittent RC1 trouble
 // must reach automatic search before the watchdog can restart the scope.
 bind(true);parent_wait(3);struct r2_wd_policy wd={0};
 for(unsigned i=0;i<320u*16u;i++){
  fake_now+=62500;tracking=(fake_now/1000000)%2==0;
  r2_link_quality_rx(&delivery,1,(uint16_t)i,fake_now);
  delivery_tick(fake_now,fake_now);
  CHECK(r2_wd_policy_step(&wd,(uint32_t)(fake_now/1000),true,true,
      upstream_usable,false,0)==R2_WD_NONE);
 }
 CHECK(relay_fallback&&!wd.retries);
 seen(11,true,true,-25,fake_now); // Current RC1 remains loud but unreliable.
 seen(22,true,true,-40,fake_now); // Nearby eligible RC2 gets no depth bonus.
 collect(fake_now);CHECK(choose(fake_now+10000000)==22);
 CHECK(rc_stream_blocked(&stream_history,11,true,fake_now+10000000));
 // With no trouble, the current parent remains retained despite alternatives.
 bind(true);sample(10,false);seen(22,true,true,-20,fake_now);collect(fake_now);
 CHECK(!choose(fake_now+10000000));
 // Valid output still disappears immediately when its source expires.
 bind(true);sample(10,false);CHECK(upstream_usable);
 current_origin.expires=fake_now;delivery_tick(fake_now,fake_now);
 CHECK(!upstream_usable&&!relay_fallback);
 // Explicit parent identity accepts the same exact ID through its observed
 // GM or RC class. It does not change the default automatic retention policy.
 CHECK(rc_admission_g1_direct(1,true));
 CHECK(!rc_admission_g1_direct(65,true)); // Passive client is not a directGM.
 CHECK(!rc_admission_g1_direct(1,false));
 CHECK(!rc_admission_g1_direct(0,true));
 clear();forced_parent=11;seen(11,false,false,-60,1000000);
 seen(22,false,false,-20,1000000);seen(33,true,true,-20,1000000);
 CHECK(choose(1040000)==11);CHECK(!candidate_choose(1040001,false)->relay);
 unsigned kept=0;for(unsigned i=0;i<8;i++)kept+=candidates[i].id!=0;CHECK(kept==1);
 clear();forced_parent=22;seen(11,false,false,-20,1000000);
 seen(22,true,false,-60,1000000);CHECK(choose(1040000)==22);
 CHECK(candidate_choose(1040001,false)->relay);
 clear();forced_parent=44;seen(11,false,false,-20,1000000);
 seen(22,true,true,-20,1000000);CHECK(!choose(1040000));
 // An enrollment that excludes directGM still excludes it when ID is forced.
 clear();forced_parent=11;exclude_direct=true;
 seen(11,false,false,-20,1000000);CHECK(!choose(1040000));
 // Stale and nonfresh forced-ID locators are not manufactured into a parent.
 clear();forced_parent=11;seen(11,false,false,-20,1000000);
 CHECK(!choose(1200000));
 clear();forced_parent=11;seen(11,false,false,-20,1000000);
 candidate_defer(11,1000001);CHECK(!choose(1040000));
 // Explicit relay-only diagnostics retain their fast collection and role fence.
 clear();exclude_direct=true;seen(11,false,false,-20,1000000);
 seen(22,true,false,-40,1000000);CHECK(choose(1040000)==22);
 printf("PASS %u production field-selection assertions\n",checks);
 return 0;
}
