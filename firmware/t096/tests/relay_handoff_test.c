/* Exact production handoff block: controller calls are completion boundaries. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
struct {unsigned self;uint64_t identity;} net;
struct {uint64_t identity[4];bool field_profile;} roster;
struct {int error;} status;
struct {int unused;} scope;
static bool local_relay_path;
static struct {uint64_t root,peer,root_boot;uint32_t source_epoch;bool received;} local_demand;
static int r2_relay_service_path(uint64_t root,uint64_t relay,uint64_t local,uint64_t boot,uint32_t epoch){(void)root;(void)relay;(void)local;return 0;}
static int source_work,lock,key;
static bool relay_transition,relay_selected,relay_requested,relay_profile_set,relay_leaf_stopped;
static uint64_t relay_transition_until;
static bool radio_closed,ble_closed,stop_sent,profile_changed;
static unsigned normal_ticks,relay_starts,service_stops;
static int k_spin_lock(int *x){return 0;}
static void k_spin_unlock(int *x,int y){}
static int r2_group_slot(uint64_t *ids,uint64_t id){return id==UINT64_C(0x1000000000000001)?0:2;}
static void r2_radio_service_stop(uint64_t now){stop_sent=true;}
static bool r2_radio_service_closed(void){return radio_closed;}
static int r2_network_profile_root(void *n,uint8_t root,uint8_t leaf,uint64_t now){profile_changed=true;return 0;}
static void r2_ble_public_client(unsigned p){}
static void r2_ble_stop(void){ble_closed=false;}
static bool r2_ble_closed(void){return ble_closed;}
static int r2_relay_service_start(uint64_t now){assert(radio_closed);++relay_starts;return 0;}
static void r2_service_stop(void){++service_stops;}
#define r2_radio_service_tick(...) (++normal_ticks)
static void poll(bool requested,bool cancelled,uint64_t now){int64_t utc=1;
#include "relay_handoff_under_test.h"
}
int main(void){
 relay_requested=true;poll(true,false,1000);
 assert(stop_sent&&relay_transition&&!profile_changed&&!relay_selected&&relay_transition_until==2001000);
 poll(true,true,2000);assert(!profile_changed&&!relay_selected&&relay_transition);
 radio_closed=true;poll(true,true,3000);
 assert(!relay_transition&&!relay_requested&&!relay_selected&&!profile_changed&&!relay_starts);
 poll(false,true,4000);assert(normal_ticks==1&&!service_stops);
 /* Fresh admitted handoff consumes only its own original two-second bound. */
 relay_requested=true;poll(true,false,5000);
 assert(relay_selected&&profile_changed&&relay_starts==1&&relay_transition_until==2005000&&!service_stops);
 return 0;
}
