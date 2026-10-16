/* SPDX-License-Identifier: Apache-2.0 */
#include "role_announcement.h"
#include <errno.h>
#include <string.h>
static bool valid(const struct r2_role_record *r){
 return r&&r->sender&&r->sender_boot&&r->sender==r->root&&r->sender_boot==r->root_boot&&
 r->source_epoch&&r->sequence&&r->created_us&&r->valid_us&&r->valid_us<=R2_ROLE_MAX_VALID_US&&
 r->members<=15&&(r->flags==R2_ROLE_SERVING||r->flags==(R2_ROLE_SERVING|R2_ROLE_HOT))&&
 ((r->flags&R2_ROLE_HOT)?r->hot&&r->hot_boot&&r->coordinator==r->hot&&r->hot!=r->root:!r->hot&&!r->hot_boot);
}
int r2_role_encode(uint8_t out[R2_ROLE_AD_BYTES],const struct r2_role_record *r,const struct r2_group_config *g){
 if(!out||!valid(r)||!r2_group_valid(g))return -EINVAL;
 memset(out,0,R2_ROLE_AD_BYTES);out[0]=R2_ROLE_AD_BYTES-1;out[1]=0xff;out[2]=out[3]=0xff;out[4]='G';out[5]='S';out[6]=1;out[7]=r->flags;
 r2_group_put(out+8,g->id,8);r2_group_put(out+16,g->epoch,4);
 const uint64_t values[]={r->sender,r->sender_boot,r->root,r->root_boot,r->hot,r->hot_boot,r->coordinator,r->created_us};
 const unsigned offsets[]={20,28,36,44,52,60,68,84};
 for(unsigned i=0;i<8;i++)r2_group_put(out+offsets[i],values[i],8);
 r2_group_put(out+76,r->source_epoch,4);r2_group_put(out+80,r->sequence,4);r2_group_put(out+92,r->valid_us,4);out[96]=r->members;
 return r2_group_mac(out+R2_ROLE_AD_BODY,g,"R2ROLE01",out,R2_ROLE_AD_BODY);
}
int r2_role_decode(struct r2_role_record *r,const uint8_t *w,size_t size,const struct r2_group_config *g){
 if(!r||!w||size!=R2_ROLE_AD_BYTES||!r2_group_valid(g)||w[0]!=R2_ROLE_AD_BYTES-1||w[1]!=0xff||w[2]!=0xff||w[3]!=0xff||w[4]!='G'||w[5]!='S'||w[6]!=1||w[97]||w[98]||w[99]||r2_group_get(w+8,8)!=g->id||r2_group_get(w+16,4)!=g->epoch||!r2_group_verify(w+R2_ROLE_AD_BODY,g,"R2ROLE01",w,R2_ROLE_AD_BODY))return -EACCES;
 struct r2_role_record v={.sender=r2_group_get(w+20,8),.sender_boot=r2_group_get(w+28,8),.root=r2_group_get(w+36,8),.root_boot=r2_group_get(w+44,8),.hot=r2_group_get(w+52,8),.hot_boot=r2_group_get(w+60,8),.coordinator=r2_group_get(w+68,8),.created_us=r2_group_get(w+84,8),.source_epoch=r2_group_get(w+76,4),.sequence=r2_group_get(w+80,4),.valid_us=r2_group_get(w+92,4),.flags=w[7],.members=w[96]};
 if(!valid(&v))return -EINVAL;
 *r=v;return 0;
}
