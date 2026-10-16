/* SPDX-License-Identifier: Apache-2.0
 * Explicit build/enrollment test profiles. No live role election or new store. */
#pragma once
#include "rc_boundary.h"
enum rc5_field_profile {RC5_TC03_RELAY_CLIENT=1,RC5_TC02_RELAY_CLIENT=2,RC5_TC03_DIRECT_GM=3,RC5_TC03_ROOT_CHAIN=4,RC6_AUTO_TC03_RC_TC02_DIAG=5};
static inline bool rc5_field_routes(unsigned profile,struct rc5_routes *r){
 if(!r||profile<1||profile>5)return false;
 uint64_t rc=(profile==RC5_TC02_RELAY_CLIENT||profile==RC6_AUTO_TC03_RC_TC02_DIAG)?UINT64_C(0x1000000000000003):UINT64_C(0x1000000000000002);
 uint64_t leaf=(profile==RC5_TC02_RELAY_CLIENT||profile==RC6_AUTO_TC03_RC_TC02_DIAG)?UINT64_C(0x1000000000000002):UINT64_C(0x1000000000000003);
 *r=(struct rc5_routes){.count=3,.node={{.id=UINT64_C(0x1000000000000001),.slot=0,.transmitter=true},{.id=rc,.parent=0,.slot=1,.depth=1,.transmitter=true},{.id=leaf,.parent=1,.slot=255,.depth=1}}};
 if(profile==RC5_TC03_DIRECT_GM)*r=(struct rc5_routes){.count=2,.node={{.id=UINT64_C(0x1000000000000001),.slot=0,.transmitter=true},{.id=UINT64_C(0x1000000000000003),.parent=0,.slot=255,.depth=0}}};
 if(profile==RC5_TC03_ROOT_CHAIN)*r=(struct rc5_routes){.count=3,.node={{.id=UINT64_C(0x1000000000000003),.slot=0,.transmitter=true},{.id=UINT64_C(0x1000000000000002),.parent=0,.slot=1,.depth=1,.transmitter=true},{.id=UINT64_C(0x1000000000000001),.parent=1,.slot=255,.depth=1}}};
 return rc5_routes_valid(r);
}
