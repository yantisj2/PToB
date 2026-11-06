// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Engineering phase/rate/fast-and-slow-damped-acceleration model. Internal covariance is not
// an RF/UTC bound. No source permission, BLE sample, PPS or persistent state.
#include "lora_thermal.h"
#include <math.h>
#include <string.h>
#include <limits.h>
// Frozen on complete native052 development before prospective validation.
static const double Tau=120.0,FastTau=30.0,R=250000.0,Q=.0001;
double n30_lora_decay(double seconds){
 // exp(-seconds/120), range reduced by16. At seconds<=300 the Taylor
 // remainder is below1.6e-13 before four squarings; this saves duplicate
 // general-domain libm code inside the original fixed1MiB image.
 unsigned extra=0;while(seconds>300.0&&extra<64){seconds*=.5;++extra;}
 const double x=seconds/1920.0;
 double y=1.0+x*(-1.0+x*(.5+x*(-1.0/6.0+x*(1.0/24.0+x*(-1.0/120.0+x*(1.0/720.0+x*(-1.0/5040.0+x/40320.0)))))));
 y*=y;y*=y;y*=y;y*=y;while(extra--)y*=y;return y;
}
double n30_lora_decay_fast(double seconds){
 const double slow=n30_lora_decay(seconds);const double twice=slow*slow;return twice*twice;
}
void gm_clear(struct gm_clock* c){if(c)memset(c,0,sizeof *c);}
bool gm_ready(const struct gm_clock* c){return c&&c->points>=4&&c->tick>=c->first_tick&&c->tick-c->first_tick>=UINT64_C(180000000)*16;}
static bool delta(uint64_t tick,uint64_t anchor,double* dt,uint64_t* nominal){
 if(tick<anchor)return false;
 const uint64_t d=tick-anchor;
 if(d>UINT64_C(300000000)*16)return false;
 *dt=(double)d/16000000.0;*nominal=d*125/2;return true;
}
bool gm_observed_predict(const struct gm_clock* c,uint64_t tick,uint64_t* ns){
 if(!c||!ns||!c->points||tick<c->tick||tick-c->tick>UINT64_C(120000000)*16)return false;
 double dt;uint64_t nominal;if(!delta(tick,c->tick,&dt,&nominal))return false;
 const double h=Tau*(dt-Tau*(1.0-n30_lora_decay(dt)));
 const double hf=FastTau*(dt-FastTau*(1.0-n30_lora_decay_fast(dt)));
 const double correction=c->phase+c->rate*dt+c->fast_accel*hf+c->accel*h;
 if(!isfinite(correction)||fabs(correction)>1e9)return false;
 const int64_t ci=llround(correction);uint64_t elapsed;
 if(ci<0){if(nominal<(uint64_t)-ci)return false;elapsed=nominal-(uint64_t)-ci;}
 else{if(nominal>UINT64_MAX-(uint64_t)ci)return false;elapsed=nominal+(uint64_t)ci;}
 if(c->ns>UINT64_MAX-elapsed)return false;
 *ns=c->ns+elapsed;return true;
}
bool gm_predict(const struct gm_clock* c,uint64_t tick,uint64_t* ns){return gm_ready(c)&&gm_observed_predict(c,tick,ns);}
static bool point(struct gm_clock* c,uint64_t tick,uint64_t ns,bool observed){
 if(!c||!tick||!ns)return false;
 if(!c->points){c->tick=c->first_tick=tick;c->ns=ns;c->points=1;c->p[0][0]=R;c->p[1][1]=1e8;c->p[2][2]=c->p[3][3]=100;return true;}
 double dt;uint64_t nominal;
 if(tick<=c->tick||ns<=c->ns){gm_clear(c);return false;}
 const uint64_t ticks=tick-c->tick;const bool long_observation=observed&&ticks>UINT64_C(300000000)*16;
 if(long_observation){if(ticks>UINT64_MAX/125){gm_clear(c);return false;}dt=(double)ticks/16000000.0;nominal=ticks*125/2;}
 else if(!delta(tick,c->tick,&dt,&nominal)){gm_clear(c);return false;}
 const uint64_t remote=ns-c->ns;
 if(remote>INT64_MAX){gm_clear(c);return false;}
 const double u=n30_lora_decay(dt),g=Tau*(1.0-u),h=Tau*(dt-g);
 const double uf=n30_lora_decay_fast(dt),gf=FastTau*(1.0-uf),hf=FastTau*(dt-gf);
 const double f[4][4]={{1,dt,hf,h},{0,1,gf,g},{0,0,uf,0},{0,0,0,u}},x[4]={c->phase,c->rate,c->fast_accel,c->accel};
 double xp[4]={0},cp[4][4]={{0}},tmp[4][4]={{0}};
 for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++){xp[i]+=f[i][j]*x[j];for(unsigned k=0;k<4;k++)tmp[i][j]+=f[i][k]*c->p[k][j];}
 for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++)for(unsigned k=0;k<4;k++)cp[i][j]+=tmp[i][k]*f[j][k];
 const double dt2=dt*dt,dt3=dt2*dt,dt4=dt3*dt,dt5=dt4*dt;
 const double noise[3][3]={{dt5/20,dt4/8,dt3/6},{dt4/8,dt3/3,dt2/2},{dt3/6,dt2/2,dt}};
 for(unsigned mode=2;mode<4;mode++){const unsigned ids[3]={0,1,mode};
  for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)cp[ids[i]][ids[j]]+=Q*noise[i][j];
 }
 const double z=(double)remote-(double)(tick-c->tick)*62.5;
 const double innovation=z-xp[0],s=cp[0][0]+R;
 if(!isfinite(s)||s<=0||!isfinite(innovation)||(!long_observation&&fabs(innovation)>1000000)){gm_clear(c);return false;}
 double gain[4];for(unsigned i=0;i<4;i++)gain[i]=cp[i][0]/s;
 const double phase=xp[0]+gain[0]*innovation-z,rate=xp[1]+gain[1]*innovation,fast=xp[2]+gain[2]*innovation,acc=xp[3]+gain[3]*innovation;
 if(!isfinite(phase)||!isfinite(rate)||!isfinite(fast)||!isfinite(acc)||fabs(phase)>1000000||fabs(rate)>100000||fabs(fast)+fabs(acc)>1000){gm_clear(c);return false;}
 // Scalar H=[1,0,0,0] covariance update. Evaluate the phase
 // row as R/S times its prediction to avoid cancellation, and write mirrored
 // entries together; this is algebraically the frozen replay Joseph update.
 double pn[4][4];
 for(unsigned i=0;i<4;i++)for(unsigned j=i;j<4;j++){
  const double value=i==0?cp[0][j]*(R/s):cp[i][j]-gain[i]*cp[0][j];
  if(!isfinite(value)){gm_clear(c);return false;}
  pn[i][j]=pn[j][i]=value;
 }
 for(unsigned i=0;i<4;i++)if(pn[i][i]<0){gm_clear(c);return false;}
 c->phase=phase;c->rate=rate;c->fast_accel=fast;c->accel=acc;c->tick=tick;c->ns=ns;memcpy(c->p,pn,sizeof pn);if(c->points<UINT32_MAX)++c->points;return true;
}

// Long gaps are actual authenticated observer measurements, not pseudo-inputs.
// The frozen F and integrated-jerk Q equations use the actual full dt.
// Qualified gm_point retains its original300s numeric/admission behavior.
bool gm_point(struct gm_clock* c,uint64_t tick,uint64_t ns){return point(c,tick,ns,false);}
bool gm_observed_point(struct gm_clock* c,uint64_t tick,uint64_t ns){return point(c,tick,ns,true);}
