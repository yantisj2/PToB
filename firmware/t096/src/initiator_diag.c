/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 */
#include "initiator_diag.h"
#include "timing_events.h"
#if defined(CONFIG_ARM)
#include <zephyr/irq.h>
#else
static unsigned irq_lock(void){return 0;}
static void irq_unlock(unsigned key){(void)key;}
#endif
static volatile struct r2_init_diag state;
void r2_init_diag_count(unsigned counter)
{
 if(counter<R2_INIT_COUNTERS&&state.count[counter]!=UINT32_MAX)++state.count[counter];
}
void r2_init_diag_target(const uint8_t address[6],unsigned type,unsigned channel)
{
 for(unsigned i=0;i<6;i++)state.address[i]=address[i];
 state.type=(uint8_t)type;state.channel=(uint8_t)channel;
 r2_init_diag_count(R2_INIT_PREPARE);
}
void r2_init_diag_snapshot(struct r2_init_diag *out)
{
 unsigned key=irq_lock();
 for(unsigned i=0;i<R2_INIT_COUNTERS;i++)out->count[i]=state.count[i];
 for(unsigned i=0;i<6;i++)out->address[i]=state.address[i];
 out->type=state.type;out->channel=state.channel;
 irq_unlock(key);
}
void r2_init_diag_emit(uint64_t peer,int result,const struct r2_init_diag *before)
{
 struct r2_init_diag s;r2_init_diag_snapshot(&s);uint32_t d[R2_INIT_COUNTERS];
 unsigned saturated=0;
 for(unsigned i=0;i<R2_INIT_COUNTERS;i++){
  d[i]=s.count[i]>=before->count[i]?s.count[i]-before->count[i]:UINT32_MAX;
  if(s.count[i]==UINT32_MAX)saturated=1u<<31;
 }
 uint32_t lo=(uint32_t)s.address[0]|(uint32_t)s.address[1]<<8|
  (uint32_t)s.address[2]<<16|(uint32_t)s.address[3]<<24;
 uint32_t hi=(uint32_t)s.address[4]|(uint32_t)s.address[5]<<8;
 r2_timing_event(R2_TIMING_INIT_TARGET,result,peer,saturated|s.type|(uint32_t)s.channel<<8,lo,hi);
 r2_timing_event(R2_TIMING_INIT_RX,result,peer,d[R2_INIT_PREPARE],d[R2_INIT_RX],d[R2_INIT_MATCH]);
 r2_timing_event(R2_TIMING_INIT_REQUEST,result,peer,d[R2_INIT_REQUEST],d[R2_INIT_WINDOW],d[R2_INIT_BUFFER]);
 r2_timing_event(R2_TIMING_INIT_ABORT,result,peer,d[R2_INIT_ABORT],0,0);
}
