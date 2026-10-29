/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "time_zone_selector.h"
#include "../data/time_zone_data.h"
#include <string.h>

static uint16_t u16(size_t at){return (uint16_t)r2_tz_data[at]|((uint16_t)r2_tz_data[at+1]<<8);}
static uint32_t u32(size_t at){return (uint32_t)u16(at)|((uint32_t)u16(at+2)<<16);}
static int16_t i16(size_t at){uint16_t v=u16(at);return v<=INT16_MAX?(int16_t)v:(int16_t)(-1-(int32_t)(UINT16_MAX-v));}
static bool range(size_t at,size_t n){return at<=sizeof r2_tz_data && n<=sizeof r2_tz_data-at;}
static bool header(void){return sizeof r2_tz_data>=32 && !memcmp(r2_tz_data,"TZ01",4) && u16(28)==sizeof r2_tz_data && u16(30)==1;}
size_t r2_tz_db_size(void){return sizeof r2_tz_data;}
const char *r2_tz_db_version(void){return R2_TZ_DB_VERSION;}
bool r2_tz_db_window(uint64_t t){return header() && t/1000000>=u32(4) && t/1000000<u32(8);}
static size_t record(uint16_t id){
 if(!header()||!id)return 0;
 size_t n=u16(12),at=u16(16);
 if(!range(at,n*6))return 0;
 for(size_t i=0;i<n;i++,at+=6)if(u16(at)==id)return at;
 return 0;
}
bool r2_tz_db_offset(uint16_t id,uint64_t utc,int16_t *out){
 if(!out||!r2_tz_db_window(utc))return false;
 size_t r=record(id);if(!r)return false;
 unsigned cal=u16(r+2);if(cal>=u16(14))return false;
 size_t h=u16(18)+cal*6;if(!range(h,6))return false;
 int16_t offset=i16(h);unsigned first=u16(h+2),count=u16(h+4);
 size_t at=u16(20)+(size_t)first*6;
 if(!range(at,(size_t)count*6))return false;
 uint64_t second=utc/1000000;uint32_t previous=u32(4);
 for(unsigned i=0;i<count;i++,at+=6){
  uint32_t transition=u32(at);if(transition<=previous||transition>=u32(8))return false;
  previous=transition;
  if(transition<=second)offset=i16(at+4);
 }
 if(offset < -840 || offset > 840 || (id==1 && offset))return false;
 *out=offset;return true;
}
const char *r2_tz_db_name(uint16_t id){
 size_t r=record(id);if(!r)return "Unknown";
 size_t at=u16(22)+u16(r+4);
 if(!range(at,1)||!memchr(r2_tz_data+at,0,sizeof r2_tz_data-at))return "Unknown";
 return (const char *)r2_tz_data+at;
}
static int floor_degree(int32_t v){return v/10000000-(v%10000000<0);}
uint16_t r2_tz_db_geo(int32_t lat,int32_t lon){
 if(!header()||lat < -900000000||lat>900000000||lon < -1800000000||lon>1800000000)return 0;
 if(lon==1800000000)lon=-1800000000;
 int y=floor_degree(lat),x=floor_degree(lon);
 for(unsigned g=0;g<2;g++){
  size_t grid=u16(24+2*g);if(!range(grid,8))return 0;
  int row=y-i16(grid),col=x-i16(grid+2);
  unsigned rows=r2_tz_data[grid+4],cols=r2_tz_data[grid+5];
  if(row<0||col<0||(unsigned)row>=rows||(unsigned)col>=cols)continue;
  if(!range(grid+8,2*(rows+1)))return 0;
  size_t at=grid+u16(grid+8+(unsigned)row*2),end=grid+u16(grid+10+(unsigned)row*2);
  unsigned prior=0;
  for(unsigned run=0;run<cols && at<end;run++,at+=2){
   if(!range(at,2)||at+2>end)return 0;
   unsigned bound=r2_tz_data[at],index=r2_tz_data[at+1];
   if(bound<=prior||bound>cols)return 0;
   if((unsigned)col<bound){
    if(index==255)return UINT16_MAX;
    if(!index||index>u16(12))return 0;
    size_t r=u16(16)+(index-1)*6;
    return range(r,6)?u16(r):0;
   }
   prior=bound;
  }
  return 0;
 }
 return 0;
}
