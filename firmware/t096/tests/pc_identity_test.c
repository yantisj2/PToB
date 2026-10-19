/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
struct r2_identity {char silicon[17];};
struct r2_enrollment {bool rc7_leaf,rc7_boot_rc,rc7_boot_parent,rc7_exclude_direct_gm;struct {unsigned id;}group;};
typedef struct {unsigned type;struct {uint8_t val[6];}a;}bt_addr_le_t;
#define BT_ADDR_LE_RANDOM 1
static struct r2_identity physical;
static struct r2_enrollment enrollment;
static unsigned key_calls,identity_calls,configured,targeted;
static int id_result;
static bool r2_group_valid(const void *g){return ((const struct r2_enrollment*)0)==g?false:((const unsigned*)g)[0]!=0;}
static void r2_group_pair_key(uint8_t out[16],const void *g,uint64_t a,uint64_t b){assert(g&&a==UINT64_C(0x1000000000000001)&&a==b);for(unsigned i=0;i<16;i++)out[i]=(uint8_t)i;++key_calls;}
static int bt_id_create(const bt_addr_le_t *a,void *irk){assert(!configured&&!irk&&a->type==BT_ADDR_LE_RANDOM);assert(!memcmp(a->a.val,(uint8_t[]){0,1,2,3,4,0xc5},6));++identity_calls;return id_result;}
static int r2_dynamic_service_configure(const struct r2_identity *i,const struct r2_enrollment *e){assert(identity_calls&&i&&e);++configured;return 0;}
static int r2_dynamic_service_parent_target(uint64_t p){assert(configured);
#ifdef R2_PC_AUTO_PARENT
 assert(!p);
#else
 assert(p);
#endif
 ++targeted;return 0;}
#include "pc_identity_under_test.h"
int main(void){
 struct r2_identity id={.silicon="1000000000000001"};
 struct r2_enrollment e={.rc7_leaf=true,.rc7_boot_rc=true,.rc7_boot_parent=true,.group={1}};
#ifdef R2_PC_AUTO_PARENT
 assert(r2_service_configure(&id,&e)==-EINVAL&&!identity_calls);e.rc7_boot_parent=false;
#else
 e.rc7_boot_parent=false;assert(r2_service_configure(&id,&e)==-EINVAL&&!identity_calls);e.rc7_boot_parent=true;
#endif
 assert(!r2_service_configure(&id,&e));assert(key_calls==1&&identity_calls==1&&configured==1&&targeted==1);
 configured=targeted=0;id_result=-EIO;assert(r2_service_configure(&id,&e)==-EIO&&!configured&&!targeted);
 id_result=1;assert(r2_service_configure(&id,&e)==-EACCES&&!configured);
 e.group.id=0;unsigned n=identity_calls;assert(r2_service_configure(&id,&e)==-EACCES&&identity_calls==n);
 e.group.id=1;e.rc7_leaf=false;assert(r2_service_configure(&id,&e)==-EINVAL&&identity_calls==n);
 assert(r2_service_configure(NULL,&e)==-EINVAL);
 puts("PASS actual PC configure: canonical group-derived GAP identity before receiver; invalid identity/group/leaf fail closed");
}
