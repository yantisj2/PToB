#include "core.h"
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
int main(void){
 struct r2_request r;const char *base="r2 00000001 1000000000000001 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa 0000000000001234 profile ";char line[192];
 const char *modes[]={"gm","rc","toggle"};for(unsigned i=0;i<3;i++){snprintf(line,sizeof line,"%s%s",base,modes[i]);assert(!r2_parse(line,&r)&&r.op==R2_PROFILE&&r.nonce==i);assert(r2_bound(&r,"1000000000000001","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",0x1234));assert(!r2_bound(&r,"1000000000000001","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",0x5678));}
 const char *bad[]={"","GM","rc extra","permanent","3"};for(unsigned i=0;i<5;i++){snprintf(line,sizeof line,"%s%s",base,bad[i]);assert(r2_parse(line,&r)==-EINVAL);}
 puts("PASS serial GM/RC/toggle syntax, exact physical boot binding, no persistence option, malformed modes rejected");
}
