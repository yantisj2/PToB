/* Extracted production owner-selection block, asynchronous drain boundary. */
#include "group_admission.h"
#include "relay_admission.h"
#include <assert.h>
static struct {unsigned capabilities;bool local_gm;uint64_t identity;} net={7,false,333};
static struct {struct {uint64_t id;} group;bool field_profile;} roster={{1},false};
static struct {bool gm;} scope;
static struct r2_local_demand local_demand;
static struct r2_relay_request relay_request;
static bool local_relay_path,relay_selected,relay_requested,relay_transition,relay_profile_set,relay_cancelled;
static bool closed;static unsigned stops,releases;
static uint64_t relay_activation_until,relay_session;
static int key,lock;
static int k_spin_lock(int *p){return 0;}
void r2_relay_service_stop(uint64_t now){++stops;}
bool r2_relay_service_closed(void){return closed;}
int r2_relay_service_release_path(void){assert(closed);++releases;return 0;}
static void r2_radio_service_stop(uint64_t now){}
static bool r2_radio_service_closed(void){return closed;}
static void poll(struct r2_local_demand incoming,uint64_t now){
 struct r2_group_client parent_context={.root=111,.boot=100,.source_epoch=1,.at=50};
 bool fresh_anchor=true;
#include "local_path_transition_under_test.h"
}
int main(void){
 local_relay_path=relay_selected=true;
 local_demand=(struct r2_local_demand){111,100,0,0,0,0,1,true};
 struct r2_local_demand better={111,100,222,200,100,30000100,1,false};
 poll(better,101);assert(stops==1&&relay_selected&&local_demand.at==0&&!relay_request.pending);
 poll(better,102);assert(stops==2&&local_demand.at==0);
 closed=true;poll(better,103);
 assert(releases==1&&!relay_selected&&!local_relay_path&&!relay_request.pending);
 poll(better,104);
 assert(local_relay_path&&!local_demand.received&&local_demand.peer==222&&local_demand.at==100);
 assert(relay_request.pending&&relay_request.session==100&&relay_activation_until==30000100);
 poll(better,105);assert(releases==1&&relay_request.session==100);
 return 0;
}
