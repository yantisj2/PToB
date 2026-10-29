/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "time_zone_selector.h"
#include "clock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint16_t zone(const char *name){
 for(unsigned i=1;i<256;i++)if(!strcmp(name,r2_tz_db_name((uint16_t)i)))return (uint16_t)i;
 assert(!"Missing test zone");return 0;
}
static void sentence(char *out,const char *body){
 unsigned checksum=0;for(const char *p=body;*p;p++)checksum^=(unsigned char)*p;
 int n=snprintf(out,128,"$%s*%02X",body,checksum);assert(n>0&&n<128);
}
static void position(void){
 char line[128];struct r2_position p={0};
 sentence(line,"GNRMC,120000.000,A,4152.0000,N,08737.8000,W,0,0,291026,,,A");
 assert(r2_nmea_position(line,&p,0)&&p.valid&&p.received_us==0);
 assert(p.latitude_e7==418666666&&p.longitude_e7==-876300000);
 assert(r2_tz_db_geo(p.latitude_e7,p.longitude_e7)==2);
 sentence(line,"GNRMC,120000.000,A,9000.0000,S,18000.0000,E,0,0,291026,,,A");
 assert(r2_nmea_position(line,&p,1)&&p.latitude_e7==-900000000&&p.longitude_e7==-1800000000);
 const char *bad[]={"GNRMC,120000,A,9160.0,N,08737.8,W,0,0,291026,,,A",
 "GNRMC,120000,A,4152.0,X,08737.8,W,0,0,291026,,,A",
 "GNRMC,120000,A,,N,08737.8,W,0,0,291026,,,A",
 "GNRMC,120000,V,4152.0,N,08737.8,W,0,0,291026,,,A",
 "GNRMC,120000,A,4152.0,N,18000.1,E,0,0,291026,,,A",
 "GNRMC,120000,A,4160.0,N,08737.8,W,0,0,291026,,,A",
 "GNRMC,120000,A,4152.0,N,08737.8,W,0,0,291026,,,N"};
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++){
  sentence(line,bad[i]);struct r2_position before=p;
  assert(!r2_nmea_position(line,&p,2)&&!memcmp(&p,&before,sizeof p));
 }
 sentence(line,"GNRMC,120000,A,4152.0,N,08737.8,W,0,0,291026,,,A");
 line[strlen(line)-1]^=1;assert(!r2_nmea_position(line,&p,3));
 struct r2_clock clock;r2_clock_init(&clock,0,R2_ASSOC_UNKNOWN);
 sentence(line,"GNRMC,120000,A,,N,,W,0,0,291026,,,A");
 strcat(line,"\n");r2_clock_bytes(&clock,(const uint8_t *)line,strlen(line),0,0);
 assert(!clock.position.valid&&clock.view.rejected==0&&clock.view.faults==0);
 sentence(line,"GNRMC,120001,A,4152.0,N,08737.8,W,0,0,291026,,,A");
 strcat(line,"\n");r2_clock_bytes(&clock,(const uint8_t *)line,strlen(line),1000000,1000000);
 assert(clock.position.valid&&clock.position.received_us==1000000);
 r2_clock_bytes(&clock,(const uint8_t *)line,strlen(line),2000000,2000000);
 assert(clock.position.received_us==1000000); /* duplicate RMC is not a new fix */
 r2_clock_fault(&clock,R2_INPUT_STOP,2000001);assert(!clock.position.valid);
}
static void geographic(void){
 assert(r2_tz_db_geo(418800000,-876300000)==2);
 assert(r2_tz_db_geo(334500000,-1120700000)==zone("America/Phoenix"));
 assert(r2_tz_db_geo(476000000,-1223300000)==zone("America/Los_Angeles"));
 assert(r2_tz_db_geo(407100000,-740100000)==zone("America/New_York"));
 assert(r2_tz_db_geo(515000000,-1200000)==zone("Europe/London"));
 assert(r2_tz_db_geo(533400000,-62600000)==zone("Europe/Dublin"));
 assert(r2_tz_db_geo(386800000,-918000000)==2); /* negative longitude floors west */
 assert(!r2_tz_db_geo(-330000000,1510000000));
 assert(!r2_tz_db_geo(900000001,0));
 assert(r2_tz_db_geo(350000000,1800000000)==r2_tz_db_geo(350000000,-1800000000));
 assert(r2_tz_db_geo(462000000,-855000000)==UINT16_MAX); /* unresolved Michigan */
 assert(r2_tz_db_size()<=16384);
}
static void selection(void){
 const uint64_t utc=UINT64_C(1793275200000000); /* 2026-10-29 */
 struct r2_tz_selector s={0};struct r2_position p={.latitude_e7=418800000,.longitude_e7=-876300000,.valid=true};
 r2_tz_selector_step(&s,&p,0,utc,true);assert(s.hits==1&&!s.selected);
 r2_tz_selector_step(&s,&p,1000000,utc,true);assert(s.hits==1);
 p.received_us=60000000;r2_tz_selector_step(&s,&p,60000000,utc,true);assert(s.hits==2);
 p.received_us=120000000;r2_tz_selector_step(&s,&p,120000000,utc,true);assert(s.selected==2);
 struct r2_tz t=r2_tz_selector_value(&s,R2_TZ_AUTOMATIC,(struct r2_tz){0},120000000,utc,true);
 assert(t.id==2&&t.offset_min==-300);
 p.valid=false;r2_tz_selector_step(&s,&p,121000000,utc,true);
 assert(r2_tz_selector_value(&s,2,(struct r2_tz){0},719999999,utc,true).id==2);
 assert(!r2_tz_selector_value(&s,2,(struct r2_tz){0},720000000,utc,true).id);
 p=(struct r2_position){.received_us=180000000,.latitude_e7=0,.longitude_e7=0,.valid=true};
 r2_tz_selector_step(&s,&p,180000000,utc,true);assert(!s.hits&&s.confirmed_at==120000000);
 p=(struct r2_position){.received_us=240000000,.latitude_e7=334500000,.longitude_e7=-1120700000,.valid=true};
 r2_tz_selector_step(&s,&p,240000000,utc,true);assert(s.hits==1&&s.selected==2);
 p.received_us=300000000;r2_tz_selector_step(&s,&p,300000000,utc,true);
 p.received_us=360000000;r2_tz_selector_step(&s,&p,360000000,utc,true);
 assert(s.selected==zone("America/Phoenix"));
 assert(!r2_tz_selector_value(&s,2,(struct r2_tz){0},360000000,utc,false).id);
 assert(!r2_tz_selector_value(&s,2,(struct r2_tz){0},360000000,UINT64_C(1956528000000000),true).id);
 t=r2_tz_selector_value(&s,0,(struct r2_tz){2,-300},360000000,0,false);assert(t.id==2&&t.offset_min==-300);
 t=r2_tz_selector_value(&s,1,(struct r2_tz){2,0},0,utc,true);assert(t.id==2&&t.offset_min==-300);
 s=(struct r2_tz_selector){0};assert(!r2_tz_selector_value(&s,2,(struct r2_tz){0},0,utc,true).id);
}
static void oracle(const char *path){
 FILE *f=fopen(path,"r");assert(f);unsigned id;unsigned long long utc;int offset;unsigned count=0;
 while(fscanf(f,"%u %llu %d",&id,&utc,&offset)==3){
  int16_t result=INT16_MIN;assert(r2_tz_db_offset((uint16_t)id,utc,&result));
  if(result!=offset){fprintf(stderr,"offset mismatch %u %llu %d != %d\n",id,utc,result,offset);abort();}
  ++count;
 }
 assert(feof(f)&&count>200000);fclose(f);
 int16_t result;assert(!r2_tz_db_offset(2,UINT64_C(1767225599999999),&result));
 assert(!r2_tz_db_offset(2,UINT64_C(1956528000000000),&result));
 assert(!r2_tz_db_offset(65000,UINT64_C(1793275200000000),&result));
 printf("PASS %u generated-calendar daily/boundary comparisons in production C\n",count);
}
static void geography_oracle(const char *path){
 FILE *f=fopen(path,"r");assert(f);int lat,lon;unsigned expected,count=0;
 while(fscanf(f,"%d %d %u",&lat,&lon,&expected)==3){
  assert(r2_tz_db_geo(lat,lon)==expected);++count;
 }
 assert(feof(f)&&count==97);fclose(f);
 puts("PASS all 97 pinned IANA principal-location classifications in production C");
}
int main(int argc,char **argv){
 position();geographic();selection();assert(argc==3);oracle(argv[1]);geography_oracle(argv[2]);
 puts("PASS actual coordinate parser/clock boundary, coarse map, selection/hold/expiry/override");
}
