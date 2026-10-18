/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 */
#include "initiator_diag.h"
#include "timing_events.h"
#include <errno.h>
#include <stdio.h>
#if defined(CONFIG_ARM)
#include <zephyr/irq.h>
#else
static unsigned irq_lock(void){return 0;}
static void irq_unlock(unsigned key){(void)key;}
#endif
static volatile struct r2_init_diag state;
struct r2_init_last {struct r2_init_diag delta;uint32_t saturated;uint64_t peer;uint32_t serial;int32_t result;};
static struct r2_init_last last;
_Static_assert(sizeof last<=80,"Bounded last initiation evidence");
void r2_init_diag_count(unsigned counter)
{
 if(counter<R2_INIT_COUNTERS&&state.count[counter]!=UINT32_MAX)++state.count[counter];
}
void r2_init_diag_starved(unsigned available,unsigned needed)
{
 if(!state.rx_needed||available<state.rx_low)state.rx_low=(uint8_t)available;
 state.rx_free=(uint8_t)available;state.rx_needed=(uint8_t)needed;
 r2_init_diag_count(R2_INIT_BUFFER);
}
void r2_init_diag_target(const uint8_t address[6],unsigned type,unsigned channel)
{
 for(unsigned i=0;i<6;i++)state.address[i]=address[i];
 state.type=(uint8_t)type;state.channel=(uint8_t)channel;
 r2_init_diag_count(R2_INIT_PREPARE);
}
void r2_init_diag_aux_rsp(bool done,bool crc,bool valid)
{
 r2_init_diag_count(!done?R2_INIT_AUX_RSP_NO_DONE:!crc?R2_INIT_AUX_RSP_BAD_CRC:
                    !valid?R2_INIT_AUX_RSP_INVALID:R2_INIT_AUX_RSP_OK);
}
void r2_init_diag_snapshot(struct r2_init_diag *out)
{
 unsigned key=irq_lock();
 for(unsigned i=0;i<R2_INIT_COUNTERS;i++)out->count[i]=state.count[i];
 for(unsigned i=0;i<6;i++)out->address[i]=state.address[i];
 out->type=state.type;out->channel=state.channel;
 out->rx_free=state.rx_free;out->rx_low=state.rx_low;out->rx_needed=state.rx_needed;
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
 unsigned key=irq_lock();
 last.delta=s;
 for(unsigned i=0;i<R2_INIT_COUNTERS;i++)last.delta.count[i]=d[i];
 last.saturated=!!saturated;last.peer=peer;last.result=result;if(last.serial!=UINT32_MAX)++last.serial;
 irq_unlock(key);
 uint32_t lo=(uint32_t)s.address[0]|(uint32_t)s.address[1]<<8|
  (uint32_t)s.address[2]<<16|(uint32_t)s.address[3]<<24;
 uint32_t hi=(uint32_t)s.address[4]|(uint32_t)s.address[5]<<8;
 r2_timing_event(R2_TIMING_INIT_TARGET,result,peer,saturated|s.type|(uint32_t)s.channel<<8,lo,hi);
 r2_timing_event(R2_TIMING_INIT_RX,result,peer,d[R2_INIT_PREPARE],d[R2_INIT_RX],d[R2_INIT_MATCH]);
 r2_timing_event(R2_TIMING_INIT_REQUEST,result,peer,d[R2_INIT_REQUEST],d[R2_INIT_WINDOW],d[R2_INIT_BUFFER]);
 r2_timing_event(R2_TIMING_INIT_ABORT,result,peer,d[R2_INIT_ABORT],0,0);
 if(d[R2_INIT_BUFFER]&&s.rx_needed)
  r2_timing_event(R2_TIMING_INIT_RX_RESERVE,result,peer,s.rx_needed,s.rx_free,s.rx_low);
 for(unsigned i=0;i<3;i++){
  unsigned at=R2_INIT_AUX_NO_CONTEXT+2*i;
  r2_timing_event(R2_TIMING_INIT_AUX,result,peer,i,d[at],at+1<R2_INIT_COUNTERS?d[at+1]:0);
 }
}
int r2_init_diag_append(char *out,size_t capacity,int n)
{
 if(!out||n<=0||(size_t)n>=capacity||out[n-1]!='}')return -EINVAL;
 unsigned key=irq_lock();struct r2_init_last saved=last;irq_unlock(key);
 const struct r2_init_diag *s=&saved.delta;const uint32_t *d=s->count;
 int extra=snprintf(out+n-1,capacity-(size_t)n+1,
  ",\"initiation\":{\"schema\":2,\"valid\":%s,\"serial\":%u,\"saturated\":%s,\"peer\":\"%016llx\",\"result\":%d,\"address_le\":\"%02x%02x%02x%02x%02x%02x\",\"type\":%u,\"channel\":%u,\"counts\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],\"rx_reserve\":[%u,%u,%u]}}",
  saved.serial?"true":"false",saved.serial,saved.saturated?"true":"false",(unsigned long long)saved.peer,saved.result,
  s->address[0],s->address[1],s->address[2],s->address[3],s->address[4],s->address[5],s->type,s->channel,
  d[0],d[1],d[2],d[3],d[4],d[5],d[6],d[7],d[8],d[9],d[10],d[11],s->rx_free,s->rx_low,s->rx_needed);
 return extra<0||(size_t)extra>=capacity-(size_t)n+1?-ENOSPC:n-1+extra;
}
