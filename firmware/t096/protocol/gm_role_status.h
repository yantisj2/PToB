#pragma once
#include "network.h"
#include "role_announcement.h"
#include "gnss_diagnostics.h"
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
/* Descriptive source snapshot only. Actual admission and quality are copied;
 * satellite count and GNSS observations never create a timing permission. */
static inline uint16_t r2_gm_gnss_age(int64_t us){
 if(us<0)return R2_GNSS_AGE_UNKNOWN;
 uint64_t ms=(uint64_t)us/1000;
 return ms>=R2_GNSS_AGE_SATURATED?R2_GNSS_AGE_SATURATED:(uint16_t)ms;
}
static inline struct r2_gnss_record r2_gm_gnss_status(const struct r2_network *n,
 const struct r2_clock_view *v,const struct r2_role_record *role){
 struct r2_gnss_record r={0};
 if(!role->sender)return r;
 r.root=role->root;r.root_boot=role->root_boot;r.mesh_epoch=role->source_epoch;
 r.sampled_us=role->created_us;r.valid_us=R2_GNSS_MAX_VALID_US;
 r.source_kind=R2_GNSS_SOURCE_LOCAL;r.sats=R2_GNSS_SATS_UNKNOWN;
 r.rmc_age_ms=r2_gm_gnss_age(v->gps_age_us);r.pps_age_ms=r2_gm_gnss_age(v->pps_age_us);r.gga_age_ms=r2_gm_gnss_age(v->gga_age_us);
 r.known=R2_GNSS_K_QUALIFIED|R2_GNSS_K_PTP;
 if(v->gps_age_us>=0){r.known|=R2_GNSS_K_FIX;if(v->fix_active)r.values|=R2_GNSS_V_FIX;}
 if(v->pps_age_us>=0){r.known|=R2_GNSS_K_PPS;if(v->pps_present)r.values|=R2_GNSS_V_PPS;}
 if(n->admission.eligible)r.values|=R2_GNSS_V_QUALIFIED;
 if(v->gga_age_us>=0&&v->gga_age_us<4000000&&v->sats_used<=99){r.known|=R2_GNSS_K_SATS;r.sats=(uint8_t)v->sats_used;}
 r.priority1=n->quality.priority1;r.clock_class=n->quality.clock_class;
 r.accuracy=n->quality.accuracy;r.priority2=n->quality.priority2;r.variance=n->quality.variance;
 return r;
}
