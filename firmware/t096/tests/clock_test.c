/* SPDX-License-Identifier: Apache-2.0 */
#include "clock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"clock check %u line %d: %s\n",checks,__LINE__,#x);abort();}}while(0)
static void sentence(char *out,const char *body)
{
 unsigned sum=0;for(const char *p=body;*p;p++)sum^=(unsigned char)*p;
 snprintf(out,128,"$%s*%02X\r\n",body,sum);
}
static void rmc_line(char *out,unsigned seconds,char valid)
{
 char body[100];snprintf(body,sizeof(body),"GNRMC,%02u%02u%02u.000,%c,4000.0,N,10000.0,W,0,0,081026,,,A",seconds/3600,(seconds/60)%60,seconds%60,valid);sentence(out,body);
}
static void feed(struct r2_clock *c,unsigned second,int64_t edge,bool pps,bool valid)
{
 char line[128];if(pps)r2_clock_pps(c,edge,edge);
 rmc_line(line,second,valid?'A':'V');
 r2_clock_bytes(c,(uint8_t*)line,strlen(line),edge+200000,edge+200000);
}
static void acquired(struct r2_clock *c,int ppm)
{
 int64_t last=0;
 r2_clock_init(c,0,R2_ASSOC_PRECEDING_UTC_SECOND);
 for(unsigned s=1;s<=100;s++) {
   int64_t edge=(int64_t)s*(1000000+ppm);
   feed(c,s,edge,true,true);
   if(c->mapped){CHECK(c->view.utc_us>=last);last=c->view.utc_us;}
 }
 CHECK(c->view.state==R2_TIME_SYNCED);CHECK(c->view.valid&&c->view.source_available);
 CHECK(llabs(c->view.rate_ppb+(int64_t)ppm*1000)<3000);
 CHECK(llabs(c->view.phase_us)<2000);
}
static void parser(void)
{
 char line[128];int64_t epoch=0;unsigned satellites=0;
 sentence(line,"GNRMC,235959.000,A,0,N,0,E,0,0,311299,,,A");line[strlen(line)-2]=0;
 CHECK(r2_nmea(line,&epoch,&satellites)==1);CHECK(epoch==INT64_C(4102444799000000));
 const char *bad[]={"GNRMC,000000,A,0,N,0,E,0,0,290225,,,A","GNRMC,235960,A,0,N,0,E,0,0,311226,,,A",
 "GNRMC,250000,A,0,N,0,E,0,0,081026,,,A","GNRMC,000000.x,A,0,N,0,E,0,0,081026,,,A"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){sentence(line,bad[i]);line[strlen(line)-2]=0;CHECK(r2_nmea(line,&epoch,&satellites)<0);}
 sentence(line,"GNRMC,000000,A,0,N,0,E,0,0,290224,,,A");line[strlen(line)-2]=0;CHECK(r2_nmea(line,&epoch,&satellites)==1);
 line[4]='X';CHECK(r2_nmea(line,&epoch,&satellites)==-R2_INPUT_CHECKSUM);
}
int main(void)
{
 parser();struct r2_clock c;
 r2_clock_init(&c,0,R2_ASSOC_UNKNOWN);CHECK(c.view.rx_age_us==-1);
 feed(&c,1,1000000,false,false);
 CHECK(c.view.rx_age_us==0&&!c.view.fix_active&&c.view.reason==R2_INPUT_VOID);
 r2_clock_advance(&c,3300000);CHECK(c.view.rx_age_us==2100000&&!c.view.fix_active);

 /* A valid UART aiding ACK must not withdraw already acquired local time. */
 r2_clock_init(&c,0,R2_ASSOC_UNKNOWN);
 for(unsigned s=1;s<=5;s++)feed(&c,s,(int64_t)s*1000000,true,true);
 uint32_t ack_epoch=c.view.source_epoch,ack_faults=c.view.faults;
 int64_t ack_pps=c.last_pps,ack_rmc=c.last_rmc;
 const char *ack="$OK*04\r\n";
 r2_clock_bytes(&c,(const uint8_t*)ack,3,5250000,5250000);
 r2_clock_bytes(&c,(const uint8_t*)ack+3,strlen(ack)-3,5251000,5251000);
 CHECK(c.view.fix_active&&c.view.valid&&c.view.pps_present);
 CHECK(c.view.source_epoch==ack_epoch&&c.view.faults==ack_faults&&c.last_pps==ack_pps&&c.last_rmc==ack_rmc);
 const char *bad_ack="$OK*05\r\n";
 r2_clock_bytes(&c,(const uint8_t*)bad_ack,strlen(bad_ack),5260000,5260000);
 CHECK(c.view.faults==ack_faults+1&&c.view.reason==R2_INPUT_CHECKSUM);

 /* A delayed consumer may not apply source corrections past their original expiry. */
 r2_clock_init(&c,0,R2_ASSOC_PRECEDING_UTC_SECOND);
 for(unsigned s=1;s<=3;s++)feed(&c,s,(int64_t)s*1000000,true,true);
 int64_t pre=c.view.utc_us;r2_clock_advance(&c,10200000);
 CHECK(c.view.utc_us-pre<=7006500&&c.phase==0&&c.view.state==R2_TIME_HOLDOVER);
 r2_clock_init(&c,0,R2_ASSOC_UNKNOWN);
 for(unsigned s=1;s<=15;s++)feed(&c,s,(int64_t)s*1000000,true,true);
 CHECK(c.view.state==R2_TIME_COARSE&&c.view.valid&&!c.view.source_available&&c.view.pairs==0);
 r2_clock_init(&c,0,R2_ASSOC_PRECEDING_UTC_SECOND);
 for(unsigned s=1;s<=10;s++)r2_clock_pps(&c,(int64_t)s*1000000,(int64_t)s*1000000);
 CHECK(!c.view.valid&&!c.view.source_available);
 for(int ppm=-500;ppm<=500;ppm+=100)acquired(&c,ppm);
 acquired(&c,0);uint32_t epoch=c.view.source_epoch;
 int64_t before=c.view.utc_us;
 r2_clock_advance(&c,101500000);CHECK(c.view.state==R2_TIME_HOLDOVER&&!c.view.source_available&&c.view.source_epoch>epoch&&c.view.utc_us>before);
 r2_clock_advance(&c,160000000);CHECK(c.view.state==R2_TIME_EXPIRED&&!c.view.valid);
 for(unsigned s=161;s<=180;s++)feed(&c,s,(int64_t)s*1000000,true,true);
 CHECK(c.view.state==R2_TIME_SYNCED&&c.view.source_epoch>epoch);
 before=c.view.utc_us;feed(&c,181,181000000,true,false);
 CHECK(!c.view.source_available&&c.view.state==R2_TIME_HOLDOVER&&c.view.utc_us>before);
 for(unsigned s=182;s<=200;s++)feed(&c,s,(int64_t)s*1000000,true,true);
 CHECK(c.view.state==R2_TIME_SYNCED);
 epoch=c.view.source_epoch;
 r2_clock_fault(&c,R2_INPUT_RESTART,201000000);CHECK(!c.view.source_available&&c.view.source_epoch>epoch);
 for(unsigned s=202;s<=220;s++)feed(&c,s,(int64_t)s*1000000,true,true);
 CHECK(c.view.state==R2_TIME_SYNCED);
 /* UTC backward step is refused, output never steps back. */
 before=c.view.utc_us;feed(&c,100,221000000,true,true);CHECK(c.view.utc_us>before&&!c.view.source_available);
 /* Stale events retain their original timestamp and cannot restore freshness. */
 char line[128];rmc_line(line,222,'A');r2_clock_bytes(&c,(uint8_t*)line,strlen(line),220000000,222000000);
 CHECK(c.view.reason==R2_INPUT_STALE&&!c.view.source_available);
 acquired(&c,0);epoch=c.view.accepted;int64_t last_rmc=c.last_rmc;
 int64_t pulse=c.last_pps;r2_clock_pps(&c,pulse,100220000);CHECK(c.last_pps==pulse);
 rmc_line(line,100,'A');r2_clock_bytes(&c,(uint8_t*)line,strlen(line),100300000,100300000);
 CHECK(c.view.accepted==epoch&&c.last_rmc==last_rmc);
 /* Consumer tick rollback is a sticky timebase fault, not a giant unsigned delta. */
 r2_clock_advance(&c,100);CHECK(c.view.state==R2_TIME_FAULT&&!c.view.valid);
 r2_clock_advance(&c,102000000);CHECK(c.view.state==R2_TIME_FAULT);
 /* Beyond 32-bit microsecond and millisecond rollover, arithmetic remains 64-bit. */
 int64_t base=INT64_C(4294967296)*1000;r2_clock_init(&c,base,R2_ASSOC_PRECEDING_UTC_SECOND);
 for(unsigned s=1;s<=100;s++)feed(&c,s,base+(int64_t)s*1000000,true,true);
 CHECK(c.view.state==R2_TIME_SYNCED);
 /* Bounded last-RMC evidence distinguishes receiver VOID from malformed
  * input without logging an unrestricted NMEA stream. */
 CHECK(c.view.rmc_status=='A');
 rmc_line(line,101,'V');r2_clock_bytes(&c,(uint8_t*)line,strlen(line),base+101000000,base+101000000);
 CHECK(c.view.rmc_status=='V'&&c.view.reason==R2_INPUT_VOID&&!c.view.source_available);
 /* Deterministic parser damage cannot overrun bounded storage. */
 unsigned seed=123;for(unsigned i=0;i<20000;i++) {seed=1664525*seed+1013904223;uint8_t byte=(uint8_t)(seed>>24);r2_clock_bytes(&c,&byte,1,base+101000000+i,base+101000000+i);}
 CHECK(c.used<sizeof(c.line));
 printf("R2 clock: %u checks PASS (parser, +/-500ppm, lock, loss, holdover, recovery, faults, rollover, fuzz)\n",checks);
}
