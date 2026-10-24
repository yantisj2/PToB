/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gnss_diagnostics.h"
#include <errno.h>
#include <string.h>
_Static_assert(sizeof(struct r2_gnss_record)==56,"HDOP must fit existing diagnostic-record padding");

static bool valid(const struct r2_gnss_record *r){
 if(!r||!r->root||!r->root_boot||!r->mesh_epoch||!r->sequence||!r->sampled_us||
    !r->valid_us||r->valid_us>R2_GNSS_MAX_VALID_US||r->sampled_us>UINT64_MAX-r->valid_us||
    r->source_kind!=R2_GNSS_SOURCE_LOCAL||(r->known&~63u)||(r->values&~7u)||
    (r->values&~r->known))return false;
 if((r->values&R2_GNSS_V_FIX)&&r->rmc_age_ms>=2500u)return false;
 if((r->values&R2_GNSS_V_PPS)&&r->pps_age_ms>=1500u)return false;
 if((r->values&R2_GNSS_V_QUALIFIED)&&
    (r->values&(R2_GNSS_V_FIX|R2_GNSS_V_PPS))!=(R2_GNSS_V_FIX|R2_GNSS_V_PPS))return false;
 if(r->known&R2_GNSS_K_SATS){
  if(r->sats>99u||r->gga_age_ms>=4000u)return false;
 }else if(r->sats!=R2_GNSS_SATS_UNKNOWN)return false;
 if(r->known&R2_GNSS_K_HDOP){
  if(r->hdop_centi==R2_GNSS_HDOP_UNKNOWN||r->gga_age_ms>=4000u)return false;
 }else if(r->hdop_centi!=R2_GNSS_HDOP_UNKNOWN)return false;
 if(!(r->known&R2_GNSS_K_PTP)&&
    (r->priority1!=128u||r->clock_class!=248u||r->accuracy!=254u||
     r->priority2!=128u||r->variance!=UINT16_MAX))return false;
 return true;
}
int r2_gnss_encode(uint8_t out[R2_GNSS_AD_BYTES],const struct r2_gnss_record *r,const struct r2_group_config *g){
 if(!out||!valid(r)||!r2_group_valid(g))return -EINVAL;
 memset(out,0,R2_GNSS_AD_BYTES);
 out[0]=R2_GNSS_AD_BYTES-1;out[1]=out[2]=out[3]=0xff;
 out[4]='G';out[5]='D';out[6]=2;
 r2_group_put(out+8,g->id,8);r2_group_put(out+16,g->epoch,4);
 r2_group_put(out+20,r->root,8);r2_group_put(out+28,r->root_boot,8);
 r2_group_put(out+36,r->mesh_epoch,4);r2_group_put(out+40,r->sequence,4);
 r2_group_put(out+44,r->sampled_us,8);r2_group_put(out+52,r->valid_us,4);
 out[56]=r->known;out[57]=r->values;out[58]=r->sats;out[59]=r->source_kind;
 r2_group_put(out+60,r->rmc_age_ms,2);r2_group_put(out+62,r->pps_age_ms,2);
 r2_group_put(out+64,r->gga_age_ms,2);
 out[66]=r->priority1;out[67]=r->clock_class;out[68]=r->accuracy;out[69]=r->priority2;
 r2_group_put(out+70,r->variance,2);
 r2_group_put(out+72,r->hdop_centi,2);
 return r2_group_mac(out+R2_GNSS_AD_BODY,g,"R2GNDG02",out,R2_GNSS_AD_BODY);
}
int r2_gnss_decode(struct r2_gnss_record *r,const uint8_t *w,size_t size,const struct r2_group_config *g){
 bool legacy=size==R2_GNSS_LEGACY_AD_BYTES;
 if(!r||!w||(!legacy&&size!=R2_GNSS_AD_BYTES)||!r2_group_valid(g)||
    w[0]!=size-1||w[1]!=0xff||w[2]!=0xff||w[3]!=0xff||
    w[4]!='G'||w[5]!='D'||w[6]!=(legacy?1:2)||w[7]||(legacy&&(w[56]&~31u))||
    r2_group_get(w+8,8)!=g->id||r2_group_get(w+16,4)!=g->epoch||
    !r2_group_verify(w+size-16,g,legacy?"R2GNDG01":"R2GNDG02",w,size-16))return -EACCES;
 struct r2_gnss_record v={
  .root=r2_group_get(w+20,8),.root_boot=r2_group_get(w+28,8),
  .mesh_epoch=r2_group_get(w+36,4),.sequence=r2_group_get(w+40,4),
  .sampled_us=r2_group_get(w+44,8),.valid_us=r2_group_get(w+52,4),
  .known=w[56],.values=w[57],.sats=w[58],.source_kind=w[59],
  .rmc_age_ms=(uint16_t)r2_group_get(w+60,2),.pps_age_ms=(uint16_t)r2_group_get(w+62,2),
  .gga_age_ms=(uint16_t)r2_group_get(w+64,2),
  .priority1=w[66],.clock_class=w[67],.accuracy=w[68],.priority2=w[69],
  .variance=(uint16_t)r2_group_get(w+70,2),
  .hdop_centi=legacy?R2_GNSS_HDOP_UNKNOWN:(uint16_t)r2_group_get(w+72,2)};
 if(!valid(&v))return -EINVAL;
 *r=v;return 0;
}
