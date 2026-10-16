/* Actual admission policy: a coarse exchange must not permanently ban control. */
#include "group_admission.h"
#include <assert.h>
#include <stdio.h>
static struct r2_join_exchange exchange;
static bool control_requested;
static uint64_t control_denied_until;
static bool room=true;
bool r2_ble_control_available(void){return room;}
#include "control_retry_under_test.h"
int main(void){
 struct r2_network n={.capabilities=1,.now=1000000};n.admission.eligible=true;
 struct r2_join_record r={.capabilities=1,.source_flags=1,.root=99};
 /* Eligibility changed after a coarse request: there was no control refusal. */
 control_requested=false;control_result(&r,n.now);assert(control_available(&n));
 control_requested=true;control_result(&r,n.now);assert(!control_available(&n));
 n.now=5999999;assert(!control_available(&n));n.now=6000000;assert(control_available(&n));
 room=false;assert(!control_available(&n));room=true;
 n.admission.eligible=false;assert(!control_available(&n));n.admission.eligible=true;
 control_result(&r,n.now);assert(!control_available(&n));
 exchange.control=true;control_result(&r,n.now);assert(control_available(&n));
 exchange.control=false;exchange.server=true;control_result(&r,n.now);assert(control_available(&n));
 exchange.server=false;control_result(&r,UINT64_MAX-10);assert(control_denied_until==UINT64_MAX);
 puts("Admission coarse-request race, bounded refusal retry, capacity/source gates and authenticated success PASS");
}
