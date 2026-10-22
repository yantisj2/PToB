// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Portrait adaptation of T096 pages0/1. No hardware or timing policy here.
#include "ui.hpp"
#include "dual_time.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
namespace n30 { namespace {
#include "font.inc"
void pixel(uint8_t* f,int x,int y,bool on=true){if(x>=0&&x<64&&y>=0&&y<128){const auto bit=uint8_t(0x80>>(x%8));if(on)f[y*8+x/8]|=bit;else f[y*8+x/8]&=uint8_t(~bit);}}
void fill(uint8_t* f,int x,int y,int w,int h,bool on=true){for(int j=0;j<h;j++)for(int i=0;i<w;i++)pixel(f,x+i,y+j,on);}
void box(uint8_t* f,int x,int y,int w,int h){fill(f,x+1,y,w-2,1);fill(f,x+1,y+h-1,w-2,1);fill(f,x,y+1,1,h-2);fill(f,x+w-1,y+1,1,h-2);}
void text(uint8_t* f,int x,int y,const char* s){for(;*s&&x<64;s++,x+=6){const auto g=glyph(*s);for(int c=0;c<5;c++)for(int r=0;r<7;r++)if(g[c]&(1<<r))pixel(f,x+c,y+r);}}
void center(uint8_t* f,int y,const char* s){text(f,(64-int(std::strlen(s))*6+1)/2,y,s);}
void line(uint8_t* f,int x,int y,int xx,int yy){int dx=std::abs(xx-x),sx=x<xx?1:-1,dy=-std::abs(yy-y),sy=y<yy?1:-1,e=dx+dy;for(;;){pixel(f,x,y);if(x==xx&&y==yy)break;const int q=2*e;if(q>=dy){e+=dy;x+=sx;}if(q<=dx){e+=dx;y+=sy;}}}
void circle(uint8_t* f,int cx,int cy,int r){int x=r,y=0,e=1-r;while(x>=y){for(int a:{-1,1})for(int b:{-1,1}){pixel(f,cx+a*x,cy+b*y);pixel(f,cx+a*y,cy+b*x);}y++;if(e<0)e+=2*y+1;else{x--;e+=2*(y-x)+1;}}}
void header(uint8_t* f,const Model& m,bool fresh,int top=0){char s[12];if(fresh&&m.valid)std::snprintf(s,sizeof s,"%.4s>%.5s",m.unit,m.parent);else std::snprintf(s,sizeof s,"%.10s",m.unit);box(f,1,top,62,11);center(f,top+2,s);}
const char* state(const Model& m,bool fresh){if(!fresh)return "STALE";switch(m.state){case State::Off:return "OFF";case State::Search:return "SEARCH";case State::Join:return "JOIN";case State::Acquire:return "ACQUIRE";case State::Follow:return m.locked?"LOCK EST":"SYNCING";case State::Hold:return "HOLD";case State::Fault:return "FAULT";}return "WAIT";}
void number(uint8_t* f,int x,int y,unsigned n){static const uint8_t mask[]={0x3f,6,0x5b,0x4f,0x66,0x6d,0x7d,7,0x7f,0x6f};const auto b=mask[n%10];if(b&1)fill(f,x+1,y,4,1);if(b&2)fill(f,x+5,y+1,1,5);if(b&4)fill(f,x+5,y+7,1,5);if(b&8)fill(f,x+1,y+12,4,1);if(b&16)fill(f,x,y+7,1,5);if(b&32)fill(f,x,y+1,1,5);if(b&64)fill(f,x+1,y+6,4,1);}
void digital(uint8_t* f,int y,bool valid,uint64_t utc){unsigned s=unsigned(utc/1000000%86400),v[]={s/3600/10,s/3600%10,s/60%60/10,s/60%10,s%60/10,s%10};int x=7;for(unsigned i=0;i<6;i++){if(valid)number(f,x,y,v[i]);else fill(f,x+1,y+6,4,1);x+=7;if(i==1||i==3){pixel(f,x,y+4);pixel(f,x,y+8);x+=4;}}}
void dial(uint8_t* f,int cy,uint64_t utc,uint16_t hz){constexpr double pi=3.141592653589793;circle(f,32,cy,21);for(unsigned i=0;i<60;i++){const double a=i*pi/30;const int r=i%5==0?17:20;line(f,32+int(std::lround(std::sin(a)*r)),cy-int(std::lround(std::cos(a)*r)),32+int(std::lround(std::sin(a)*20)),cy-int(std::lround(std::cos(a)*20)));}const double sec=double(utc%60000000)/1000000,minute=double(utc/1000000%3600)/60,hour=double(utc/1000000%43200)/3600;for(auto h:{std::pair<double,int>{hour*pi/6,10},{minute*pi/30,15},{sec*pi/30,18}})line(f,32,cy,32+int(std::lround(std::sin(h.first)*h.second)),cy-int(std::lround(std::cos(h.first)*h.second)));fill(f,31,cy-1,3,3);char s[10];if(hz)std::snprintf(s,sizeof s,"%uHz",unsigned(hz));else std::snprintf(s,sizeof s,"--Hz");fill(f,18,cy+9,28,9,false);center(f,cy+10,s);}
void rms(char* out,size_t n,uint32_t v){if(v==UINT32_MAX)std::snprintf(out,n,"-- ns");else if(v<1000)std::snprintf(out,n,"%u ns",unsigned(v));else if(v<1000000)std::snprintf(out,n,"%u.%02u us",unsigned(v/1000),unsigned(v%1000/10));else std::snprintf(out,n,">999 us");}
void clock_text(uint8_t* f,int y,bool valid,uint64_t utc){char out[12]="--:--:--";if(valid){const unsigned t=unsigned(utc/1000000%86400);std::snprintf(out,sizeof out,"%02u:%02u:%02u",t/3600,t/60%60,t%60);}center(f,y,out);}
void signed_count(char* out,size_t size,int64_t value){
 const uint64_t n=value<0?uint64_t(-(value+1))+1:uint64_t(value);const char sign=value<0?'-':'+';
 if(n<100000)std::snprintf(out,size,"%c%u",sign,unsigned(n));
 else if(n<10000000)std::snprintf(out,size,"%c%uK",sign,unsigned(n/1000));
 else if(n<1000000000)std::snprintf(out,size,"%c%uM",sign,unsigned(n/1000000));
 else std::snprintf(out,size,"%c>1G",sign);
}
void lora_page(uint8_t* f,const Model& m,uint64_t now){
 const auto& l=m.lora;const bool fresh=current(m,now)&&now>=l.sampled_us&&now-l.sampled_us<SnapshotMaxUs;
 box(f,1,0,62,11);center(f,2,"LORA RC8");char out[24];
 center(f,15,l.requested?"RX ON":"RX OFF");
 if(l.mode==1)center(f,25,"KEEP ON");else if(l.mode==2)center(f,25,"KEEP OFF");else{std::snprintf(out,sizeof out,"AUTO %us",unsigned((l.transition_remaining_ms+999)/1000));center(f,25,out);}
 center(f,37,!fresh?"STALE":l.fault?"RADIO FAIL":!l.requested?"DISABLED":!l.active?"STARTING":l.state==2?"MATCH TIME":l.state==3?"EXPIRED":"WAIT RADIO");
 uint64_t utc=0;const bool valid=lora_time(l,now,utc);clock_text(f,48,valid,utc);
 if(valid&&l.link_samples>=4){rms(out,sizeof out,l.link_rms_ns);center(f,60,out);std::snprintf(out,sizeof out,"LINK FIT%u",unsigned(l.link_samples));center(f,70,out);}
 else{center(f,60,valid?"COLLECTING":"NO TIMING");center(f,70,"LINK RMS");}
 if(l.root)std::snprintf(out,sizeof out,"R%04X",unsigned(l.root&65535));else std::snprintf(out,sizeof out,"R----");text(f,2,81,out);
 if(l.parent)std::snprintf(out,sizeof out,"P%04X",unsigned(l.parent&65535));else std::snprintf(out,sizeof out,"P----");text(f,33,81,out);
 if(l.parent_quality)std::snprintf(out,sizeof out,"C%u A%u",unsigned(l.parent_class),unsigned(l.parent_accuracy));else std::snprintf(out,sizeof out,"%s META %s",l.kind==2?"BLE":l.kind==1?"GM":"--",l.metadata_known?"OK":"?");center(f,91,out);
 if(l.age_ms==UINT32_MAX)center(f,101,"AGE --");else if(l.age_ms>=12000)center(f,101,"AGE >12s");else{std::snprintf(out,sizeof out,"AGE %u.%us",unsigned(l.age_ms/1000),unsigned(l.age_ms%1000/100));center(f,101,out);}
 uint64_t ble;center(f,111,ble_time(m,now,ble)&&m.ble.following?"BLE SELECT":m.source==Source::LoRa&&valid?"RC8 SELECT":"NO CLOCK");center(f,121,"HOLD 1.5S");
}
void help(uint8_t* f,const View& v){box(f,1,0,62,11);center(f,2,v.page?"RF KEY":"CLOCK KEY");if(v.page){const char* names[]={"HEL SCAN","SRC GROUP","PRE PA RX","ACK JOIN","CMT MODEL","SRV TX API","RDY N/A","ERR REJECT"};for(unsigned i=0;i<8;i++)text(f,2,15+int(i)*10,names[i]);center(f,99,"DOT RX");center(f,109,"FILL TX");}else{const char* names[]={"RC ONLY","GPS ABSENT","LOCK EST","RELATIVE","RMS IS NOT","UTC ERROR","HOLD LOCAL","4S BLANK"};for(unsigned i=0;i<8;i++)center(f,16+int(i)*11,names[i]);}center(f,120,"PRESS BACK");}
}
void render(uint8_t f[FrameBytes],const Model& m,const View& v,uint64_t now){
 std::memset(f,0,FrameBytes);if(v.blank)return;if(v.help){help(f,v);return;}
 if(v.page==2&&!v.saver){lora_page(f,m,now);return;}
 const bool fresh=current(m,now);uint64_t utc=0;const bool valid=time(m,now,utc),locked=analog_time(m,now,utc);
 if(v.saver){
  // T096's moving assembly: source strip, analog panel, identity strip.
  // Rigidly translated and scaled to64x128; no digital dashboard in saver.
  const int top=4+22*(v.saver_position%3);box(f,1,top,62,11);
  text(f,5,top+2,"UTC");text(f,33,top+2,source_label(m,now));
  if(pps_on(m,now))fill(f,56,top+3,3,5);
  if(v.minimal)return;
  box(f,1,top+13,62,49);
  if(saver_time(m,now,utc))dial(f,top+37,utc,m.hz);else{center(f,top+28,"WAIT");center(f,top+42,"NO TIME");}
  header(f,m,fresh,top+64);return;
 }
 header(f,m,fresh);
 if(v.page==0){
  box(f,1,12,62,28);text(f,5,15,"UTC");text(f,33,15,source_label(m,now));if(pps_on(m,now))fill(f,56,16,3,5);digital(f,24,valid,utc);
  box(f,1,42,62,49);if(locked)dial(f,66,utc,m.hz);else{center(f,54,state(m,fresh));center(f,70,valid?"REL CLOCK":"NO TIME");}
  box(f,1,93,62,35);center(f,96,m.source==Source::LoRa?"LINK RMS":"SYNC RMS");char s[16];rms(s,sizeof s,valid&&m.state==State::Follow?m.rms_ns:UINT32_MAX);center(f,107,s);
  if(locked&&m.source!=Source::LoRa){box(f,3,106,6,8);box(f,4,103,4,5);}fill(f,4,117,56,1);text(f,5,120,"RC");if(fresh&&m.mode_pending)text(f,23,120,">");else if(fresh&&m.relay_only)text(f,23,120,"R");if(fresh&&m.serving)fill(f,20,121,3,3);
  for(unsigned i=0;i<4;i++){int x=35+int(i)*6,h=2+int(i)*2;const bool on=valid&&m.delivery!=UINT16_MAX&&m.delivery>=(i==0?1:i==1?500:i==2?750:900);if(on)fill(f,x,127-h,4,h);else{fill(f,x,127-h,4,1);fill(f,x,126,4,1);}}
 }else{
  box(f,1,12,62,48);text(f,4,15,m.serving&&fresh?"BLE RX+TX":"BLE RX");text(f,4,39,!fresh||m.lora.fault?"LORA FAIL":m.lora.active?"LORA RX":m.lora.requested?"LORA START":"LORA OFF");
  for(unsigned row=0;row<2;row++)for(unsigned i=0;i<8;i++){int x=4+int(i)*7,y=25+int(row)*24;box(f,x,y,6,7);if(fresh&&(row?m.lora_lamps[i]:m.lamps[i])==1)fill(f,x+2,y+2,2,3);else if(fresh&&(row?m.lora_lamps[i]:m.lamps[i])==2)fill(f,x+1,y+1,4,5);}
  uint64_t ble=0,lora=0;const bool bv=ble_time(m,now,ble),lv=lora_time(m.lora,now,lora);
  text(f,3,65,"BLE");text(f,30,65,bv?(m.ble.following?"TRACK":"HOLD"):"WAIT");clock_text(f,77,bv,ble);
  text(f,3,89,"LORA");text(f,36,89,lv?"EST":m.lora.requested?"WAIT":"OFF");clock_text(f,100,lv,lora);fill(f,3,110,58,1);
  char value[12],out[16];if(m.delta_valid&&bv&&lv)signed_count(value,sizeof value,m.delta_ns);else std::snprintf(value,sizeof value,"--");std::snprintf(out,sizeof out,"DNS %s",value);center(f,113,out);
  if(m.drift_valid&&bv&&lv)signed_count(value,sizeof value,m.drift_ppb);else std::snprintf(value,sizeof value,"--");std::snprintf(out,sizeof out,"PPB %s",value);center(f,121,out);
 }
}
void ClockHold::draw(uint8_t frame[FrameBytes],const Model& m,const View& v,uint64_t now)const{
 render(frame,m,v,now);
 if(!v.clock_frozen||!active(now)||v.page!=page_||v.saver!=saver_||v.help||v.blank)return;
 if(saver_)std::memcpy(frame,image_,FrameBytes);
 else{
  // Only RMS digits and the lock (rows103..115) remain live during grace.
  // Keep UTC digits, hands, source/identity and footer from the same frame.
  std::memcpy(frame,image_,103*8);
  std::memcpy(frame+116*8,image_+116*8,12*8);
 }
 // Cached clock pixels are never evidence of a fresh UTC edge. Keep the
 // assembly still during grace, but suppress its PPS lamp without a live gate.
 fill(frame,56,saver_?7+22*(position_%3):16,3,5,false);
 if(saver_&&v.minimal)fill(frame,0,15+22*(position_%3),64,128,false);
}
}
