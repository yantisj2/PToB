/* SPDX-License-Identifier: Apache-2.0 */
#include "clock.h"
#include <limits.h>
#include <string.h>
_Static_assert(sizeof(struct r2_clock_view)==128,"HDOP must fit existing clock-view tail padding");
static void sat(uint32_t *v) { if(*v<UINT32_MAX)++*v; }
static int64_t absolute(int64_t v) { return v<0?-v:v; }
static int64_t scale(int64_t dt,int32_t ppb) { return dt/1000000000*ppb+(dt%1000000000)*ppb/1000000000; }
static void epoch(struct r2_clock *c) { if(c->view.source_epoch==UINT32_MAX)c->fatal=true;else ++c->view.source_epoch; }
void r2_clock_source_fence(struct r2_clock *c) { epoch(c); c->translated_epoch=0; }
static void refresh(struct r2_clock *c,int64_t now)
{
  struct r2_clock_view *v=&c->view;
  v->rx_age_us=c->last_rx<0?-1:now-c->last_rx;
  v->gga_age_us=c->last_gga<0?-1:now-c->last_gga;
  v->gps_age_us=c->last_rmc<0?-1:now-c->last_rmc;
  v->pps_age_us=c->last_pps<0?-1:now-c->last_pps;
  v->fix_active=c->run_rmc>=3 && v->gps_age_us>=0 && v->gps_age_us<R2_GPS_FRESH_US;
  v->pps_present=v->pps_age_us>=0 && v->pps_age_us<R2_PPS_FRESH_US;
  bool available=v->fix_active && v->pps_present && c->run_pair>=5 && v->rate_valid &&
    absolute(c->phase)<=2000 && !c->stopped && !c->fatal;
  if(available!=v->source_available)epoch(c);
  v->source_available=available&&!c->fatal;
  v->valid=c->mapped&&!c->fatal&&c->last_good>=0&&now-c->last_good<R2_HOLDOVER_US;
  if(c->fatal)v->state=R2_TIME_FAULT;
  else if(!c->mapped)v->state=c->run_rmc?R2_TIME_ACQUIRING:R2_TIME_UNSET;
  else if(!v->valid)v->state=R2_TIME_EXPIRED;
  else if(available)v->state=R2_TIME_SYNCED;
  else if(c->run_pair && v->fix_active && v->pps_present)v->state=R2_TIME_DISCIPLINING;
  else if(v->fix_active&&!c->paired_once&&!c->stopped)v->state=R2_TIME_COARSE;
  else v->state=R2_TIME_HOLDOVER;
  v->utc_us=c->mapped?c->wall:0;v->mono_us=now;v->phase_us=c->phase;
  if(c->last_gga<0||now-c->last_gga>=4000000)v->sats_used=0;
}
void r2_clock_init(struct r2_clock *c,int64_t now,enum r2_association association)
{
  *c=(struct r2_clock){.last_tick=now,.last_rmc=-1,.last_pps=-1,.last_pair_edge=-1,
    .last_good=-1,.last_gga=-1,.last_rx=-1,.view={.generation=1,.source_epoch=1,.association=association,.gga_hdop_centi=R2_GGA_HDOP_UNKNOWN}};
  if(now<0||now>R2_CLOCK_MAX_US||association>R2_ASSOC_PRECEDING_UTC_SECOND)c->fatal=true;
  refresh(c,now);
}
void r2_clock_advance(struct r2_clock *c,int64_t now)
{
  if(now<c->last_tick||now<0||now>R2_CLOCK_MAX_US) {
    c->fatal=true;c->view.reason=R2_INPUT_TIMEBASE;c->view.source_available=false;
    c->view.valid=false;c->view.state=R2_TIME_FAULT;return;
  }
  int64_t prior=c->last_tick,dt=now-prior;c->last_tick=now;
  int64_t until=c->last_rmc<0?prior:c->last_rmc+R2_GPS_FRESH_US;
  if(c->last_pair_edge>=0&&c->last_pair_edge+R2_PPS_FRESH_US<until)
    until=c->last_pair_edge+R2_PPS_FRESH_US;
  int64_t slew_dt=until<=prior?0:(now<until?now:until)-prior;
  if(c->mapped) {
    int64_t fraction=(dt%1000000000)*c->view.rate_ppb+c->rate_remainder;
    int64_t rate=dt/1000000000*c->view.rate_ppb+fraction/1000000000;
    c->rate_remainder=fraction%1000000000;
    int64_t fraction_slew=(slew_dt%1000000000)*R2_SLEW_PPB+c->slew_remainder;
    int64_t limit=slew_dt/1000000000*R2_SLEW_PPB+fraction_slew/1000000000;
    c->slew_remainder=fraction_slew%1000000000;
    int64_t adjust=c->phase>limit?limit:c->phase<-limit?-limit:c->phase;
    c->wall+=dt+rate+adjust;c->phase-=adjust;
  }
  if(c->last_rmc>=0&&now-c->last_rmc>=R2_GPS_FRESH_US){c->run_rmc=0;c->phase=0;c->slew_remainder=0;}
  if(c->last_pair_edge>=0 && (now-c->last_pair_edge>=R2_PPS_FRESH_US || !c->run_rmc)) {
    c->run_pair=0;c->fit_count=0;c->last_pair_edge=-1;c->phase=0;
  }
  refresh(c,now);
}
void r2_clock_fault(struct r2_clock *c,enum r2_clock_fault reason,int64_t now)
{
#ifdef R2_TZ_AUTO
  c->position.valid=false;
#endif
  r2_clock_advance(c,now);sat(&c->view.faults);c->view.reason=reason;
  if(c->view.generation==UINT32_MAX)c->fatal=true;else ++c->view.generation;
  epoch(c);c->run_rmc=c->run_pair=c->fit_count=c->used=0;
  c->last_rmc=c->last_pps=c->last_pair_edge=c->last_gga=-1;c->phase=0;
  c->line_drop=false;if(reason==R2_INPUT_STOP)c->stopped=true;
  refresh(c,c->last_tick);
}
void r2_clock_pps(struct r2_clock *c,int64_t edge,int64_t now)
{
  r2_clock_advance(c,now);if(c->fatal||c->stopped)return;
  if(edge<0||edge>now||now-edge>=R2_INPUT_MAX_AGE_US) {
    sat(&c->view.pps_rejected);r2_clock_fault(c,R2_INPUT_STALE,now);return;
  }
  if(c->last_pps>=0) {
    int64_t delta=edge-c->last_pps;
    if(delta<=250000) {sat(&c->view.pps_rejected);return;} /* bounce/replay never refreshes */
    if(delta<998000||delta>1002000) {
      sat(&c->view.pps_rejected);c->run_pair=c->fit_count=0;c->last_pair_edge=-1;c->phase=0;
    }
  }
  c->last_pps=edge;sat(&c->view.pps_edges);refresh(c,now);
}
static void rmc(struct r2_clock *c,int64_t utc,int64_t received,int64_t now)
{
  if(c->last_rmc>=0) {
    int64_t delta=utc-c->last_rmc_utc;
    if(delta==0) {sat(&c->view.rejected);return;} /* duplicate cannot extend freshness */
    if(delta!=1000000||received<=c->last_rmc || received-c->last_rmc>1500000) {
      sat(&c->view.rejected);r2_clock_fault(c,R2_INPUT_JUMP,now);return;
    }
  }
  c->last_rmc=received;c->last_rmc_utc=utc;sat(&c->run_rmc);sat(&c->view.accepted);
  if(c->run_rmc>=3&&!c->paired_once) {
    c->last_good=received;
    if(!c->mapped) {c->mapped=true;c->wall=utc+(now-received);}
    else {
      int64_t error=utc+(now-received)-c->wall;
      if(absolute(error)>2000000){r2_clock_fault(c,R2_INPUT_JUMP,now);return;}
      c->phase=error; /* UART-arrival coarse steering; uncertainty remains unknown. */
    }
  }
  /* No inferred PPS label from cadence. This contract must be independently
   * commissioned for physical receivers; the hardware default is UNKNOWN. */
  bool pair=c->view.association==R2_ASSOC_PRECEDING_UTC_SECOND && utc%1000000==0 &&
    c->last_pps>=0 && received>=c->last_pps+20000 && received-c->last_pps<=600000 &&
    c->last_pps!=c->last_pair_edge && c->run_rmc>=3;
  if(pair) {
    if(c->last_pair_edge>=0 && (utc-c->last_pair_utc!=1000000 ||
       c->last_pps-c->last_pair_edge<998000 || c->last_pps-c->last_pair_edge>1002000)) {
      r2_clock_fault(c,R2_INPUT_JUMP,now);return;
    }
    int64_t target=utc+(now-c->last_pps)+scale(now-c->last_pps,c->view.rate_ppb);
    int64_t error=target-c->wall;
    if(absolute(error)>2000000) {sat(&c->view.rejected);r2_clock_fault(c,R2_INPUT_JUMP,now);return;}
    c->phase=error;c->last_pair_edge=c->last_pps;c->last_pair_utc=utc;
    c->last_good=c->last_pps;c->paired_once=true;sat(&c->run_pair);sat(&c->view.pairs);
    if(!c->fit_count) {c->fit_edge=c->last_pps;c->fit_utc=utc;c->fit_count=1;}
    else if(++c->fit_count>=9) {
      int64_t local=c->last_pps-c->fit_edge,reference=utc-c->fit_utc;
      int64_t estimate=(reference-local)*1000000000/local;
      if(absolute(estimate)>R2_RATE_LIMIT_PPB) {r2_clock_fault(c,R2_INPUT_JUMP,now);return;}
      c->view.rate_ppb=c->view.rate_valid?(3*c->view.rate_ppb+(int32_t)estimate)/4:(int32_t)estimate;
      c->view.rate_valid=true;c->fit_count=0;
    }
  }
  refresh(c,now);
}
void r2_clock_bytes(struct r2_clock *c,const uint8_t *bytes,size_t size,int64_t received,int64_t now)
{
  r2_clock_advance(c,now);if(c->fatal||c->stopped)return;
  if(received<0||received>now||now-received>=R2_INPUT_MAX_AGE_US) {r2_clock_fault(c,R2_INPUT_STALE,now);return;}
  if(size)c->last_rx=received; /* Reception remains observable even with a void fix. */
  for(size_t i=0;i<size;i++) {
    unsigned char ch=bytes[i];
    if(ch=='$') {c->used=1;c->line[0]='$';c->line_drop=false;c->line_start=received;continue;}
    if(ch=='\r')continue;
    if(ch=='\n') {
      if(c->used&&!c->line_drop) {
        c->line[c->used]=0;int64_t utc=0;unsigned sats=0;
        int kind=r2_nmea(c->line,&utc,&sats);c->used=0;
        if(kind==1||kind==2){
          unsigned field=0;c->view.rmc_status=c->view.rmc_mode=0;
          for(const char *s=c->line;*s&&*s!='*';s++)if(*s==','){
            ++field;if(field==2)c->view.rmc_status=(uint8_t)s[1];
            if(field==12)c->view.rmc_mode=(uint8_t)(s[1]=='*'||s[1]==','?0:s[1]);
          }
        }
        if(received<c->line_start||received-c->line_start>500000)kind=-R2_INPUT_STALE;
        if(kind<0||kind==2) {sat(&c->view.rejected);r2_clock_fault(c,kind==2?R2_INPUT_VOID:(enum r2_clock_fault)-kind,now);}
        else if(kind==1){
#ifdef R2_TZ_AUTO
          int64_t prior=c->last_rmc;
#endif
          rmc(c,utc,received,now);
#ifdef R2_TZ_AUTO
          /* A duplicate/rejected navigation epoch cannot freshen position. */
          if(c->last_rmc==received && c->last_rmc!=prior)
            c->position.valid=r2_nmea_position(c->line,&c->position,c->line_start);
#endif
        }
        else if(kind==3) {c->view.sats_used=sats;c->view.gga_hdop_centi=r2_nmea_gga_hdop(c->line);c->last_gga=received;sat(&c->view.gga_count);}
      }
      c->used=0;continue;
    }
    if(!c->used||c->line_drop)continue;
    if(ch<32||ch>126||c->used>=sizeof(c->line)-1) {
      sat(&c->view.rejected);r2_clock_fault(c,R2_INPUT_OVERFLOW,now);c->line_drop=true;continue;
    }
    c->line[c->used++]=(char)ch;
  }
  refresh(c,now);
}
