#pragma once
#include "network.h"
#include "role_announcement.h"
// CONTROL-owner snapshot only. This describes existing role gates; it never
// changes candidacy, election, source permission, timing or peer admission.
static inline struct r2_role_record r2_gm_role_status(const struct r2_network *n,
 const uint64_t ids[4],bool gm,bool public_client,uint64_t source_until,uint32_t source_epoch){
 struct r2_role_record r={0};struct v12_dc_snapshot v;
 r2_network_snapshot(n,&v);
 if(!gm||public_client||!v.local_gm_ready||!n->ready||n->stopped||n->self>=4||
    v.elected_root!=n->self||!ids[n->self]||!n->boot||!source_epoch||source_until<=n->now)return r;
 r.sender=r.root=ids[n->self];r.sender_boot=r.root_boot=n->boot;
 r.coordinator=n->coordinator<4?ids[n->coordinator]:0;r.source_epoch=source_epoch;
 r.flags=R2_ROLE_SERVING;r.created_us=n->now;r.members=n->members_mask&15;
 uint64_t remaining=source_until-n->now;
 const unsigned hot=v.hot_standby;
 if(n->hot_ready&&hot<4&&hot!=n->self&&n->peer[hot].connected&&n->peer[hot].authenticated&&
    n->pair.root==n->self&&n->pair.standby==hot&&n->pair.root_boot==n->boot&&
    n->pair.standby_boot==n->peer[hot].boot&&n->pair.until>n->now&&n->management_until>n->now){
   r.hot=r.coordinator=ids[hot];r.hot_boot=n->peer[hot].boot;r.flags|=R2_ROLE_HOT;
   uint64_t pair=n->pair.until-n->now,management=n->management_until-n->now;
   if(pair<remaining)remaining=pair;
   if(management<remaining)remaining=management;
 }
 r.valid_us=remaining>R2_ROLE_MAX_VALID_US?R2_ROLE_MAX_VALID_US:(uint32_t)remaining;
 return r;
}
