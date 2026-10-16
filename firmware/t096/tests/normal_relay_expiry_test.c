/* Actual production selection prefix, controlled asynchronous owner boundary. */
#include "group_admission.h"
#include "runtime_profile.h"
#include <assert.h>
#include <stdio.h>
static struct {uint64_t identity;} net={333};
static struct r2_radio_scope scope;
static struct r2_local_demand incoming_fixture,local_demand;
static struct r2_group_client anchor_fixture;
static bool profile_rc=true,relay_profile_set,relay_selected,relay_transition,relay_retiring,closed,server,allowed,draining;
static int stop_requested;
static unsigned stops,releases,activates,anchors;
static struct r2_relay_status relay_fixture;
static struct r2_radio_precision_view precision_fixture;
static unsigned unavailable;
static struct r2_link_quality_view quality_fixture;
void r2_relay_service_link_quality(struct r2_link_quality_view *out){*out=quality_fixture;}
static uint64_t relay_activation_until,relay_transition_until;
static int atomic_get(int *p){return *p;}
void r2_group_admission_demand(struct r2_local_demand *out){*out=incoming_fixture;}
void r2_group_admission_client(struct r2_group_client *out){*out=anchor_fixture;}
void r2_relay_service_snapshot(struct r2_relay_status *out,struct r2_radio_precision_view *precision,uint64_t now){*out=relay_fixture;*precision=precision_fixture;}
void r2_group_admission_parent_unavailable(uint64_t parent,uint64_t now){assert(parent==local_demand.peer);unavailable++;incoming_fixture.at=now;incoming_fixture.until=0;}
void r2_ble_precision_draining(bool value){draining=value;}
void r2_relay_service_stop(uint64_t now){assert(draining);++stops;}
bool r2_relay_service_closed(void){return closed;}
int r2_relay_service_release_path(void){assert(closed&&relay_retiring);++releases;return 0;}
void r2_service_stop(void){assert(0);}
void r2_ble_group_relay(bool value){server=value;}
void r2_group_admission_relay_allowed(bool value){allowed=value;}
void r2_group_admission_require_anchor(void){++anchors;}
void r2_radio_service_activate(void){assert(closed&&!relay_profile_set&&!server&&allowed&&anchors&&!draining);++activates;}
static void poll(uint64_t now){
#include "normal_relay_expiry_under_test.h"
}
static void fresh(bool received){
 relay_profile_set=relay_selected=true;relay_transition=relay_retiring=closed=draining=false;stops=releases=activates=anchors=0;server=allowed=received;
 relay_activation_until=10000;local_demand=(struct r2_local_demand){111,100,222,200,100,10000,1,received};incoming_fixture=local_demand;
 anchor_fixture=(struct r2_group_client){.source=true,.root=111,.boot=100,.source_epoch=1,.at=1,.until=999999};
 relay_fixture=(struct r2_relay_status){.source_until_us=999999};precision_fixture=(struct r2_radio_precision_view){.correction_age_valid=true};unavailable=0;quality_fixture=(struct r2_link_quality_view){0};
}
int main(void){
 fresh(true);poll(9999);assert(!stops&&!relay_retiring);
 poll(10000);assert(!stops&&!relay_retiring&&relay_selected&&relay_profile_set);
 poll(10001);assert(!stops&&!releases&&!activates); /* lease expiry preserves upstream owner */
 incoming_fixture.at=10002;incoming_fixture.until=20000;poll(10003);
 assert(!stops&&local_demand.at==10002&&relay_activation_until==20000);
 fresh(true);incoming_fixture.at=500;incoming_fixture.until=20000;poll(10000);assert(!stops&&relay_activation_until==20000&&local_demand.at==500);
 fresh(false);incoming_fixture.at=500;incoming_fixture.until=20000;poll(10000);assert(!stops&&relay_activation_until==20000);
 fresh(false);incoming_fixture.at=500;incoming_fixture.until=20000;incoming_fixture.peer_boot++;poll(10000);assert(stops&&relay_retiring&&relay_activation_until==0);
 fresh(true);incoming_fixture.at=500;incoming_fixture.until=20000;incoming_fixture.source_epoch++;poll(10000);assert(!stops&&!relay_retiring&&local_demand.source_epoch==1);
 fresh(true);anchor_fixture.boot++;poll(9999);assert(stops&&relay_retiring&&!activates);
 fresh(true);relay_profile_set=relay_selected=false;incoming_fixture.root=net.identity;poll(1000);assert(!relay_transition&&!stops);
 fresh(false);relay_activation_until=local_demand.until=incoming_fixture.until=360000100;anchor_fixture.until=1500100;relay_fixture.source_until_us=1500100;
 anchor_fixture.until=999999999;relay_fixture.source_until_us=999999999; /* GATT/raw headers remain live while corrections stop. */
 precision_fixture.correction_age_ms=1999;poll(2000100);assert(!unavailable&&!stops);precision_fixture.correction_age_ms=2000;quality_fixture.poor=true;
 poll(2000101);assert(unavailable==1&&relay_retiring&&stops&&!activates);closed=true;poll(2000102);assert(activates==1&&!relay_selected);
 fresh(false);relay_activation_until=local_demand.until=incoming_fixture.until=360000100;anchor_fixture.until=1500100;relay_fixture.source_until_us=0;precision_fixture.correction_age_valid=false;
 poll(2000099);assert(!unavailable&&!stops);poll(2000100);assert(!unavailable&&!stops);
 quality_fixture.poor=true;poll(3000100);assert(unavailable==1&&stops&&relay_retiring);
 puts("PASS server expiry retains same upstream, matching renewals, rejected foreign renewal, real parent/scope changes drain, map warmup stays, sustained delivery failure retires");
}
