/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "dc_gnss_transport.h"
#include <string.h>
size_t dc_gnss_ring_put(struct dc_gnss_ring *r,const uint8_t *p,size_t n){
 size_t put=0;for(;put<n&&r->count<DC_GNSS_RING_SIZE;put++){r->bytes[r->head]=p[put];r->head=(r->head+1)%DC_GNSS_RING_SIZE;r->count++;}
 uint32_t lost=n-put>UINT32_MAX?UINT32_MAX:(uint32_t)(n-put);
 r->dropped=UINT32_MAX-r->dropped<lost?UINT32_MAX:r->dropped+lost;return put;
}
size_t dc_gnss_ring_get(struct dc_gnss_ring *r,uint8_t *p,size_t n){
 size_t got=0;for(;got<n&&r->count;got++){p[got]=r->bytes[r->tail];r->tail=(r->tail+1)%DC_GNSS_RING_SIZE;r->count--;}return got;
}
static bool timing(const uint8_t *p,unsigned n){
 return n>=6&&((!memcmp(p+3,"RMC",3)||!memcmp(p+3,"GGA",3))&&p[0]=='$');
}
void dc_gnss_demux_feed(struct dc_gnss_demux *d,const uint8_t *p,size_t n,int64_t at,dc_gnss_time_sink sink,void *ctx){
 if((d->used||d->binary_left||d->header_used)&&(at<d->started||at-d->started>=250000)){
  if(timing(d->line,d->used)&&d->overflow<UINT32_MAX)d->overflow++;
  d->used=d->binary_left=d->header_used=0;d->discard=false;
 }
 for(size_t i=0;i<n;i++){
  uint8_t c=p[i];
  if(d->binary_left){d->binary_left--;continue;}
  if(d->header_used){
   d->header[d->header_used++]=c;
   if(d->header_used==2&&(c&0xfc)){d->header_used=0;continue;}
   if(d->header_used==3){d->binary_left=((unsigned)d->header[1]<<8|c)+3;d->header_used=0;}
   continue;
  }
  if(c==0xd3){d->used=0;d->discard=false;d->header_used=1;d->header[0]=c;d->started=at;continue;}
  if(c=='$'){d->used=1;d->line[0]=c;d->discard=false;d->started=at;continue;}
  if(!d->used)continue;
  if(c=='\n'){
   if(!d->discard&&(timing(d->line,d->used)||(d->used>=3&&!memcmp(d->line,"$OK",3))||(d->used>=5&&!memcmp(d->line,"$FAIL",5)))){
    d->line[d->used++]='\n';sink(d->line,d->used,d->started,ctx);
   }
   d->used=0;d->discard=false;continue;
  }
  if(d->discard)continue;
  if(c<32&&c!='\r'){d->discard=true;if(timing(d->line,d->used)&&d->overflow<UINT32_MAX)d->overflow++;continue;}
  if(d->used>=sizeof(d->line)-1){d->discard=true;if(timing(d->line,d->used)&&d->overflow<UINT32_MAX)d->overflow++;continue;}
  d->line[d->used++]=c;
 }
}
uint32_t dc_gnss_crc24q(const uint8_t *p,size_t n){
 uint32_t crc=0;for(size_t i=0;i<n;i++){crc^=(uint32_t)p[i]<<16;for(unsigned b=0;b<8;b++){crc<<=1;if(crc&0x1000000)crc^=0x1864cfb;}}return crc&0xffffff;
}
static int hex(uint8_t c){if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;}
bool dc_gnss_command_allowed(const uint8_t *p,size_t n){
 if(n<2||n>127||p[0]!='$')return false;
 while(n&&(p[n-1]=='\r'||p[n-1]=='\n'))n--;
 const uint8_t *star=memchr(p,'*',n);
 if(star){size_t at=(size_t)(star-p);if(at+3!=n||hex(p[at+1])<0||hex(p[at+2])<0)return false;unsigned sum=0;for(size_t i=1;i<at;i++)sum^=p[i];if(sum!=(unsigned)(hex(p[at+1])*16+hex(p[at+2])))return false;n=at;}
 for(size_t i=1;i<n;i++)if(p[i]<32||p[i]>126)return false;
 static const char *const queries[]={"$PDTINFO","$CFGTP","$CFGSYS","$CFGNMEA","$CFGNAV","$CFGPRT,1","$CFGPRT,2"};
 for(size_t i=0;i<sizeof(queries)/sizeof(queries[0]);i++)if(n==strlen(queries[i])&&!memcmp(p,queries[i],n))return true;
 /* Enable navigation messages without disabling the RMC/GGA timing input. */
 if(n==13&&!memcmp(p,"$CFGMSG,0,",10)&&p[10]>='0'&&p[10]<='7'&&p[11]==','&&p[12]=='1')return true;
 if(n==12&&!memcmp(p,"$CFGNMEA,h51",12))return true;
 /* Fixed physical baud, required NMEA+Unicore retained, RTCM enabled.
  * This is an explicit host request, never an automatic receiver mutation. */
 static const char raw[]="$CFGPRT,1,h0,115200,129,7";
 return n==sizeof(raw)-1&&!memcmp(p,raw,n);
}
size_t dc_gnss_frame_byte(struct dc_gnss_frame_parser *f,uint8_t c,int64_t at){
 if(f->used&&(at<f->started||at-f->started>=250000)){f->used=f->wanted=0;if(f->rejected<UINT32_MAX)f->rejected++;}
 if(!f->used){if(c!='$'&&c!=0xd3)return 0;f->started=at;f->wanted=0;}
 if(f->used>=sizeof(f->bytes)){f->used=f->wanted=0;if(f->rejected<UINT32_MAX)f->rejected++;return 0;}
 f->bytes[f->used++]=c;
 if(f->bytes[0]==0xd3){
  if(f->used==2&&(c&0xfc)){f->used=0;if(f->rejected<UINT32_MAX)f->rejected++;return 0;}
  if(f->used==3)f->wanted=(((size_t)f->bytes[1]<<8)|c)+6;
  if(f->wanted&&f->used==f->wanted){size_t n=f->used;f->used=f->wanted=0;if(!dc_gnss_crc24q(f->bytes,n))return n;if(f->rejected<UINT32_MAX)f->rejected++;}
 }else if(c=='\n'){
  size_t n=f->used;f->used=0;if(dc_gnss_command_allowed(f->bytes,n))return n;if(f->rejected<UINT32_MAX)f->rejected++;
 }else if(f->used>=128){f->used=0;if(f->rejected<UINT32_MAX)f->rejected++;}
 return 0;
}
