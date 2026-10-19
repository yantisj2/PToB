/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "pc_experiment.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "pc_projection_under_test.h"
static bool parent_known,parent_synced,upstream_usable,scanning,relay_fallback,creating,lt_paused,slot_scan_failed;
static unsigned node,starts,stops,faults;static void *lt_conn;static uint64_t capture_epoch;
static struct {bool scanning;}slot_state;static struct rc5_state timing;
static struct r2_capture observed;static bool capture_ok;static int start_result;
struct bt_le_scan_param {unsigned type,interval,window;};
#define BT_LE_SCAN_TYPE_PASSIVE 0
static bool r2_capture_now(struct r2_capture *n){*n=observed;return capture_ok;}
static void check(bool ok,const char *name){assert(name);if(!ok)++faults;}
static int bt_le_scan_stop(void){++stops;return 0;}
static int bt_le_scan_start(const struct bt_le_scan_param *p,void *cb){assert(!cb&&!p->type&&p->interval==32&&p->window==(parent_synced?8u:16u));++starts;return start_result;}
#include "pc_scan_under_test.h"
int main(void){
 parent_known=parent_synced=capture_ok=true;node=1;capture_epoch=7;
 observed=(struct r2_capture){.tick=10000000,.epoch=7};
 timing=(struct rc5_state){.bound=true,.context_until=30000000,.reference={.valid=true,.completed_at=9000000}};
 scanning=true;pc_management_scan(true);
 assert(!scanning&&stops==1&&!starts&&!upstream_usable); /* Expired output stays expired. */
 pc_management_scan(true);assert(!scanning&&stops==1&&!starts);
 observed.tick=17000000;pc_management_scan(true);assert(scanning&&starts==1); /* Exactly500ms: real stale ingress. */
 scanning=false;observed.tick=10000000;observed.epoch=8;pc_management_scan(true);assert(scanning&&starts==2);
 scanning=false;observed.epoch=7;capture_ok=false;pc_management_scan(true);assert(scanning&&starts==3);
 capture_ok=true;scanning=false;parent_synced=false;pc_management_scan(true);assert(scanning&&starts==4);
 parent_synced=true;scanning=false;relay_fallback=true;pc_management_scan(true);assert(scanning&&starts==5);
 relay_fallback=false;scanning=false;slot_state.scanning=true;start_result=-EBUSY;pc_management_scan(true);assert(!scanning&&slot_scan_failed&&!faults);
 slot_state.scanning=false;scanning=false;creating=true;unsigned before=starts;pc_management_scan(true);assert(starts==before);
 creating=false;pc_management_scan(false);assert(starts==before&&!upstream_usable);
 puts("PASS actual PC discovery: fresh synchronized ingress suppresses degraded scan with expired output; fresh capture, true outage, scope, slot and acquisition fences retained");
}
