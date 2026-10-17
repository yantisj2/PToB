/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "dc_clock.h"
#include <limits.h>
#include <string.h>
bool r2_dc_clock(const struct r2_source_clock *s,const struct r2_clock *c,
 struct r2_capture at,uint64_t now,uint64_t identity,uint64_t boot,struct r2_uc_clock *out){
 *out=(struct r2_uc_clock){0};
 if(!identity||!boot||!at.epoch||at.epoch>UINT32_MAX||at.kind!=R2_CAPTURE_NOW||
    c->fatal||c->stopped||!c->view.valid||!c->view.source_available||!c->view.fix_active||
    !c->view.pps_present||!s->utc_bound||s->utc_source_epoch!=c->view.source_epoch||
    s->utc_association!=c->view.association||!s->count||at.epoch!=s->epoch||
    c->view.gps_age_us<0||c->view.gps_age_us>=R2_GPS_FRESH_US||
    c->view.pps_age_us<0||c->view.pps_age_us>=R2_PPS_FRESH_US||c->view.mono_us<0||
    now<(uint64_t)c->view.mono_us||now-(uint64_t)c->view.mono_us>=100000)return false;
 const struct r2_source_map *m=&s->maps[(s->head+R2_SOURCE_MAP_HISTORY-1)%R2_SOURCE_MAP_HISTORY];
 if(!m->generation||m->generation>UINT32_MAX||at.tick<m->from||at.tick>=m->until||
    m->rate_q16<UINT64_C(15990000)*65536||m->rate_q16>UINT64_C(16010000)*65536||
    m->phase_ppb < -5000000||m->phase_ppb>5000000)return false;
 uint64_t ns,gen,expires;
 if(!r2_source_clock_read(s,at,&ns,&gen)||gen!=m->generation||ns>INT64_MAX||
    !r2_source_clock_expiry(s,at,&expires)||expires<=ns)return false;
 /* The wire's integer ppb inverse rate approximates the Q16 source rate to
  * <2ns over its <=1.5s lifetime. Phase slew is included; never drop it. */
 int64_t raw=((int64_t)m->rate_q16-INT64_C(16000000)*65536)*INT64_C(1000000000)/(INT64_C(16000000)*65536);
 int64_t rate=(INT64_C(1000000000)+raw)*INT64_C(1000000000)/(INT64_C(1000000000)-m->phase_ppb)-INT64_C(1000000000);
 /* Match the existing macOS immutable-map contract; acquisition slews outside
  * this range remain unavailable rather than widening host admission. */
 if(rate < -300000||rate>300000)return false;
 uint64_t remaining=(uint64_t)(R2_GPS_FRESH_US-c->view.gps_age_us);
 if((uint64_t)(R2_PPS_FRESH_US-c->view.pps_age_us)<remaining)remaining=(uint64_t)(R2_PPS_FRESH_US-c->view.pps_age_us);
 uint64_t age=now-(uint64_t)c->view.mono_us;
 if(age>=remaining)return false;
 remaining-=age;
 /* Conservative ticks at minimum admitted oscillator rate, no grace. */
 uint64_t ticks=remaining*1599/100;
 if(!ticks||at.tick>UINT64_MAX-ticks)return false;
 uint64_t until=at.tick+ticks;if(m->until<until)until=m->until;
 *out=(struct r2_uc_clock){.map={.effective=m->from,.anchor_tick=m->from,.anchor_ns=m->anchor_ns,
  .ppb=(int32_t)rate,.generation=(uint32_t)m->generation,.bound=UINT32_MAX,
  .rate_bound=UINT32_MAX,.capture_bound=UINT32_MAX},
  .origin={.root=identity,.boot=boot,.epoch=c->view.source_epoch,.issued=m->anchor_ns,
   .expires=expires,.utc_offset=s->utc_offset_ns,.frame=1000000,.hold_ns=UINT32_MAX},
  .until_tick=until,.session_until=until,.discipline_tick=s->last_edge,
  .capture_epoch=(uint32_t)at.epoch,.depth=0,.valid=true,.estimate=true};
 memcpy(out->scope,"DC-GNSS-USB-v1",14);return true;
}
void r2_dc_rms_add(struct r2_dc_rms *s,uint64_t tick,uint64_t ns,uint16_t frame,
 uint32_t usb,uint32_t capture,uint32_t source,uint32_t generation,uint64_t now){
 bool same=s->seen&&s->usb_epoch==usb&&s->capture_epoch==capture&&s->source_epoch==source&&s->generation==generation;
 if(same&&tick==s->tick)return; /* Polling the same hardware frame is not a sample. */
 unsigned frames=(frame-s->frame)&2047u;
 if(!same||tick<=s->tick||tick-s->tick>=UINT64_C(16000000)||!frames||frames>=1000||
    ns<=s->ns||now<s->last_ms||now-s->last_ms>=1000){*s=(struct r2_dc_rms){0};same=false;}
 if(same){
  /* Per-frame interval error over the observed span; no mean removal or fit.
   * Gaps are spans, not fabricated missing individual SOF observations. */
  int64_t error=(int64_t)((ns-s->ns)/frames)-1000000;
  if(error < -1000000||error>1000000){*s=(struct r2_dc_rms){0};}
  else{
   uint32_t magnitude=(uint32_t)(error<0?-error:error);
   uint64_t old=s->errors[s->head];s->squares-=old*old;
   s->errors[s->head]=magnitude;s->squares+=(uint64_t)magnitude*magnitude;
   s->head=(s->head+1)%R2_DC_RMS_WINDOW;if(s->count<R2_DC_RMS_WINDOW)++s->count;
  }
 }
 s->tick=tick;s->ns=ns;s->frame=frame;s->usb_epoch=usb;s->capture_epoch=capture;
 s->source_epoch=source;s->generation=generation;s->last_ms=now;s->seen=true;
}
bool r2_dc_rms_read(const struct r2_dc_rms *s,uint64_t now,uint32_t *rms,unsigned *samples){
 *samples=s->count;
 if(s->count<4||now<s->last_ms||now-s->last_ms>=1000)return false;
 *rms=r2_servo_rms_floor(s->squares,s->count);return true;
}
