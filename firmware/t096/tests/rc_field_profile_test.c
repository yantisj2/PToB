#include "rc_field_profile.h"
#include <assert.h>
int main(void){struct rc5_routes r;
 assert(!rc5_field_routes(0,&r)&&!rc5_field_routes(5,&r));
 assert(rc5_field_routes(1,&r)&&r.node[1].id==UINT64_C(0x1000000000000002)&&r.node[2].id==UINT64_C(0x1000000000000003)&&r.node[1].transmitter&&!r.node[2].transmitter);
 assert(rc5_field_routes(2,&r)&&r.node[1].id==UINT64_C(0x1000000000000003)&&r.node[2].id==UINT64_C(0x1000000000000002)&&r.node[1].transmitter&&!r.node[2].transmitter);
 assert(rc5_field_routes(3,&r)&&r.count==2&&r.node[1].id==UINT64_C(0x1000000000000003)&&r.node[1].parent==0&&!r.node[1].transmitter);
 assert(rc5_field_routes(4,&r)&&r.count==3&&r.node[0].id==UINT64_C(0x1000000000000003)&&r.node[1].id==UINT64_C(0x1000000000000002)&&r.node[2].id==UINT64_C(0x1000000000000001)&&r.node[1].transmitter&&!r.node[2].transmitter&&r.node[2].parent==1);
 return 0;}
