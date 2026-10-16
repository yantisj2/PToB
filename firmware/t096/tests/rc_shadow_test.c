/* SPDX-License-Identifier: Apache-2.0 */
#include "rc_shadow.h"
#include <assert.h>
int main(void){
 struct rc_shadow_history h={0};struct rc_shadow_map out;
 struct r2_capture e={.epoch=1,.kind=R2_CAPTURE_PPS,.tick=99};
 assert(rc_shadow_select(&h,e,&out)==RC_SHADOW_NO_MAP);
 h.count=1;h.head=1;h.map[0]=(struct rc_shadow_map){.epoch=1,.lo=100,.active=110,.map={.effective=90,.generation=1}};
 assert(rc_shadow_select(&h,e,&out)==RC_SHADOW_NO_MAP);
 for(e.tick=100;e.tick<=110;e.tick++)assert(rc_shadow_select(&h,e,&out)==RC_SHADOW_ACTIVATION_RACE);
 e.tick=111;assert(rc_shadow_select(&h,e,&out)==RC_SHADOW_OK&&out.map.generation==1);
 /* A later estimator, with a backdated effective RF/completion tick, cannot
  * improve or replace the map that was actually available at this edge. */
 h.count=2;h.head=2;h.map[1]=(struct rc_shadow_map){.epoch=1,.lo=200,.active=210,.map={.effective=95,.generation=2}};
 assert(rc_shadow_select(&h,e,&out)==RC_SHADOW_OK&&out.map.generation==1);
 e.tick=200;assert(rc_shadow_select(&h,e,&out)==RC_SHADOW_ACTIVATION_RACE);
 h.count=1;h.head=1;
 e.tick=90+7999999;assert(rc_shadow_select(&h,e,&out)==RC_SHADOW_OK);
 e.tick=90+8000000;assert(rc_shadow_select(&h,e,&out)==RC_SHADOW_EXPIRED);
 e.tick=90+8000001;assert(rc_shadow_select(&h,e,&out)==RC_SHADOW_EXPIRED);
 e.epoch=2;assert(rc_shadow_select(&h,e,&out)==RC_SHADOW_NO_MAP);
 h.count=RC_SHADOW_HISTORY;e.tick=1;e.epoch=1;assert(rc_shadow_select(&h,e,&out)==RC_SHADOW_HISTORY_MISSING);
 e.kind=R2_CAPTURE_NOW;assert(rc_shadow_select(&h,e,&out)==RC_SHADOW_BAD_CAPTURE);
 return 0;
}
