/* SPDX-License-Identifier: Apache-2.0
 * Bounded UI-only PPS session statistics. Never a timing/authority input.
 * Exact magnitude bins through1023ns; wider tails use explicit approximate
 * power-of-two bins. Mean and maximum retain every usable session sample. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#define R2_SHADOW_EXACT_BINS 1024u
#define R2_SHADOW_BINS (R2_SHADOW_EXACT_BINS+19u)
enum r2_shadow_page_state {R2_SHADOW_OFF,R2_SHADOW_DWELL,R2_SHADOW_WAIT_PPS,R2_SHADOW_WAIT_MAP,R2_SHADOW_GM,R2_SHADOW_RUNNING,R2_SHADOW_FULL};
struct r2_shadow_session_view {
 uint64_t elapsed_ms;int64_t mean_tenth_ns;
 uint32_t session,samples,skipped,missing,p95_ns,p99_ns,max_ns;
 unsigned state;bool active,valid,approximate;
};
struct r2_shadow_session {
 uint32_t histogram[R2_SHADOW_BINS];int64_t sum;
 uint64_t entered_ms,started_ms,elapsed_ms,start_tick,epoch;
 uint32_t session,samples,skipped,missing,max_ns,cursor;
 bool entered,active;
 unsigned state;
};
static inline void r2_shadow_session_stop(struct r2_shadow_session *s,uint64_t now){
 if(s->active&&now>=s->started_ms)s->elapsed_ms=now-s->started_ms;
 s->active=false;s->entered=false;s->state=R2_SHADOW_OFF;
}
/* True exactly when a new dwell-qualified session needs a hardware boundary. */
static inline bool r2_shadow_session_step(struct r2_shadow_session *s,bool visible,bool gm,bool pps,uint64_t now){
 if(gm&&visible){if(s->state!=R2_SHADOW_GM){uint32_t session=s->session;r2_shadow_session_stop(s,now);memset(s,0,sizeof *s);s->session=session;s->state=R2_SHADOW_GM;}return false;}
 if(!visible){r2_shadow_session_stop(s,now);return false;}
 if(!s->entered){uint32_t session=s->session;memset(s,0,sizeof *s);s->session=session;s->entered=true;s->entered_ms=now;}
 if(now<s->entered_ms){r2_shadow_session_stop(s,now);return false;}
 if(s->active){s->elapsed_ms=now>=s->started_ms?now-s->started_ms:0;s->state=pps?R2_SHADOW_RUNNING:R2_SHADOW_WAIT_PPS;return false;}
 if(s->state==R2_SHADOW_FULL)return false;
 if(now-s->entered_ms<1000){s->state=R2_SHADOW_DWELL;return false;}
 s->state=pps?R2_SHADOW_WAIT_MAP:R2_SHADOW_WAIT_PPS;
 return pps;
}
static inline bool r2_shadow_session_begin(struct r2_shadow_session *s,uint64_t now,uint64_t tick,uint64_t epoch,uint32_t cursor){
 if(!s->entered||s->active||!tick||!epoch||s->session==UINT32_MAX)return false;
 s->started_ms=now;s->start_tick=tick;s->epoch=epoch;s->cursor=cursor;s->active=true;s->state=R2_SHADOW_RUNNING;++s->session;return true;
}
static inline unsigned r2_shadow_bin(uint32_t ns){
 if(ns<R2_SHADOW_EXACT_BINS)return ns;
 unsigned b=R2_SHADOW_EXACT_BINS;uint32_t edge=2048;
 while(ns>=edge&&b+1<R2_SHADOW_BINS){++b;edge*=2;}
 return b;
}
static inline uint32_t r2_shadow_bin_value(unsigned b){
 if(b<R2_SHADOW_EXACT_BINS)return b;
 uint32_t lo=UINT32_C(1024)<<(b-R2_SHADOW_EXACT_BINS);
 return lo+lo/2-1;
}
static inline void r2_shadow_session_sample(struct r2_shadow_session *s,uint32_t sequence,uint64_t tick,uint64_t epoch,bool usable,int64_t phase){
 if(!s->active||sequence<=s->cursor)return;
 uint32_t gap=sequence-s->cursor-1;s->missing=gap>UINT32_MAX-s->missing?UINT32_MAX:s->missing+gap;s->cursor=sequence;
 if(epoch!=s->epoch||tick<=s->start_tick||!usable||phase<-500000000||phase>500000000){if(s->skipped<UINT32_MAX)++s->skipped;return;}
 if(s->samples==UINT32_MAX){s->active=false;s->state=R2_SHADOW_FULL;return;}
 uint32_t ns=(uint32_t)(phase<0?-phase:phase);++s->histogram[r2_shadow_bin(ns)];++s->samples;s->sum+=phase;
 if(ns>s->max_ns)s->max_ns=ns;
}
static inline uint32_t r2_shadow_rank(const struct r2_shadow_session *s,uint32_t rank,bool *approximate){
 uint64_t total=0;
 for(unsigned i=0;i<R2_SHADOW_BINS;i++){total+=s->histogram[i];if(total>rank){*approximate|=i>=R2_SHADOW_EXACT_BINS;return r2_shadow_bin_value(i);}}
 return 0;
}
static inline uint32_t r2_shadow_percentile(const struct r2_shadow_session *s,unsigned p,bool *approximate){
 uint64_t scaled=(uint64_t)(s->samples-1)*p;uint32_t rank=(uint32_t)(scaled/100),fraction=(uint32_t)(scaled%100);
 uint32_t lo=r2_shadow_rank(s,rank,approximate),hi=fraction?r2_shadow_rank(s,rank+1,approximate):lo;
 return (uint32_t)(((uint64_t)lo*(100-fraction)+(uint64_t)hi*fraction+50)/100);
}
static inline void r2_shadow_session_view(const struct r2_shadow_session *s,struct r2_shadow_session_view *v){
 *v=(struct r2_shadow_session_view){.elapsed_ms=s->elapsed_ms,.session=s->session,.samples=s->samples,.skipped=s->skipped,.missing=s->missing,.max_ns=s->max_ns,.state=s->state,.active=s->active,.valid=s->samples!=0};
 if(!s->samples)return;
 int64_t q=s->sum/s->samples,r=s->sum%s->samples;
 v->mean_tenth_ns=q*10+(r*10+(r<0?-(int64_t)s->samples/2:(int64_t)s->samples/2))/s->samples;
 v->p95_ns=r2_shadow_percentile(s,95,&v->approximate);v->p99_ns=r2_shadow_percentile(s,99,&v->approximate);
}
