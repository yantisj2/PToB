/* Execute the actual RC-only selector functions extracted by the runner. */
#include <assert.h>
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
static unsigned parent_index;
#include "selection_under_test.h"
static unsigned checks;
#define CHECK(x) do{assert(x);++checks;}while(0)

static void clear(void) {
 memset(candidates,0,sizeof candidates);candidate_since=upstream_bad_since=0;
 parent_selected_at=parent_stage_at=0;fake_now=0;
 upstream_usable=relay_fallback=startup_admitted=startup_attempted=false;
 exclude_direct=parent_known=parent_synced=parent_failed=parent_locked=parent_relay=parent_ineligible=false;
 forced_parent=parent_admission=0;delivery=(struct r2_link_quality){0};
 stream_history=(struct rc_stream_history){0};stream_sample=(struct rc_stream_sample){0};
 captured=tracking=fit.valid=false;current_origin.expires=UINT64_MAX;
}
static void seen(uint64_t id,bool relay,bool publishing,int8_t rssi,uint64_t now){bt_addr_le_t address={{0}};candidate_seen(&address,id,relay,publishing,rssi,now);}
static uint64_t choose(uint64_t now){struct candidate *c=candidate_choose(now,false);return c?c->id:0;}
static void bind(bool relay) {
 clear();fake_now=1000000;candidate_since=1;parent_selected_at=parent_stage_at=upstream_bad_since=fake_now;
 parent_known=parent_synced=startup_attempted=startup_admitted=true;
 parent_admission=2;parent_relay=relay;routes.node[0].id=11;
 captured=fit.valid=true;r2_link_quality_bind(&delivery,11,1,1);
 delivery_tick(fake_now,fake_now);
}
static void sample(unsigned seconds,bool flicker) {
 static uint16_t event;
 for(unsigned i=0;i<seconds*16;i++) {
  fake_now+=62500;tracking=!flicker||((fake_now/1000000)%2==0);
  r2_link_quality_rx(&delivery,1,++event,fake_now);
  delivery_tick(fake_now,fake_now);
 }
}
int main(int argc,char **argv) {
 CHECK(rc_admission_g1_direct(1,true)&&rc_admission_g1_direct(129,true));
 CHECK(!rc_admission_g1_direct(65,true)&&!rc_admission_g1_direct(193,true));
 CHECK(!rc_admission_g1_direct(2,true)&&!rc_admission_g1_direct(1,false));
 clear();fake_now=4000000;candidate_since=1;
 seen(11,false,false,-20,fake_now);seen(11,true,true,-70,fake_now);
 CHECK(candidates[0].id==11&&!candidates[0].relay&&candidates[1].id==11&&candidates[1].relay);
 parent_known=startup_attempted=true;routes.node[0].id=11;
 candidate_nonroot(11,fake_now);delivery_tick(fake_now,fake_now);
 CHECK(candidates[0].cooldown==64000000&&!candidates[1].cooldown);
 CHECK(choose(fake_now)==11&&candidate_choose(fake_now,false)->relay);
 if(argc==2&&!strcmp(argv[1],"flicker")) {
  bind(false);sample(40,true);CHECK(stream_sample.unstable);CHECK(relay_fallback);
  return 0;
 }
 clear();seen(11,false,false,-90,1000000);seen(22,true,true,-20,1000000);
 CHECK(!choose(3999999));seen(11,false,false,-90,4000000);seen(22,true,true,-20,4000000);CHECK(choose(4000000)==11);
 parent_known=startup_attempted=true;routes.node[0].id=11;candidate_defer(11,4000000);CHECK(!choose(4000001));
 relay_fallback=true;CHECK(choose(4000001)==22);CHECK(rc_stream_blocked(&stream_history,11,false,4000001));
 clear();fake_now=4000000;parent_known=startup_attempted=true;routes.node[0].id=11;
 parent_selected_at=parent_stage_at=candidate_since=1000000;
 seen(11,false,false,-20,fake_now);seen(33,false,false,-70,fake_now);
 candidate_nonroot(11,fake_now);CHECK(parent_ineligible&&!parent_acquiring(fake_now));
 delivery_tick(fake_now,fake_now);CHECK(relay_fallback&&choose(fake_now)==33);
 CHECK(candidates[0].cooldown==64000000); // authenticated non-root cannot monopolize G1
 clear();seen(22,true,false,-20,1000000);seen(22,true,false,-20,4000000);CHECK(!choose(4000000));
 seen(33,true,true,-70,4000000);CHECK(choose(4000000)==33);
 clear();seen(11,false,false,-20,1000000);seen(22,true,true,-80,4000000);CHECK(choose(4000000)==22); // stale GM
 clear();seen(11,false,false,-20,1000000);seen(22,true,true,-80,1000000);forced_parent=22;CHECK(choose(1040000)==22);
 forced_parent=0;exclude_direct=true;CHECK(choose(1040000)==22);
 bind(false);sample(40,true);CHECK(stream_sample.unstable&&relay_fallback);
 tracking=true;delivery_tick(fake_now+1,fake_now+1);CHECK(upstream_usable&&relay_fallback); // flicker cannot erase failure
 seen(11,false,false,-20,fake_now);seen(22,true,true,-75,fake_now);CHECK(choose(fake_now)==22);CHECK(rc_stream_blocked(&stream_history,11,false,fake_now));
 bind(true);sample(40,false);CHECK(upstream_usable&&!relay_fallback&&!stream_sample.unstable);
 delivery.view.poor=true;++fake_now;delivery_tick(fake_now,fake_now);CHECK(!relay_fallback); // packet score alone cannot evict useful time
 seen(33,false,false,-20,fake_now);CHECK(!choose(fake_now)); // healthy relay stays despite returning GM
 sample(2,true);CHECK(!relay_fallback);sample(10,false);CHECK(!stream_sample.unstable&&!relay_fallback); // brief fading recovers
 bind(true);tracking=true;delivery_tick(2000000,2000000);tracking=false;
 delivery_tick(3000000,3000000);delivery_tick(12000000,12000000);CHECK(!relay_fallback); // first brief lock retains relay acquisition allowance
 delivery_tick(30999999,30999999);CHECK(!relay_fallback);delivery_tick(31000000,31000000);CHECK(relay_fallback);
 bind(false);captured=false;delivery_tick(12999999,12999999);CHECK(!relay_fallback);
 delivery_tick(13000000,13000000);CHECK(relay_fallback); // unsuccessful direct trial is shorter than relay trial
 bind(true);parent_admission=0;fake_now=21000000;parent_wait(1);CHECK(parent_stage_at==fake_now);
 fake_now=29000000;parent_wait(1);CHECK(parent_stage_at==21000000); // repeated PENDING cannot rearm
 fake_now=41000000;startup_complete();CHECK(parent_stage_at==fake_now&&parent_admission==2);
 fake_now=51000000;startup_complete();CHECK(parent_stage_at==41000000);
 captured=false;delivery_tick(60999999,60999999);CHECK(!relay_fallback);delivery_tick(61000000,61000000);CHECK(relay_fallback); // total attempt bounded
 bind(true);sample(40,true);CHECK(relay_fallback);sample(10,false);CHECK(!relay_fallback&&!stream_sample.unstable); // sustained recovery cancels search
 bind(false);sample(40,true);CHECK(relay_fallback);parent_synced=false;++fake_now;delivery_tick(fake_now,fake_now);CHECK(!stream_sample.unstable&&stream_sample.count==0); // pause is unknown, not RF loss
 clear();startup_attempted=true;relay_fallback=true;seen(22,true,false,-40,1000000);seen(33,true,true,-60,1000000);CHECK(choose(1040000)==33); // active relay ahead of heard-only idle peer
 printf("PASS %u actual RC marginal-link selector assertions\n",checks);
}
