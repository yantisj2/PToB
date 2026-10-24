/* Copyright (c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Real NMEA/clock bodies: reported HDOP never changes GGA acceptance. */
#include "clock.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void line(char out[128],const char *body){
 unsigned sum=0;for(const char *p=body;*p;p++)sum^=(unsigned char)*p;
 int n=snprintf(out,128,"$%s*%02X",body,sum);assert(n>0&&n<128);
}
static void gga(char out[128],const char *hdop){
 char body[110];int n=snprintf(body,sizeof body,"GNGGA,120000.00,0,N,0,E,1,08,%s,0,M,0,M,,",hdop);
 assert(n>0&&(size_t)n<sizeof body);line(out,body);
}
int main(void){
 _Static_assert(sizeof(struct r2_clock_view)==128,"Diagnostic uses existing padding");
 _Static_assert(offsetof(struct r2_clock_view,gga_hdop_centi)==124,"HDOP tail-padding placement");
 const struct {const char *text;uint16_t value;} good[]={
  {"0",0},{"1",100},{"1.2",120},{"1.23",123},{"655.34",65534},{"000.01",1},{"99.99",9999}};
 const char *bad[]={"","-1","+1","nan","NaN","1e1","1.",".1","1..2","1.234",
  "655.35","656","4294967295"," 1.2","1.2 ","1x"};
 char wire[128];int64_t utc=0;unsigned sats=0;
 for(unsigned i=0;i<sizeof good/sizeof good[0];i++){
  gga(wire,good[i].text);assert(r2_nmea(wire,&utc,&sats)==3&&sats==8);
  assert(r2_nmea_gga_hdop(wire)==good[i].value);
 }
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++){
  gga(wire,bad[i]);assert(r2_nmea(wire,&utc,&sats)==3&&sats==8);
  assert(r2_nmea_gga_hdop(wire)==R2_GGA_HDOP_UNKNOWN);
 }
 gga(wire,"1.23");wire[strlen(wire)-1]^=1;
 assert(r2_nmea(wire,&utc,&sats)==-R2_INPUT_CHECKSUM&&r2_nmea_gga_hdop(wire)==R2_GGA_HDOP_UNKNOWN);
 line(wire,"GNRMC,120000.00,A,0,N,0,E,0,0,081026,,,A");
 assert(r2_nmea(wire,&utc,&sats)==1&&r2_nmea_gga_hdop(wire)==R2_GGA_HDOP_UNKNOWN);
 line(wire,"GNGGA,120000.00,0,N,0,E,1,08");
 assert(r2_nmea(wire,&utc,&sats)==3&&r2_nmea_gga_hdop(wire)==R2_GGA_HDOP_UNKNOWN);
 char unterminated[128];memset(unterminated,'1',sizeof unterminated);
 assert(r2_nmea_gga_hdop(unterminated)==R2_GGA_HDOP_UNKNOWN&&r2_nmea_gga_hdop(NULL)==R2_GGA_HDOP_UNKNOWN);
 struct r2_clock c;r2_clock_init(&c,0,R2_ASSOC_UNKNOWN);assert(c.view.gga_hdop_centi==R2_GGA_HDOP_UNKNOWN);
 for(unsigned i=0;i<sizeof bad/sizeof bad[0]+1;i++){
  gga(wire,i?bad[i-1]:"1.23");size_t n=strlen(wire);wire[n++]='\n';
  int64_t at=1000+(int64_t)i*1000;r2_clock_bytes(&c,(uint8_t*)wire,n,at,at);
  assert(c.view.gga_count==i+1&&c.view.sats_used==8&&c.last_gga==at);
  assert(!c.view.rejected&&!c.view.faults&&!c.view.pairs&&!c.view.accepted&&!c.mapped&&!c.view.source_available);
  assert(c.view.gga_hdop_centi==(i?R2_GGA_HDOP_UNKNOWN:123));
 }
 puts("PASS diagnostic HDOP: exact centi values,16 malformed-but-accepted GGA fields,checksum/RMC/missing/bounded cases,128B unchanged ABI");
 return 0;
}
