/* SPDX-License-Identifier: Apache-2.0 */
#include "watchdog_policy.h"
enum r2_wd_action r2_wd_policy_step(struct r2_wd_policy *p,uint32_t now,
 bool enabled,bool rc,bool useful,bool pending,uint32_t generation)
{
 if(!enabled){*p=(struct r2_wd_policy){0};return R2_WD_NONE;}
 if(!p->armed){*p=(struct r2_wd_policy){.armed=true,.rc=rc,.since=now,.generation=generation};}
 if(p->recovering){
  /* Existing owners retain their own original 2s drain and 2s PT-ack
   * deadlines. This outer observation bound cannot renew either deadline. */
  if((uint32_t)(now-p->recovery_at)>=4250u)return R2_WD_RESET;
  if(pending||generation==p->generation)return R2_WD_NONE;
  p->recovering=false;p->generation=generation;p->rc=rc;p->since=now;p->healthy_seen=false;
 }else if(rc!=p->rc){
  p->rc=rc;p->generation=generation;p->since=now;p->retries=0;
  p->healthy_seen=false;
 }else if(generation!=p->generation){
  /* A same-profile scope change is not delivered clock recovery. */
  p->generation=generation;p->healthy_seen=false;
  if(!rc){p->since=now;p->retries=0;}
 }
 if(pending){p->healthy_seen=false;return R2_WD_NONE;}
 bool healthy=useful;
 if(rc){
  if(!useful)p->healthy_seen=false;
  else{
   if(!p->healthy_seen||(uint32_t)(now-p->observed_at)>=1000u)p->healthy_at=now;
   p->healthy_seen=true;
  }
  healthy=useful&&(uint32_t)(now-p->healthy_at)>=R2_WD_RC_HEALTHY_MS;
 }
 p->observed_at=now;
 if(healthy){p->since=now;p->retries=0;return R2_WD_NONE;}
 if((uint32_t)(now-p->since)<(rc?R2_WD_RC_MS:R2_WD_GM_MS))return R2_WD_NONE;
 if(p->retries==R2_WD_RETRIES)return R2_WD_RESET;
 ++p->retries;p->recovering=true;p->recovery_at=now;return R2_WD_RESTART;
}
