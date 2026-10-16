#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
struct r2_group_client{bool passive,source;uint64_t at,map_until,utc_us;};
struct pt_output{struct r2_group_client context;};
static bool drained;static unsigned calls;static uint64_t aligned_now,aligned_utc;
static bool r2_radio_service_closed(void){return drained;}
static bool r2_ble_client_align(uint64_t now,uint64_t utc){++calls;aligned_now=now;aligned_utc=utc;return true;}
#include "direct_alignment_under_test.h"
int main(void){
 struct pt_output out={.context={true,true,100,200,1000}};
 direct_window_align(&out,120);assert(!calls);
 drained=true;direct_window_align(&out,120);assert(calls==1&&aligned_now==120&&aligned_utc==1020);
 direct_window_align(&out,99);direct_window_align(&out,200);assert(calls==1);
 out.context.source=false;direct_window_align(&out,120);assert(calls==1);
 out.context.source=true;out.context.passive=false;direct_window_align(&out,120);assert(calls==1);
 out.context.passive=true;out.context.utc_us=UINT64_MAX;direct_window_align(&out,120);assert(calls==1);
 puts("direct client alignment requires live mapped source, drained PA, valid age and arithmetic PASS");
}
