#include <assert.h>
#include <stdio.h>
#include "zephyr_fake.h"
#include "../src/radio_audit.c"
int main(void){
 struct r2_radio_audit_record r={.event=16,.tick=UINT64_MAX,.address_raw=UINT32_MAX,.raw=UINT32_MAX},out;
 struct r2_radio_audit_status s;
 assert(!r2_radio_audit_read(0,&out,&s)&&!s.produced);
 r.event=15;r2_radio_audit_put(&r);assert(!r2_radio_audit_read(0,&out,&s));
 r.event=16;for(unsigned i=0;i<11;i++){r.publication=i;r2_radio_audit_put(&r);}
 assert(r2_radio_audit_read(0,&out,&s)&&out.sequence==4&&out.publication==3&&s.produced==11&&s.overwritten==3);
 r.tick=0;assert(out.tick==UINT64_MAX&&out.raw==UINT32_MAX);
 assert(!r2_radio_audit_read(11,&out,&s));
 audit_stats.produced=UINT32_MAX;r2_radio_audit_put(&r);assert(audit_stats.produced==UINT32_MAX);
 puts("PASS bounded raw radio audit: selection, immutable copy, overflow, cursor, saturated sequence");
}
