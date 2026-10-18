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
int main(int argc,char **argv) {
 if(argc==2&&!strcmp(argv[1],"dwell")) {
  bind(true);captured=false;delivery_tick(31000000,31000000);
  CHECK(!relay_fallback);return 0;
 }
 if(argc==2&&!strcmp(argv[1],"relay-first")) {
  clear();startup_attempted=relay_fallback=true;
  seen(11,false,false,-20,1000000);seen(22,true,true,-100,1000000);
  CHECK(choose(1040000)==22);return 0;
 }
 if(argc==2&&!strcmp(argv[1],"recovery")) {
  bind(true);sample(120,false);captured=false;delivery_tick(fake_now,fake_now);
  fake_now+=10000000;delivery_tick(fake_now,fake_now);
  CHECK(!upstream_usable&&!relay_fallback);return 0;
 }
 if(argc==2&&!strcmp(argv[1],"renewal-recovery")) {
  bind(true);sample(120,false);captured=false;delivery_tick(fake_now,fake_now);
  fake_now+=90000000;delivery_tick(fake_now,fake_now);
  CHECK(!upstream_usable&&!relay_fallback);return 0;
 }
 if(argc==2&&!strcmp(argv[1],"known-gm-retry")) {
  bind(false);sample(120,false);CHECK(parent_locked);
  captured=false;delivery_tick(fake_now,fake_now);fake_now+=6000000;delivery_tick(fake_now,fake_now);
  seen(11,false,false,-20,fake_now);seen(22,true,true,-70,fake_now);CHECK(choose(fake_now)==22);
  CHECK(!rc_stream_blocked(&stream_history,11,false,fake_now+60000000));return 0;
 }
 if(argc==2&&!strcmp(argv[1],"refused-path")) {
  clear();startup_attempted=true;
  seen(11,false,false,-20,1000000);seen(22,true,true,-80,1000000);
  CHECK(choose(1040000)==22);return 0;
 }
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
  bind(false);sample(60,true);CHECK(stream_sample.unstable);CHECK(relay_fallback);
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
 bind(false);sample(60,true);CHECK(stream_sample.unstable&&relay_fallback&&!parent_locked);
 tracking=true;delivery_tick(fake_now+1,fake_now+1);CHECK(upstream_usable&&relay_fallback); // flicker cannot erase failure
 seen(11,false,false,-20,fake_now);seen(22,true,true,-75,fake_now);CHECK(choose(fake_now)==22);CHECK(rc_stream_blocked(&stream_history,11,false,fake_now));
 bind(true);sample(40,false);CHECK(upstream_usable&&!relay_fallback&&!stream_sample.unstable);
 delivery.view.poor=true;++fake_now;delivery_tick(fake_now,fake_now);CHECK(!relay_fallback); // packet score alone cannot evict useful time
 seen(33,false,false,-20,fake_now);CHECK(!choose(fake_now)); // healthy relay stays despite returning GM
 sample(2,true);CHECK(!relay_fallback);sample(10,false);CHECK(!stream_sample.unstable&&!relay_fallback); // brief fading recovers
 bind(true);tracking=true;delivery_tick(2000000,2000000);CHECK(!parent_locked);tracking=false;
 delivery_tick(3000000,3000000);delivery_tick(12000000,12000000);CHECK(!relay_fallback); // first brief lock retains relay acquisition allowance
 delivery_tick(120999999,120999999);CHECK(!relay_fallback);delivery_tick(121000000,121000000);CHECK(relay_fallback);
 bind(false);captured=false;delivery_tick(60999999,60999999);CHECK(!relay_fallback);
 delivery_tick(61000000,61000000);CHECK(relay_fallback); // full minute for admitted GM
 bind(true);parent_admission=0;fake_now=21000000;parent_wait(1);CHECK(parent_stage_at==fake_now);
 fake_now=29000000;parent_wait(1);CHECK(parent_stage_at==21000000); // repeated PENDING cannot rearm
 fake_now=41000000;startup_complete();CHECK(parent_stage_at==fake_now&&parent_admission==2);
 fake_now=51000000;startup_complete();CHECK(parent_stage_at==41000000);
 captured=false;delivery_tick(160999999,160999999);CHECK(!relay_fallback);delivery_tick(161000000,161000000);CHECK(relay_fallback); // full two minutes after admission
 bind(true);sample(120,true);CHECK(relay_fallback);sample(10,false);CHECK(!relay_fallback&&!stream_sample.unstable&&parent_locked); // sustained recovery cancels search
 bind(false);sample(60,true);CHECK(relay_fallback);parent_synced=false;++fake_now;delivery_tick(fake_now,fake_now);CHECK(!stream_sample.unstable&&stream_sample.count==0); // pause is unknown, not RF loss
 clear();startup_attempted=true;relay_fallback=true;seen(22,true,false,-40,1000000);seen(33,true,true,-60,1000000);CHECK(choose(1040000)==33); // active relay ahead of heard-only idle peer

 // Repeated PENDING cannot turn a silent relay into an indefinite reservation.
 bind(true);parent_admission=0;parent_synced=false;
 fake_now=59000000;parent_wait(1);CHECK(parent_acquiring(60999999));
 fake_now=61000000;parent_wait(1);delivery_tick(fake_now,fake_now);CHECK(relay_fallback);
 CHECK(!parent_acquiring(fake_now)&&parent_stage_at==59000000);
 // Admission at the edge still gets its complete precision trial; renewal is inert.
 bind(true);parent_admission=0;fake_now=60999999;startup_complete();
 fake_now=150000000;startup_complete();CHECK(parent_stage_at==60999999);
 CHECK(parent_acquiring(180999998)&&!parent_acquiring(180999999));
 // Late replies cannot extend the absolute three-minute ceiling.
 bind(true);parent_admission=0;fake_now=100000000;startup_complete();
 CHECK(parent_acquiring(180999999)&&!parent_acquiring(181000000));
 bind(false);parent_admission=0;CHECK(parent_acquiring(30999999)&&!parent_acquiring(31000000));
 fake_now=30999999;startup_complete();CHECK(parent_acquiring(90999998)&&!parent_acquiring(90999999));

 // A failed GM -> nearby relay -> late lock, with two stronger competitors.
 bind(false);sample(60,true);CHECK(relay_fallback);
 seen(11,false,false,-20,fake_now);seen(22,true,true,-45,fake_now);
 seen(33,true,true,-65,fake_now);CHECK(choose(fake_now)==22);
 select_parent(22,true);
 captured=false;
 for(unsigned second=0;second<90;second++) {
  fake_now+=1000000;delivery_tick(fake_now,fake_now);
  seen(11,false,false,-20,fake_now);seen(33,true,true,-20,fake_now);
  CHECK(!relay_fallback&&!choose(fake_now)); // no RSSI-driven switch during settling
 }
 captured=true;sample(10,false);CHECK(parent_locked&&!relay_fallback);
 sample(210,false); // original failed GM deferral has now elapsed
 CHECK(!rc_stream_blocked(&stream_history,11,false,fake_now));
 seen(11,false,false,-20,fake_now);seen(33,true,true,-20,fake_now);
 CHECK(!choose(fake_now)); // healthy relay remains after GM becomes eligible

 // A settled relay gets120s even when the rolling admission budget delays starts.
 captured=false;delivery_tick(fake_now,fake_now);uint64_t lost=fake_now;
 uint64_t starts[2]={(lost-35000000)*16,(lost-5000000)*16};unsigned attempts=0;
 for(unsigned second=1;second<120;second++) {
  fake_now=lost+second*UINT64_C(1000000);delivery_tick(fake_now,fake_now);
  seen(11,false,false,-20,fake_now);seen(33,true,true,-20,fake_now);
  CHECK(!upstream_usable&&!relay_fallback&&!choose(fake_now));
  if(rc_admission_start_allowed(starts,fake_now*16)) {
   starts[0]=starts[1];starts[1]=fake_now*16;++attempts;
  }
  if(second==40||second==80)startup_complete(); // renewal cannot rearm recovery
 }
 CHECK(attempts==4);
 fake_now=lost+119999999;delivery_tick(fake_now,fake_now);CHECK(!relay_fallback);
 fake_now=lost+120000000;delivery_tick(fake_now,fake_now);CHECK(relay_fallback);
 seen(11,false,false,-20,fake_now);seen(33,true,true,-100,fake_now);
 CHECK(choose(fake_now)==33); // relay wins even over a stronger, retry-eligible GM
 CHECK(rc_stream_blocked(&stream_history,22,true,fake_now));
 captured=true;sample(10,false);CHECK(!relay_fallback&&!stream_sample.unstable);

 // Intermittent lock cannot reset an ongoing degraded relay episode forever.
 bind(true);sample(120,false);CHECK(parent_locked&&!relay_fallback);
 sample(135,true);CHECK(relay_fallback&&stream_sample.unstable);
 // A recovering relay is retained when its stream actually settles again.
 sample(10,false);CHECK(!relay_fallback);
 // Source expiry withdraws usable time immediately, independently of retention.
 current_origin.expires=0;++fake_now;delivery_tick(fake_now,fake_now);
 CHECK(!upstream_usable&&!relay_fallback);
 // If all relays are deferred, a fresh GM remains available as a bounded retry.
 clear();startup_attempted=relay_fallback=true;
 seen(22,true,true,-20,1000000);seen(33,false,false,-90,1000000);
 rc_stream_failed(&stream_history,22,true,1000000);CHECK(choose(1040000)==33);
 // Relay path refusal drops parent_known before its dwell can expire. Recovery
 // still admits alternate relays rather than silently filtering them all out.
 bind(true);seen(11,true,true,-20,fake_now);seen(22,true,true,-80,fake_now);
 seen(33,false,false,-20,fake_now);candidate_defer(11,fake_now);
 parent_known=false;CHECK(!relay_fallback&&choose(fake_now)==22);
 fake_now+=1;seen(11,true,true,-20,fake_now);CHECK(choose(fake_now)==22);
 exclude_direct=true;CHECK(choose(fake_now)==22); // diagnostic role filter remains
 // A different, proven usable GM keeps preference when recovery is necessary.
 clear();startup_attempted=relay_fallback=true;
 seen(11,false,false,-100,1000000);seen(22,true,true,-20,1000000);
 struct rc_stream_row *gm=rc_stream_obtain(&stream_history,11,false,1000000);
 gm->delivery=1000;gm->delivery_samples=100;CHECK(choose(1040000)==22); // RF alone is not time
 gm->usable=1000;gm->usable_samples=1;CHECK(choose(1040000)==22); // a flicker is not sustained lock
 gm->usable_samples=8;CHECK(choose(1040000)==11); // sustained usable GM wins despite weak RSSI
 rc_stream_failed(&stream_history,11,false,1040000);CHECK(choose(1040001)==22);
 // Expired retry deferral permits a trial but cannot erase the recorded failure.
 fake_now=302000000;seen(11,false,false,-20,fake_now);seen(22,true,true,-100,fake_now);
 CHECK(!rc_stream_blocked(&stream_history,11,false,fake_now)&&choose(fake_now)==22);
 rc_stream_failed(&stream_history,22,true,fake_now);CHECK(choose(fake_now)==11); // keep GM in mix

 // A previously settled GM can retry earlier after interruption. A relay still
 // owns its full120s trial even when that strong GM becomes eligible mid-trial.
 bind(false);sample(120,false);CHECK(parent_locked);
 captured=false;delivery_tick(fake_now,fake_now);uint64_t gm_lost=fake_now;
 fake_now=gm_lost+5999999;delivery_tick(fake_now,fake_now);CHECK(!relay_fallback&&!upstream_usable);
 fake_now=gm_lost+6000000;delivery_tick(fake_now,fake_now);CHECK(relay_fallback);
 seen(11,false,false,-20,fake_now);seen(22,true,true,-70,fake_now);CHECK(choose(fake_now)==22);
 uint64_t gm_failed=fake_now;
 CHECK(rc_stream_blocked(&stream_history,11,false,gm_failed+59999999));
 CHECK(!rc_stream_blocked(&stream_history,11,false,gm_failed+60000000));
 CHECK(!rc_stream_proven(&stream_history,11,false,gm_failed+60000000));
 select_parent(22,true);captured=false;uint64_t relay_selected=fake_now;
 for(unsigned second=1;second<120;second++) {
  fake_now=relay_selected+second*UINT64_C(1000000);delivery_tick(fake_now,fake_now);
  seen(11,false,false,-20,fake_now);seen(33,true,true,-20,fake_now);
  CHECK(!relay_fallback&&!choose(fake_now));
 }
 fake_now=relay_selected+120000000;delivery_tick(fake_now,fake_now);
 seen(11,false,false,-20,fake_now);seen(33,true,true,-70,fake_now);
 CHECK(relay_fallback&&choose(fake_now)==33); // eligible GM cannot erase its penalty
 rc_stream_failed(&stream_history,33,true,fake_now);CHECK(choose(fake_now)==11);
 // A GM that only flickered remains on the original five-minute deferral.
 bind(false);sample(60,true);CHECK(relay_fallback&&!parent_locked);
 seen(11,false,false,-20,fake_now);seen(22,true,true,-70,fake_now);CHECK(choose(fake_now)==22);
 CHECK(rc_stream_blocked(&stream_history,11,false,fake_now+60000000));
 CHECK(!rc_stream_blocked(&stream_history,11,false,fake_now+300000000));
 printf("PASS %u actual RC marginal-link selector assertions\n",checks);
}
