/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual GD2/GD1 codec with a synthetic public group, never device enrollment. */
#include "gnss_diagnostics.h"
#include "gm_role_status.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const struct r2_group_config group={.id=UINT64_C(0x1122334455667788),.epoch=7,.psk={1}};
static struct r2_gnss_record good(void){
 return (struct r2_gnss_record){.root=UINT64_C(0x0102030405060708),
  .root_boot=UINT64_C(0x1020304050607080),.sampled_us=1000000,
  .mesh_epoch=9,.sequence=3,.valid_us=R2_GNSS_MAX_VALID_US,
  .known=63,.values=7,.sats=12,.source_kind=R2_GNSS_SOURCE_LOCAL,
  .rmc_age_ms=1,.pps_age_ms=2,.gga_age_ms=3,
  .priority1=128,.clock_class=248,.accuracy=254,.priority2=128,.variance=UINT16_MAX,.hdop_centi=123};
}
static void same(const struct r2_gnss_record *a,const struct r2_gnss_record *b){
#define EQ(f) assert(a->f==b->f)
 EQ(root);EQ(root_boot);EQ(sampled_us);EQ(mesh_epoch);EQ(sequence);EQ(valid_us);
 EQ(known);EQ(values);EQ(sats);EQ(source_kind);EQ(rmc_age_ms);EQ(pps_age_ms);EQ(gga_age_ms);
 EQ(priority1);EQ(clock_class);EQ(accuracy);EQ(priority2);EQ(variance);EQ(hdop_centi);
#undef EQ
}
static void sign(uint8_t w[R2_GNSS_AD_BYTES]){
 assert(!r2_group_mac(w+R2_GNSS_AD_BODY,&group,"R2GNDG02",w,R2_GNSS_AD_BODY));
}
static void rejected(const uint8_t *w,size_t n,const struct r2_group_config *g){
 struct r2_gnss_record out=good(),before=out;
 assert(r2_gnss_decode(&out,w,n,g));same(&out,&before);
}
static void invalid(struct r2_gnss_record r){
 uint8_t w[R2_GNSS_AD_BYTES];memset(w,0xa5,sizeof w);
 assert(r2_gnss_encode(w,&r,&group)==-EINVAL);
 for(unsigned i=0;i<sizeof w;i++)assert(w[i]==0xa5);
}
static void builder(void){
 struct r2_network n={0};n.admission.eligible=true;
 n.quality=(struct r2_clock_quality){128,248,254,128,65535,1};
 struct r2_clock_view v={.mono_us=1000000,.gps_age_us=1000,.pps_age_us=2000,
  .gga_age_us=3000,.gga_hdop_centi=123,.fix_active=true,.pps_present=true,.sats_used=12};
 struct r2_role_record role={.sender=UINT64_C(0x0102030405060708),
  .root=UINT64_C(0x0102030405060708),.root_boot=UINT64_C(0x1020304050607080),
  .source_epoch=9,.created_us=1000000};
 struct r2_network original=n;struct r2_clock_view original_view=v;
 struct r2_role_record original_role=role;
 struct r2_gnss_record r=r2_gm_gnss_status(&n,&v,&role);r.sequence=3;
 struct r2_gnss_record expected=good();same(&r,&expected);
 assert(!memcmp(&n,&original,sizeof n)&&!memcmp(&v,&original_view,sizeof v)&&
  !memcmp(&role,&original_role,sizeof role));
 assert(r2_gm_gnss_age(-1)==R2_GNSS_AGE_UNKNOWN&&r2_gm_gnss_age(INT64_MIN)==R2_GNSS_AGE_UNKNOWN);
 assert(r2_gm_gnss_age(0)==0&&r2_gm_gnss_age(999)==0&&r2_gm_gnss_age(1000)==1);
 assert(r2_gm_gnss_age(65533999)==65533&&r2_gm_gnss_age(65534000)==R2_GNSS_AGE_SATURATED);
 assert(r2_gm_gnss_age(INT64_MAX)==R2_GNSS_AGE_SATURATED);
 uint8_t w[R2_GNSS_AD_BYTES];
 /* A good fix/PPS cannot promote the copied60s admission result. */
 n.admission.eligible=false;r=r2_gm_gnss_status(&n,&v,&role);r.sequence=1;
 assert(r.values==3&&r.known==63&&!r2_gnss_encode(w,&r,&group));
 /* No observed GNSS inputs remain unknown, not false reported measurements. */
 v.gps_age_us=v.pps_age_us=v.gga_age_us=-1;v.fix_active=v.pps_present=false;
 r=r2_gm_gnss_status(&n,&v,&role);r.sequence=1;
 assert(r.known==(R2_GNSS_K_QUALIFIED|R2_GNSS_K_PTP)&&!r.values&&
  r.sats==R2_GNSS_SATS_UNKNOWN&&r.rmc_age_ms==R2_GNSS_AGE_UNKNOWN&&
  r.pps_age_ms==R2_GNSS_AGE_UNKNOWN&&r.gga_age_ms==R2_GNSS_AGE_UNKNOWN&&
  !(r.known&R2_GNSS_K_HDOP)&&r.hdop_centi==R2_GNSS_HDOP_UNKNOWN);
 assert(!r2_gnss_encode(w,&r,&group));
 /* Expired GGA never turns its retained/stale count into known zero. */
 v.gga_age_us=4000000;v.sats_used=12;
 r=r2_gm_gnss_status(&n,&v,&role);r.sequence=1;
 assert(!(r.known&(R2_GNSS_K_SATS|R2_GNSS_K_HDOP))&&
  r.sats==R2_GNSS_SATS_UNKNOWN&&r.hdop_centi==R2_GNSS_HDOP_UNKNOWN&&r.gga_age_ms==4000);
 assert(!r2_gnss_encode(w,&r,&group));
 v.gga_age_us=3999999;v.sats_used=0;
 r=r2_gm_gnss_status(&n,&v,&role);r.sequence=1;
 assert((r.known&R2_GNSS_K_SATS)&&(r.known&R2_GNSS_K_HDOP)&&
  r.sats==0&&r.hdop_centi==123&&r.gga_age_ms==3999);
 assert(!r2_gnss_encode(w,&r,&group));
 v.sats_used=100;r=r2_gm_gnss_status(&n,&v,&role);
 assert(!(r.known&R2_GNSS_K_SATS)&&r.sats==R2_GNSS_SATS_UNKNOWN);
 v.gga_hdop_centi=R2_GGA_HDOP_UNKNOWN;r=r2_gm_gnss_status(&n,&v,&role);
 assert(!(r.known&R2_GNSS_K_HDOP)&&r.hdop_centi==R2_GNSS_HDOP_UNKNOWN);
 /* The actual election dataset is copied byte for byte, not derived from SV. */
 n.quality=(struct r2_clock_quality){1,2,3,4,0x5678,99};
 r=r2_gm_gnss_status(&n,&v,&role);r.sequence=1;
 assert(r.priority1==1&&r.clock_class==2&&r.accuracy==3&&r.priority2==4&&r.variance==0x5678);
 assert(!r2_gnss_encode(w,&r,&group));
 role.sender=0;r=r2_gm_gnss_status(&n,&v,&role);assert(!r.root&&!r.root_boot&&!r.mesh_epoch);
 assert(r2_gnss_encode(w,&r,&group));
 puts("PASS actual CONTROL builder copies admission/quality, preserves unknowns and SV freshness, saturates ages and mutates no inputs");
}
int main(void){
 uint8_t w[R2_GNSS_AD_BYTES],changed[R2_GNSS_AD_BYTES];
 struct r2_gnss_record r=good(),out={0};
 assert(R2_GNSS_AD_BYTES==90&&R2_GNSS_AD_BODY==74);
 assert(!r2_gnss_encode(w,&r,&group));assert(!r2_gnss_decode(&out,w,sizeof w,&group));same(&r,&out);
 assert(w[0]==89&&w[1]==255&&w[2]==255&&w[3]==255&&w[4]=='G'&&w[5]=='D'&&w[6]==2&&!w[7]);
 assert(r2_group_get(w+8,8)==group.id&&r2_group_get(w+16,4)==group.epoch);
 assert(r2_group_get(w+20,8)==r.root&&r2_group_get(w+28,8)==r.root_boot);
 assert(r2_group_get(w+36,4)==r.mesh_epoch&&r2_group_get(w+40,4)==r.sequence);
 assert(r2_group_get(w+44,8)==r.sampled_us&&r2_group_get(w+52,4)==r.valid_us);
 assert(w[56]==63&&w[57]==7&&w[58]==12&&w[59]==1);
 assert(r2_group_get(w+60,2)==1&&r2_group_get(w+62,2)==2&&r2_group_get(w+64,2)==3);
 assert(w[66]==128&&w[67]==248&&w[68]==254&&w[69]==128&&r2_group_get(w+70,2)==65535);
 assert(r2_group_get(w+72,2)==123&&sizeof(struct r2_clock_view)==128&&sizeof(struct r2_gnss_record)==56);
 printf("GD2_VECTOR ");for(unsigned i=0;i<sizeof w;i++)printf("%02x",w[i]);puts("");

 /* Frozen GD1 bytes remain strict decode-only compatibility, never guessed
  * from a new record's size/domain. HDOP is explicitly unavailable. */
 uint8_t legacy[R2_GNSS_LEGACY_AD_BYTES];memcpy(legacy,w,R2_GNSS_LEGACY_AD_BODY);
 legacy[0]=87;legacy[6]=1;legacy[56]&=(uint8_t)~R2_GNSS_K_HDOP;
 assert(!r2_group_mac(legacy+R2_GNSS_LEGACY_AD_BODY,&group,"R2GNDG01",legacy,R2_GNSS_LEGACY_AD_BODY));
 assert(!r2_gnss_decode(&out,legacy,sizeof legacy,&group));
 struct r2_gnss_record old=r;old.known&=(uint8_t)~R2_GNSS_K_HDOP;old.hdop_centi=R2_GNSS_HDOP_UNKNOWN;same(&old,&out);
 printf("GD1_VECTOR ");for(unsigned i=0;i<sizeof legacy;i++)printf("%02x",legacy[i]);puts("");
 for(unsigned i=0;i<sizeof legacy;i++)for(unsigned bit=0;bit<8;bit++){
  memcpy(changed,legacy,sizeof legacy);changed[i]^=(uint8_t)(1u<<bit);rejected(changed,sizeof legacy,&group);
 }
 memcpy(changed,legacy,sizeof legacy);changed[56]|=R2_GNSS_K_HDOP;
 assert(!r2_group_mac(changed+R2_GNSS_LEGACY_AD_BODY,&group,"R2GNDG01",changed,R2_GNSS_LEGACY_AD_BODY));
 rejected(changed,sizeof legacy,&group);
 memcpy(changed,legacy,sizeof legacy);changed[6]=2;
 assert(!r2_group_mac(changed+R2_GNSS_LEGACY_AD_BODY,&group,"R2GNDG01",changed,R2_GNSS_LEGACY_AD_BODY));
 rejected(changed,sizeof legacy,&group);

 /* Every bit is authenticated, including the experimental AD framing. */
 for(unsigned i=0;i<sizeof w;i++)for(unsigned bit=0;bit<8;bit++){
  memcpy(changed,w,sizeof w);changed[i]^=(uint8_t)(1u<<bit);rejected(changed,sizeof changed,&group);
 }
 for(unsigned n=0;n<sizeof w;n++)rejected(w,n,&group);
 rejected(w,sizeof w+1,&group);rejected(NULL,sizeof w,&group);
 assert(r2_gnss_decode(NULL,w,sizeof w,&group));
 struct r2_group_config wrong=group;wrong.id^=1;rejected(w,sizeof w,&wrong);
 wrong=group;wrong.epoch++;rejected(w,sizeof w,&wrong);
 wrong=group;wrong.psk[31]^=1;rejected(w,sizeof w,&wrong);
 wrong=group;memset(wrong.psk,0,sizeof wrong.psk);rejected(w,sizeof w,&wrong);
 assert(r2_gnss_encode(changed,&r,&wrong)==-EINVAL);
 assert(r2_gnss_encode(NULL,&r,&group)==-EINVAL);
 assert(r2_gnss_encode(changed,NULL,&group)==-EINVAL);
 assert(r2_gnss_encode(changed,&r,NULL)==-EINVAL);
 memcpy(changed,w,sizeof w);
 assert(!r2_group_mac(changed+R2_GNSS_AD_BODY,&group,"R2ROLE01",changed,R2_GNSS_AD_BODY));
 rejected(changed,sizeof changed,&group);

 /* Authenticated invalid framing/semantic records are also refused. */
 const unsigned fixed[]={0,1,2,3,4,5,6,7};
 for(unsigned i=0;i<sizeof fixed/sizeof fixed[0];i++){
  memcpy(changed,w,sizeof w);changed[fixed[i]]^=1;sign(changed);rejected(changed,sizeof changed,&group);
 }
 const unsigned required[]={20,28,36,40,44,52};
 const unsigned sizes[]={8,8,4,4,8,4};
 for(unsigned i=0;i<sizeof required/sizeof required[0];i++){
  memcpy(changed,w,sizeof w);memset(changed+required[i],0,sizes[i]);sign(changed);rejected(changed,sizeof changed,&group);
 }
 memcpy(changed,w,sizeof w);r2_group_put(changed+52,R2_GNSS_MAX_VALID_US+1u,4);sign(changed);rejected(changed,sizeof changed,&group);
 memcpy(changed,w,sizeof w);r2_group_put(changed+44,UINT64_MAX,8);sign(changed);rejected(changed,sizeof changed,&group);
 for(unsigned at=56;at<=59;at++){
  memcpy(changed,w,sizeof w);changed[at]=at==56?64:at==57?8:at==58?100:2;
  sign(changed);rejected(changed,sizeof changed,&group);
 }
 for(unsigned age=0;age<3;age++){
  memcpy(changed,w,sizeof w);r2_group_put(changed+60+age*2,age==0?2500:age==1?1500:4000,2);
  sign(changed);rejected(changed,sizeof changed,&group);
 }

 /* All known/value combinations: unknown is distinct from known false. */
 unsigned accepted=0,refused=0;
 for(unsigned known=0;known<64;known++)for(unsigned values=0;values<8;values++){
  r=good();r.known=(uint8_t)known;r.values=(uint8_t)values;
  r.sats=known&R2_GNSS_K_SATS?0:R2_GNSS_SATS_UNKNOWN;
  r.hdop_centi=known&R2_GNSS_K_HDOP?123:R2_GNSS_HDOP_UNKNOWN;
  bool expected=!(values&~known)&&(!(values&R2_GNSS_V_QUALIFIED)||
    (values&3u)==3u);
  if(expected){assert(!r2_gnss_encode(changed,&r,&group));assert(!r2_gnss_decode(&out,changed,sizeof changed,&group));same(&r,&out);accepted++;}
  else {invalid(r);refused++;}
 }
 assert(accepted&&refused&&accepted+refused==512);
 r=good();r.values=0;r.known=0;r.sats=R2_GNSS_SATS_UNKNOWN;r.hdop_centi=R2_GNSS_HDOP_UNKNOWN;
 r.rmc_age_ms=r.pps_age_ms=r.gga_age_ms=R2_GNSS_AGE_UNKNOWN;
 assert(!r2_gnss_encode(changed,&r,&group));assert(!r2_gnss_decode(&out,changed,sizeof changed,&group));same(&r,&out);
 r.rmc_age_ms=r.pps_age_ms=r.gga_age_ms=R2_GNSS_AGE_SATURATED;
 assert(!r2_gnss_encode(changed,&r,&group));
 for(unsigned i=0;i<5;i++){
  struct r2_gnss_record bad=r;
  if(i==0)bad.priority1--;if(i==1)bad.clock_class--;if(i==2)bad.accuracy--;
  if(i==3)bad.priority2--;if(i==4)bad.variance--;
  invalid(bad);
 }
 r=good();r.known=R2_GNSS_K_PTP;r.values=0;r.sats=R2_GNSS_SATS_UNKNOWN;r.hdop_centi=R2_GNSS_HDOP_UNKNOWN;
 r.priority1=0;r.clock_class=0;r.accuracy=0;r.priority2=0;r.variance=0;
 assert(!r2_gnss_encode(changed,&r,&group));assert(!r2_gnss_decode(&out,changed,sizeof changed,&group));same(&r,&out);
 r=good();r.rmc_age_ms=2499;r.pps_age_ms=1499;r.gga_age_ms=3999;r.sats=99;
 r.sequence=UINT32_MAX;r.valid_us=1;r.sampled_us=UINT64_MAX-1;
 assert(!r2_gnss_encode(changed,&r,&group));assert(!r2_gnss_decode(&out,changed,sizeof changed,&group));same(&r,&out);
 r=good();r.root=0;invalid(r);r=good();r.root_boot=0;invalid(r);
 r=good();r.mesh_epoch=0;invalid(r);r=good();r.sequence=0;invalid(r);
 r=good();r.sampled_us=0;invalid(r);r=good();r.valid_us=0;invalid(r);
 r=good();r.valid_us=R2_GNSS_MAX_VALID_US+1u;invalid(r);
 r=good();r.sampled_us=UINT64_MAX;invalid(r);
 r=good();r.known=0;invalid(r);r=good();r.values=4;invalid(r);
 r=good();r.rmc_age_ms=R2_GNSS_AGE_UNKNOWN;invalid(r);
 r=good();r.pps_age_ms=R2_GNSS_AGE_SATURATED;invalid(r);
 r=good();r.gga_age_ms=R2_GNSS_AGE_UNKNOWN;invalid(r);
 r=good();r.hdop_centi=R2_GNSS_HDOP_UNKNOWN;invalid(r);
 r=good();r.known&=(uint8_t)~R2_GNSS_K_HDOP;invalid(r);
 r=good();r.known&=(uint8_t)~R2_GNSS_K_SATS;r.sats=R2_GNSS_SATS_UNKNOWN;r.gga_age_ms=4000;invalid(r);
 r=good();r.hdop_centi=65534;assert(!r2_gnss_encode(changed,&r,&group));
 r=good();r.hdop_centi=0;assert(!r2_gnss_encode(changed,&r,&group));
 builder();
 puts("PASS GD2 production90B/GD1legacy88B layout/HMAC,1424 bit mutations,91 lengths,HDOP/known/sentinel semantics,512 flag combinations and decode nonmutation");
 return 0;
}
