/* Actual RC7 UI binding: lamps are transport evidence, independent of maps. */
#include "ble_service.h"
#include "precision.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct r2_service_status fixture;
static uint64_t current=1000000;
static bool parent_known=true,uc_serving;
bool r2_uc_serving_clients(void){return uc_serving;}
static unsigned node=1,parent_index=0;
static struct {struct {uint64_t id;unsigned depth;} node[4];} routes;
static uint64_t mono_us(void){return current;}
void r2_service_snapshot(struct r2_service_status *s){*s=fixture;}
void r2_time_precision_snapshot(struct r2_pps_precision_view *p){memset(p,0,sizeof(*p));}
#include "rc_ui_binding_under_test.h"
int main(void){
 routes.node[0].id=UINT64_C(0x1000000000000001);
 fixture.active=true;fixture.lamps[1]=1;fixture.lamps[5]=2;
 struct r2_ui_model m={.sampled_ms=1000,.platform_ready=true};
 r2_service_ui(&m);
 assert(m.ble_active&&m.lamps[1]==1&&m.lamps[5]==2);
 assert(!m.scope_active&&!m.precision.authorized&&!m.precision.map_valid&&!r2_ui_analog(&m,1000));
 fixture.follower=fixture.follower_clock_valid=true;fixture.service_until=2000000;
 fixture.time_sampled_us=current;fixture.follower_utc=1000000000000;
 fixture.precision.authorized=fixture.precision.map_valid=true;r2_service_ui(&m);
 fixture.precision.rms_valid=true;fixture.precision.rms_ns=137;r2_service_ui(&m);
 assert(m.precision.rms_valid&&m.precision.rms_ns==137);
 assert(m.scope_active&&m.lamps[1]==1&&m.lamps[5]==2);
 memset(fixture.lamps,0,sizeof fixture.lamps);r2_service_ui(&m);assert(!m.lamps[1]&&!m.lamps[5]);
 assert(!m.rc_serving_clients);
 fixture.relay.active=true;fixture.relay.phase_admitted=true;fixture.relay.tx_corrections=1;r2_service_ui(&m);assert(m.rc_serving_clients);
 fixture.relay.phase_admitted=false;r2_service_ui(&m);assert(!m.rc_serving_clients);
 fixture.relay.active=false;uc_serving=true;r2_service_ui(&m);assert(m.rc_serving_clients);
 fixture.follower_clock_valid=false;r2_service_ui(&m);assert(!m.rc_serving_clients);
 fixture.follower_clock_valid=true;fixture.closing=true;r2_service_ui(&m);assert(!m.rc_serving_clients);
 fixture.closing=false;fixture.error=-1;r2_service_ui(&m);assert(!m.rc_serving_clients);
 puts("Actual RC7 snapshot->UI lamps PASS, with and without time authority");
}
