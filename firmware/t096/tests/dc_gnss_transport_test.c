/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "dc_gnss_transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned lines;static uint8_t captured[128];static size_t captured_n;static int64_t captured_at;
static void sink(const uint8_t *p,size_t n,int64_t at,void *ctx){(void)ctx;assert(n<=128);memcpy(captured,p,n);captured_n=n;captured_at=at;lines++;}
static size_t feed(struct dc_gnss_frame_parser *p,const uint8_t *b,size_t n,int64_t at){size_t ready=0;for(size_t i=0;i<n;i++){size_t x=dc_gnss_frame_byte(p,b[i],at);if(x)ready=x;}return ready;}
int main(void){
 struct dc_gnss_demux d={0};
 const char *rmc="$GNRMC,120000.00,A,1234.00,N,01234.00,E,0,0,171026,,,A*00\r\n";
 dc_gnss_demux_feed(&d,(const uint8_t *)rmc,7,100,sink,NULL);
 dc_gnss_demux_feed(&d,(const uint8_t *)rmc+7,strlen(rmc)-7,200,sink,NULL);
 assert(lines==1&&captured_n==strlen(rmc)&&captured_at==100&&!memcmp(captured,rmc,captured_n));
 uint8_t packet[100]={0xd3,0,94};memcpy(packet+3,rmc,strlen(rmc));
 dc_gnss_demux_feed(&d,packet,sizeof packet,300,sink,NULL);assert(lines==1);
 dc_gnss_demux_feed(&d,(const uint8_t *)rmc,strlen(rmc),400,sink,NULL);assert(lines==2);
 uint8_t longline[250];memset(longline,'X',sizeof longline);memcpy(longline,"$GNGSV,",7);longline[249]='\n';
 dc_gnss_demux_feed(&d,longline,sizeof longline,500,sink,NULL);assert(lines==2&&!d.overflow);
 memcpy(longline,"$GNRMC,",7);dc_gnss_demux_feed(&d,longline,sizeof longline,600,sink,NULL);assert(d.overflow==1&&lines==2);
 struct dc_gnss_frame_parser f={0};const char *query="$PDTINFO\r\n";
 assert(feed(&f,(const uint8_t *)query,strlen(query),100)==strlen(query));
 const char *bad="$RESET,1,hff\r\n";assert(!feed(&f,(const uint8_t *)bad,strlen(bad),200)&&f.rejected);
 assert(dc_gnss_command_allowed((const uint8_t *)"$CFGMSG,0,3,1\r\n",15));
 assert(!dc_gnss_command_allowed((const uint8_t *)"$CFGMSG,0,4,0\r\n",15));
 assert(!dc_gnss_command_allowed((const uint8_t *)"$CFGTP,1,1,1\r\n",14));
 const char *raw="$CFGPRT,1,h0,115200,129,7\r\n";
 assert(dc_gnss_command_allowed((const uint8_t *)raw,strlen(raw)));
 uint32_t crc=dc_gnss_crc24q(packet,sizeof packet-3);packet[97]=crc>>16;packet[98]=crc>>8;packet[99]=crc;
 assert(!dc_gnss_crc24q(packet,sizeof packet));
 assert(!feed(&f,packet,51,300));assert(feed(&f,packet+51,49,400)==100&&!memcmp(f.bytes,packet,100));
 packet[99]^=1;assert(!feed(&f,packet,100,500));
 feed(&f,(const uint8_t *)"$PDT",4,600);assert(!feed(&f,(const uint8_t *)"INFO\n",5,300600));
 struct dc_gnss_ring r={0};uint8_t in[DC_GNSS_RING_SIZE+10],out[DC_GNSS_RING_SIZE];for(size_t i=0;i<sizeof in;i++)in[i]=(uint8_t)i;
 assert(dc_gnss_ring_put(&r,in,sizeof in)==DC_GNSS_RING_SIZE&&r.dropped==10);
 assert(dc_gnss_ring_get(&r,out,sizeof out)==sizeof out&&!memcmp(out,in,sizeof out));
 puts("DC GNSS: split UART, binary isolation, target faults, command policy, RTCM CRC, original age and bounded backpressure PASS");
}
