// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Synthetic encoder boundary for C++ peripheral tests; no deployed credentials.
#include "lora_passive.h"
#include <assert.h>
void n30_test_packet(uint8_t* out,unsigned kind,uint32_t cursor,uint64_t ns){
 const struct r2_group_config g={.id=11,.epoch=1,.psk={1,2,3}};
 const struct lp_scope scope={.emitter=1,.boot=22,.root=1,.root_boot=22,.epoch=5,.stream=1,.seed=7,.kind=1};
 uint8_t context[16];assert(!lp_context(context,&scope,8,&g));
 struct lp_packet p={.kind=(uint8_t)kind,.schedule=8,.scope=scope,.cursor=cursor,.serial=cursor/2+1,.utc_ns=ns,.tz={1,0},.time_valid=kind==LP_META};
 if(kind==LP_SYNC)p.utc_ns=0;
 assert(!lp_encode(out,112,&p,context,&g));
}
