#include "ptob_registry.h"
#include "ui.hpp"
#include "font.hpp"
#include "td_chart.hpp"
#include "td_health.hpp"
#include "td_relays.hpp"
#include "../../main/td_epoch.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
namespace sticks3 {
namespace {
// Exact T096 RGB565 palette and pixel face, reflowed for 135x240 portrait.
constexpr uint16_t Bg=0x0820,Panel=0x1042,White=0xffff,Dim=0xa534,Cyan=0x37ff,
 Blue=0x4d1f,Green=0x4fea,Amber=0xfd20,Red=0xf9c7,Bezel=0x3987,Lcd=0x0821;
struct Canvas {
 uint8_t* frame;RenderAudit* audit;int first=0,rows=Height;
 bool intersects(int y,int h)const{return y<first+rows && y+h>first;}
 void pixel(int x,int y,uint16_t c) {
  if(x<0 || y<0 || x>=Width || y>=Height){if(audit)++audit->clipped_pixels;return;}
  if(y<first || y>=first+rows)return;
  const unsigned i=2*((y-first)*Width+x);frame[i]=c>>8;frame[i+1]=c;
 }
 void fill(int x,int y,int w,int h,uint16_t c) {for(int j=std::max(y,first);j<std::min(y+h,first+rows);++j)for(int i=x;i<x+w;++i)pixel(i,j,c);}
 void panel(int x,int y,int w,int h) {
  for(int row=std::max(0,first-y);row<std::min(h,first+rows-y);++row){const int inset=(row==0 || row==h-1)?3:(row==1 || row==h-2)?1:0;
   for(int col=inset;col<w-inset;++col)pixel(x+col,y+row,row==0 || row==h-1 || col==inset || col==w-inset-1?Bezel:Panel);}
 }
 void text(int x,int y,const char* s,int scale,uint16_t color) {
  const int width=int(std::strlen(s))*6*scale-scale;
  if(audit && (x<0 || y<0 || x+width>Width || y+7*scale>Height))++audit->text_overflow;
  if(!intersects(y,7*scale))return;
  for(;*s;++s,x+=6*scale){const auto* g=font::glyph(*s);
   for(int col=0;col<5;++col)for(int row=0;row<7;++row)if(g[col]&(1u<<row))fill(x+col*scale,y+row*scale,scale,scale,color);}
 }
 void center(int y,const char* s,int scale,uint16_t c) {text((Width-(int(std::strlen(s))*6-1)*scale)/2,y,s,scale,c);}
 void line(int x,int y,int tx,int ty,uint16_t c) {
  if(!intersects(std::min(y,ty),std::abs(ty-y)+1))return;
  int dx=std::abs(tx-x),dy=-std::abs(ty-y),sx=x<tx?1:-1,sy=y<ty?1:-1,e=dx+dy;
  for(;;){pixel(x,y,c);if(x==tx && y==ty)break;const int twice=2*e;if(twice>=dy){e+=dy;x+=sx;}if(twice<=dx){e+=dx;y+=sy;}}
 }
 void blend(int x,int y,uint16_t color,unsigned alpha) {
  if(x<0 || x>=Width || y<first || y>=first+rows || y<0 || y>=Height)return;
  const unsigned i=2*((y-first)*Width+x);const uint16_t old=uint16_t(frame[i]<<8)|frame[i+1];
  const unsigned inverse=256-alpha;
  const uint16_t mixed=uint16_t(((((color>>11)*alpha+(old>>11)*inverse)/256)<<11)|
    (((((color>>5)&63)*alpha+((old>>5)&63)*inverse)/256)<<5)|
    (((color&31)*alpha+(old&31)*inverse)/256));
  pixel(x,y,mixed);
 }
 // Q8 endpoints retain subpixel motion; antialias on the already painted
 // clock background without a framebuffer, floating-point work or erasing.
 void smooth_line(int x0,int y0,int x1,int y1,uint16_t color) {
  const bool steep=std::abs(y1-y0)>std::abs(x1-x0);
  if(steep){std::swap(x0,y0);std::swap(x1,y1);}
  if(x1<x0){std::swap(x0,x1);std::swap(y0,y1);}
  const int dx=x1-x0,dy=y1-y0;if(!dx)return;
  for(int x=(x0+255)/256;x<=x1/256;++x){
   const int y=y0+int(int64_t(x*256-x0)*dy/dx),row=y/256;const unsigned alpha=unsigned(y%256);
   if(steep){blend(row,x,color,256-alpha);blend(row+1,x,color,alpha);}
   else{blend(x,row,color,256-alpha);blend(x,row+1,color,alpha);}
  }
 }
 void circle(int cx,int cy,int r,uint16_t c) {
  if(!intersects(cy-r,2*r+1))return;
  int x=r,y=0,e=1-r;
  while(x>=y){pixel(cx+x,cy+y,c);pixel(cx+y,cy+x,c);pixel(cx-x,cy+y,c);pixel(cx-y,cy+x,c);
   pixel(cx+x,cy-y,c);pixel(cx+y,cy-x,c);pixel(cx-x,cy-y,c);pixel(cx-y,cy-x,c);
   ++y;if(e<0)e+=2*y+1;else{--x;e+=2*(y-x)+1;}}
 }
 void row(int y,const char* name,const char* value,uint16_t c=White) {
  text(12,y,name,1,Dim);const int x=123-int(std::strlen(value))*6+1;
  if(audit && x<12+int(std::strlen(name))*6+6)++audit->text_overflow;
  text(x,y,value,1,c);
 }
 void metric(int y,const char* name,uint32_t value,uint16_t c=White) {
  if(!intersects(y,7))return;
  char s[16];
  if(value==UINT32_MAX)std::snprintf(s,sizeof(s),"--");
  else if(value>=1000000000)std::snprintf(s,sizeof(s),"%.2fG",double(value)/1e9);
  else if(value>=1000000)std::snprintf(s,sizeof(s),"%.1fM",double(value)/1e6);
  else std::snprintf(s,sizeof(s),"%lu",(unsigned long)value);
  row(y,name,s,value==UINT32_MAX?Dim:c);
 }
};
const int8_t Sine[60]={0,13,26,39,52,63,75,85,94,103,110,116,121,124,126,127,126,124,121,116,110,103,94,85,75,63,52,39,26,13,0,-13,-26,-39,-52,-63,-75,-85,-94,-103,-110,-116,-121,-124,-126,-127,-126,-124,-121,-116,-110,-103,-94,-85,-75,-63,-52,-39,-26,-13};
int rx(unsigned n,int r){return Sine[n%60]*r/127;}int ry(unsigned n,int r){return -Sine[(n+15)%60]*r/127;}
void digit(Canvas& c,int x,int y,char ch,uint16_t color) {
 static const uint8_t Mask[]={0x3f,6,0x5b,0x4f,0x66,0x6d,0x7d,7,0x7f,0x6f};
 const unsigned m=ch>='0' && ch<='9'?Mask[ch-'0']:0x40;
 if(m&1)c.fill(x+2,y,9,2,color);
 if(m&2)c.fill(x+10,y+2,2,7,color);
 if(m&4)c.fill(x+10,y+12,2,7,color);
 if(m&8)c.fill(x+2,y+19,9,2,color);
 if(m&16)c.fill(x,y+12,2,7,color);
 if(m&32)c.fill(x,y+2,2,7,color);
 if(m&64)c.fill(x+2,y+9,9,3,color);
}
void time(Canvas& c,const View& s,int y) {
 char v[9];const unsigned sec=s.utc%86400;
 std::snprintf(v,sizeof(v),"%02u:%02u:%02u",sec/3600,sec/60%60,sec%60);
 if(!s.time_valid)for(unsigned i=0;i<8;++i)if(v[i]!=':')v[i]='-';
 int x=17;for(unsigned i=0;i<8;++i){if(v[i]==':'){c.fill(x,y+5,2,2,s.time_valid?Green:Dim);c.fill(x,y+15,2,2,s.time_valid?Green:Dim);x+=5;}
  else{digit(c,x,y,v[i],s.time_valid?(s.held?Amber:White):Dim);x+=15;}}
}
void dial(Canvas& c,const View& s) {
 if(!c.intersects(88,81))return;
 const int x=67,y=128;
 for(int dy=std::max(-40,c.first-y);dy<=std::min(40,c.first+c.rows-1-y);++dy)for(int dx=-40;dx<=40;++dx)if(dx*dx+dy*dy<=1600)c.pixel(x+dx,y+dy,Lcd);
 c.circle(x,y,40,Bezel);c.circle(x,y,39,0x3186);
 for(unsigned i=0;i<60;++i)c.line(x+rx(i,i%5?37:32),y+ry(i,i%5?37:32),x+rx(i,37),y+ry(i,37),i%5?Dim:White);
 const unsigned sec=s.utc%60,min=s.utc/60%60,hour=s.utc/3600%12;
 c.fill(x-17,y+13,35,16,Bezel);c.fill(x-16,y+14,33,14,Lcd);c.text(x-11,y+18,"BLE",1,s.held?Amber:Blue);
 c.line(x,y,x+rx(hour*5+min/12,19),y+ry(hour*5+min/12,19),White);
 c.line(x+1,y,x+1+rx(hour*5+min/12,19),y+ry(hour*5+min/12,19),White);
 c.line(x,y,x+rx(min,29),y+ry(min,29),Cyan);
 const int q=s.phase_explicit?s.phase256:std::min(unsigned(s.quarter),3u)*64,nx=(sec+1)%60;
 const int dx=(Sine[sec]*(256-q)+Sine[nx]*q)/256,dy=-(Sine[(sec+15)%60]*(256-q)+Sine[(nx+15)%60]*q)/256;
 if(s.phase_explicit){
  const int sx=Sine[sec]*(256-q)+Sine[nx]*q,sy=-(Sine[(sec+15)%60]*(256-q)+Sine[(nx+15)%60]*q);
  c.smooth_line(x*256-sx*6/127,y*256-sy*6/127,x*256+sx*35/127,y*256+sy*35/127,s.held?Amber:Green);
 }else c.line(x-dx*6/127,y-dy*6/127,x+dx*35/127,y+dy*35/127,s.held?Amber:Green);
 c.fill(x-1,y-1,3,3,s.held?Amber:Green);c.pixel(x,y,Bg);
}
void header(Canvas& c,const View& s) {
 c.panel(8,3,119,21);
 const char* text=s.grandmaster?"Grandmaster":s.header;
 const size_t n=std::strlen(text);const int scale=n<=10 && !s.depth?2:1;
 if(s.depth){c.fill(13,9,9,9,s.solid?Cyan:Panel);c.line(13,9,21,9,Cyan);c.line(13,17,21,17,Cyan);c.line(13,9,13,17,Cyan);c.line(21,9,21,17,Cyan);
  char d[2]={char('0'+std::min(unsigned(s.depth),9u)),0};c.text(15,10,d,1,s.solid?Bg:Cyan);c.text(27,10,text,1,Dim);
 }else c.center(scale==2?7:10,text,scale,s.grandmaster?White:Dim);
}
void precision(Canvas& c,const View& s,int y) {
 if(!c.intersects(y,49))return;
 if(s.presentation_explicit && (s.presentation==HeldClock::Fault || s.presentation==HeldClock::Stale ||
    (!s.metric_history && s.held))){
  c.panel(8,y,119,45);const auto color=s.presentation==HeldClock::Fault?Red:Amber;
  c.center(y+7,s.reason,1,color);char age[20];
  if(s.age_s==UINT32_MAX)std::snprintf(age,sizeof(age),"AGE --");
  else if(s.age_s<3600)std::snprintf(age,sizeof(age),"AGE %lus",(unsigned long)s.age_s);
  else if(s.age_s<86400)std::snprintf(age,sizeof(age),"AGE %luh",(unsigned long)(s.age_s/3600));
  else std::snprintf(age,sizeof(age),"AGE %lud",(unsigned long)(s.age_s/86400));
  c.center(y+21,age,2,color);return;
 }
 c.panel(8,y,119,45);c.fill(13,y+7,14,14,s.ble_enabled?Blue:Bezel);
 c.line(20,y+8,20,y+18,White);c.line(20,y+8,24,y+11,White);c.line(24,y+11,16,y+17,White);c.line(16,y+11,24,y+17,White);c.line(24,y+17,20,y+20,White);
 c.fill(31,y+7,1,14,Bezel);
 c.text(38,y+7,s.grandmaster?"PPS PPB":s.metric_history?(s.percentile==99?"LAST P99":"LAST P95"):
   s.percentile==99?"CAUSAL P99":"CAUSAL P95",1,Dim);
 char v[20];
 const uint32_t number=s.metric_history?s.history_ns:s.precision_ns;
 if(s.grandmaster)std::snprintf(v,sizeof(v),"-- PPB"); // No exported independent local PPS fit.
 else if(s.metric_history && number==UINT32_MAX){
  const unsigned need=s.percentile==99?100:50;
  std::snprintf(v,sizeof(v),"%lu / %u",(unsigned long)std::min(s.history_samples,uint32_t(need)),need);
 }
 else if(number==UINT32_MAX)std::snprintf(v,sizeof(v),"-- ns");
 else if(number<1000000)std::snprintf(v,sizeof(v),"%lu ns",(unsigned long)number);
 else std::snprintf(v,sizeof(v),">=1ms");
 c.center(y+(s.metric_history?18:21),v,2,number==UINT32_MAX || s.grandmaster?Dim:Cyan);
 if(s.metric_history)c.center(y+35,number==UINT32_MAX?"LEARNING / UNKNOWN":"CAUSAL / UNKNOWN",1,Dim);
}
void footer(Canvas& c,const View& s) {for(unsigned i=0;i<8;++i)c.fill(21+int(i)*12,233,6,4,i==s.page?Cyan:Dim);}
void signed_metric(Canvas& c,int y,const char* name,int32_t milli,bool valid) {
 if(!c.intersects(y,7))return;
 char v[20];const double value=double(milli)/1000;
 if(!valid)std::snprintf(v,sizeof(v),"--");
 else if(std::abs(value)>=1e6)std::snprintf(v,sizeof(v),"%+.2fM",value/1e6);
 else if(std::abs(value)>=1e3)std::snprintf(v,sizeof(v),"%+.2fk",value/1e3);
 else std::snprintf(v,sizeof(v),"%+.3f",value);
 c.row(y,name,v,valid?Cyan:Dim);
}
}
View make_view(const DisplayState& s) {
 View v{};std::snprintf(v.header,sizeof(v.header),"%.18s",s.ui_role.header[0]?s.ui_role.header:"SS3");
 std::snprintf(v.role,sizeof(v.role),"%.18s",s.role?s.role:"ACQUIRE");
 if(!std::strncmp(v.header,"NONE",4))std::snprintf(v.header,sizeof(v.header),"S3%.15s",s.ui_role.header+4);
 if(!std::strcmp(v.header,"C-?") || !std::strcmp(v.header,"PToB Clock") || !std::strcmp(v.header,"S3"))std::snprintf(v.header,sizeof(v.header),"SS3");
 constexpr uint8_t pages[]={display_pages::Front,display_pages::Relative,display_pages::Hold,display_pages::Clock,display_pages::BleRx,display_pages::Quality,display_pages::System,display_pages::Gps};
 for(unsigned i=0;i<8;++i)if(s.page==pages[i])v.page=i;
 v.grandmaster=s.ui_role.grandmaster;v.depth=s.ui_role.marker_depth;v.solid=s.ui_role.marker_solid;
 v.time_valid=s.valid && !s.copy_local;v.relative=s.copy_local;v.held=s.utc_holdover || s.ui_role.held;
 v.utc=v.time_valid?s.utc:0;v.provider=s.source_id;v.root=s.root_id;v.age_s=s.age_s;
 v.ble_enabled=s.ble_rx_enabled;v.scan=s.scan;v.fit=s.fit && !s.copy_stale;
 v.precision_ns=v.fit?s.precision_ns:UINT32_MAX;v.percentile=s.hold_percentile;
 v.p95=v.fit?s.copy_p95_ns:UINT32_MAX;v.p99=v.fit?s.copy_p99_ns:UINT32_MAX;v.maximum=v.fit?s.copy_tail_max_ns:UINT32_MAX;v.samples=s.copy_tail_samples;
 v.hold_ms=v.fit?s.hold_1us_ms:0;v.hold_trials=s.hold_trials;v.span_s=s.span_s;v.rate_sigma=s.rate_sigma_ppb;
 v.accepted=s.corrected;v.duplicates=s.duplicates;v.conflicts=s.ambiguous;v.expired=s.expired;
 v.drops=s.basic_queue_drops;v.scan_max_ms=s.basic_scan_max_ms;v.violations=s.basic_scan_violations;
 v.drift_mppb=s.drift_mppb;v.trim_mppb=s.trim_mppb;v.residual_mppb=s.resid_mppb;
 v.rssi=s.basic_rssi;v.rssi_valid=s.basic_callbacks>0;v.channel=s.copy_channel;
 return v;
}

void td_name(char *out, size_t bytes, uint64_t id) {
  const char *name = ptob_registry_name(id);
  if (name)
    std::snprintf(out, bytes, "%s", name);
  else if (id)
    std::snprintf(out, bytes, "%06lX", (unsigned long)(id & 0xffffff));
  else
    std::snprintf(out, bytes, "NONE");
}
const v12::TdPeer *td_best(const v12::TdSnapshot &d, bool relay, bool nearest = false) {
  return v12::TdModel::best_peer(d,relay,d.now_us,nearest);
}
void td_paint(Canvas &c, const View &s) {
  const auto &d = s.td;
  c.fill(0, 0, Width, Height, Bg);
  c.fill(0, 0, Width, 27, Blue);
  c.text(9, 7, "TD", 2, Bg);
  const char *titles[] = {"HEALTH", "STREAM QUALITY", "RELAY TEST", "RF HEALTH",
                          "SAMPLE DETAIL", "SOURCE HEALTH", "EPOCH", "REFERENCE"};
  c.text(40, 10, titles[s.page % v12::TdPageCount], 1, Bg);
  // One-second LCD activity mark remains visible without a nearby source.
  // Its colour reflects snapshot freshness; it grants no timing permission.
  c.fill(130,11,3,3,d.owner_stale?Red:((d.now_us/1000000)%2?White:Bg));
  const auto *gm = td_best(d, false);
  char v[24], name[12];
  const char *stage = d.owner_stale ? "OWNER SNAPSHOT STALE"
                      : d.pending   ? "DRAINING"
                      : d.request   ? "RELAY REQUEST ON"
                      : d.target    ? (d.phase == 1 ? "SAMPLING GM" : "SAMPLING RELAY")
                                    : "PASSIVE SCAN";
  const auto health=TdHealth::evaluate(d,s.radio_error);
  const auto banner=s.td_health_banner.valid?s.td_health_banner:TdHealthBanner::from(health);
  const uint16_t health_color=banner.level==TdHealth::Good?Green:banner.level==TdHealth::Fault?Red:banner.level==TdHealth::Warning?Amber:Dim;
  if(s.page==0||s.page==5){c.fill(8,32,119,17,Panel);c.center(37,banner.text(),1,health_color);}
  else {c.text(9,35,stage,1,d.request?Amber:Cyan);c.fill(9,49,117,1,Bezel);}
  if (s.page == 0) {
    // Two identity columns, one dominant measured readout, then source evidence.
    // The same fixed raster face and blue panel are retained from the device.
    c.text(9,57,"GM",1,Dim);c.text(76,57,"HOT",1,Dim);
    td_name(name,sizeof name,d.role.known?d.role.root:gm?gm->id:0);
    c.text(9,70,name,std::strlen(name)>5?1:2,d.role.available?White:Dim);
    td_name(name,sizeof name,d.role.hot);
    c.text(76,70,d.role.known?(d.role.hot?name:"NONE"):"--",std::strlen(name)>5?1:2,d.role.available&&d.role.hot?White:Dim);
    td_name(name,sizeof name,health.relay?health.relay->id:0);
    c.row(94,"RELAY",name,health.relay?White:Dim);c.fill(9,107,117,1,Bezel);
    td_name(name,sizeof name,health.link?health.link->id:0);c.row(115,"LINK",name);
    if(health.delivery<=1000)std::snprintf(v,sizeof v,"%u.%u",health.delivery/10,health.delivery%10);
    else std::snprintf(v,sizeof v,"--.-");
    c.text(9,128,v,2,White);c.text(72,135,"%",1,Dim);
    c.fill(9,149,78,4,Bezel);
    if(health.delivery<=1000)c.fill(9,149,int(78u*health.delivery/1000),4,health.delivery>=v12::TdDeliveryWarningPermille?Green:Amber);
    const int rssi=health.rssi;
    const unsigned bars=health.bars;
    for(unsigned i=0;i<4;i++)c.fill(100+int(i)*7,146-int(i)*5,5,6+int(i)*5,i<bars?Cyan:Bezel);
    if(rssi!=127)std::snprintf(v,sizeof v,"%ddBm",rssi);else std::snprintf(v,sizeof v,"--dBm");
    c.text(90,157,v,1,Dim);
    c.row(171,"SOURCE",health.source_recent?(health.time_valid?"PERMITTED":"EXPIRED"):"UNKNOWN",health.time_valid?Green:Dim);
    c.row(186,"TIME",health.source_recent?(health.time_valid?"VALID AD":"INVALID AD"):"NOT SAMPLED",health.time_valid?Green:Dim);
    if(health.sample_age_ms!=UINT32_MAX){if(health.sample_age_ms>=10000000)std::snprintf(v,sizeof v,"%luh",(unsigned long)(health.sample_age_ms/3600000));else if(health.sample_age_ms>=100000)std::snprintf(v,sizeof v,"%lus",(unsigned long)(health.sample_age_ms/1000));else std::snprintf(v,sizeof v,"%lu.%lus",(unsigned long)(health.sample_age_ms/1000),(unsigned long)((health.sample_age_ms%1000)/100));}
    else std::snprintf(v,sizeof v,"--");
    c.row(201,"SAMPLE AGE",v,Dim);
    if(health.link&&health.link->jitter_ns!=UINT32_MAX){const auto ns=health.link->jitter_ns;
      if(ns>=1000000)std::snprintf(v,sizeof v,"%lu ms",(unsigned long)(ns/1000000));
      else if(ns>=10000)std::snprintf(v,sizeof v,"%lu us",(unsigned long)(ns/1000));
      else std::snprintf(v,sizeof v,"%lu ns",(unsigned long)ns);
    }
    else std::snprintf(v,sizeof v,"--");
    c.row(216,"JITTER",v,Cyan);
  } else if (s.page == 1) {
    const unsigned metric=s.td_chart%3;
    const uint64_t ids[]={d.plot_gm?d.plot_gm:d.chart_gm,d.plot_relay?d.plot_relay:d.chart_relay};
    for(unsigned kind=0;kind<2;kind++){
      td_name(name,sizeof name,ids[kind]);uint64_t last=0;
      for(const auto& point:(kind?d.relay:d.gm))if(chart_point(point,ids[kind],d.now_us,metric))last=std::max(last,point.at_us);
      if(last)std::snprintf(v,sizeof v,"%s %s %lus",kind?"RC":"GM",name,(unsigned long)((d.now_us-last)/1000000));
      else std::snprintf(v,sizeof v,"%s %s LAST",kind?"RC":"GM",name);
      c.text(9,59+14*kind,v,1,kind?Amber:Cyan);
    }
    const auto scale=TdLogScale::range(d,ids[0],ids[1]);
    const auto linear=TdLinearScale::range(d,ids[0],ids[1],metric);
    const char* labels[]={"LOG JITTER ns","PA LOSS %","RSSI dBm"};
    c.text(9,89,labels[metric],1,Dim);c.line(44,103,44,169,Bezel);
    if(!metric)for(unsigned decade=scale.lower;decade<=scale.upper;decade++){
      const int y=scale.tick(decade);c.line(44,y,124,y,decade==scale.lower?Bezel:Panel);
      if(decade!=scale.lower&&decade!=scale.upper&&(scale.upper-scale.lower>4)&&(decade-scale.lower)%2)continue;
      uint64_t value=1;for(unsigned n=0;n<decade;n++)value*=10;
      if(value>=1000000000)std::snprintf(v,sizeof v,"%lluG",(unsigned long long)(value/1000000000));
      else if(value>=1000000)std::snprintf(v,sizeof v,"%lluM",(unsigned long long)(value/1000000));
      else if(value>=1000)std::snprintf(v,sizeof v,"%lluk",(unsigned long long)(value/1000));
      else std::snprintf(v,sizeof v,"%llu",(unsigned long long)value);
      c.text(9,y-3,v,1,Dim);
    }else for(unsigned i=0;i<3;i++){
      const int y=168-int(i)*32,value=linear.lower+(linear.upper-linear.lower)*int(i)/2;c.line(44,y,124,y,i?Panel:Bezel);
      if(metric==1)std::snprintf(v,sizeof v,"%d.%d",value/10,value%10);else std::snprintf(v,sizeof v,"%d",value);
      c.text(9,y-3,v,1,Dim);
    }
    auto draw=[&](const v12::TdTrace* trace,uint64_t id,uint16_t color){
      int px=0,py=0,lastx=0,lasty=0;uint64_t prior=0,last=0;
      for(unsigned i=0;i<24;i++){
        const auto& point=trace[i];if(!chart_point(point,id,d.now_us,metric)){prior=0;continue;}
        const int x=123-int((d.now_us-point.at_us)*78/120000000),y=metric?linear.y(point):scale.y(point.jitter_ns);
        if(prior&&point.at_us>=prior&&point.at_us-prior<=30000000)c.line(px,py,x,y,color);
        c.fill(x-1,y-1,3,3,color);px=x;py=y;prior=point.at_us;
        if(point.at_us>=last){last=point.at_us;lastx=x;lasty=y;}
      }
      if(last)for(int x=lastx+3;x<=123;x+=5)c.line(x,lasty,std::min(x+1,123),lasty,color);
    };
    draw(d.gm,ids[0],Cyan);draw(d.relay,ids[1],Amber);
    c.text(44,179,"-120s     NOW",1,Dim);
    c.text(9,190,"HOLD: NEXT CHART",1,White);
    c.text(9,205,"DASH LAST / GAP --",1,Dim);
    c.text(9,219,metric?"OBSERVED / SAMPLED":"0 FLOOR 1ns / NO UTC",1,Amber);
  } else if (s.page == 2) {
    const TdRelayList relays(d);
    const auto *nearest = relays.rows[0];
    td_name(name, sizeof name, d.request ? d.request_target : nearest ? nearest->id : 0);
    c.text(9, 59, d.request ? "REQUESTED RELAY" : "NEAREST BY RSSI", 1, Dim);
    c.text(9, 74, name, 2, White);
    const auto* featured = nearest;
    if (d.request) {
      featured = nullptr;
      for (const auto& p : d.peers)
        if (p.id == d.request_target && v12::TdModel::fresh(p, d.now_us)) featured = &p;
    }
    if (featured && featured->signal.fresh(d.now_us))
      std::snprintf(v, sizeof v, "%d dBm / HOP %u", int(featured->signal.value), unsigned(featured->depth));
    else
      std::snprintf(v, sizeof v, "NO FRESH RELAY");
    c.text(9, 95, v, 1, Amber);
    for (unsigned row = 0; row < relays.count; ++row) {
        const auto& p = *relays.rows[row];
        td_name(name, sizeof name, p.id);
        char stream[4] = "-";
        if (p.publishing) {
          if (p.stream_slot > 0 && p.stream_slot <= 3)
            std::snprintf(stream, sizeof stream, "T%u", unsigned(p.stream_slot) + 1);
          else
            std::snprintf(stream, sizeof stream, "T?");
        }
        if (!p.signal.fresh(d.now_us))
          std::snprintf(v, sizeof v, "%-6s  -- H%u %s", name, unsigned(p.depth), stream);
        else
          std::snprintf(v, sizeof v, "%-6s %d H%u %s", name, int(p.signal.value), unsigned(p.depth), stream);
        c.text(9, 117 + row * 16, v, 1, White);
      }
    if (!relays.count)
      c.text(9, 127, "NO RELAY ADVERTS", 1, Dim);
    c.text(9, 186, "HOLD 1.5s: REQ ON/OFF", 1, d.request ? Amber : Cyan);
    std::snprintf(v, sizeof v, "TOP %u/%u RSSI T:STREAM", relays.count, relays.total);
    c.text(9, 202, v, 1, Dim);
    c.text(9, 218, "OFF: LEASE MAX 5s", 1, Dim);
  } else if (s.page == 3) {
    uint64_t matched = 0, gaps = 0, rejects = 0, channels = 0;
    int rssi_min = 127, rssi_max = -127;
    bool have_rssi = false;
    for (const auto &p : d.peers)
      if (p.id) {
        matched += p.matched;
        gaps += p.missed;
        rejects += p.rejected;
        channels |= p.channels;
        if (p.rssi != 127) {
          have_rssi = true;
          rssi_min = std::min(rssi_min, int(p.rssi_min));
          rssi_max = std::max(rssi_max, int(p.rssi_max));
        }
      }
    const auto bounded = [](uint64_t n) { return uint32_t(std::min<uint64_t>(UINT32_MAX - 1, n)); };
    c.row(59, "SCOPE", "BOOT TOTAL");
    c.metric(77, "PA RX", s.accepted);
    c.metric(95, "MATCHED", bounded(matched));
    c.metric(113, "GAPS", bounded(gaps), Amber);
    c.metric(131, "AUTH REJ", bounded(rejects), rejects ? Red : White);
    c.metric(149, "CHANNELS", unsigned(__builtin_popcountll(channels)));
    if (have_rssi)
      std::snprintf(v, sizeof v, "%d..%d", rssi_min, rssi_max);
    else
      std::snprintf(v, sizeof v, "--");
    c.row(167, "RSSI dBm", v);
    c.metric(185, "RX NO MATCH", d.unmatched, d.unmatched ? Amber : White);
    c.metric(203, "API ERR", s.radio_error, s.radio_error ? Red : White);
    c.text(9, 219, "CUMULATIVE / NO CCA", 1, Dim);
  } else if(s.page==4) {
    const v12::TdPeer *p = nullptr;
    for (const auto &row : d.peers)
      if ((d.target ? d.target : d.last_target) && row.id == (d.target ? d.target : d.last_target))
        p = &row;
    td_name(name, sizeof name, p ? p->id : 0);
    c.row(59, "STREAM", name);
    c.metric(78, "JIT ns", p ? p->jitter_ns : UINT32_MAX, Cyan);
    c.metric(97, "MAX ns", p ? p->max_ns : UINT32_MAX, Amber);
    c.metric(116, "AGE ms",
             p && p->sampled_us && d.now_us >= p->sampled_us
                 ? uint32_t(std::min<uint64_t>((d.now_us - p->sampled_us) / 1000, UINT32_MAX))
                 : UINT32_MAX);
    c.metric(135, "SRC AGE us", p ? p->source_age_us : UINT32_MAX);
    const auto total = p ? uint64_t(p->packets) + p->missed : 0;
    c.metric(154, "RX /1000", total ? uint32_t(uint64_t(p->packets) * 1000 / total) : UINT32_MAX);
    c.metric(173, "PHY", p ? p->phy : UINT32_MAX);
    c.metric(192, "SYNC FAIL", d.sync_failures);
    c.text(9, 213, "RELATIVE / SAMPLED", 1, Dim);
    c.text(9, 224, "NO OUTPUT ACCURACY", 1, Amber);
  }
  else if(s.page==5) {
    const auto* p=health.gm;
    td_name(name,sizeof name,d.role.known?d.role.root:p?p->id:0);c.row(57,"GM",name);
    td_name(name,sizeof name,d.role.hot);c.row(70,"HOT",d.role.known?(d.role.hot?name:"NONE"):"UNKNOWN");
    const auto report_age=TdHealth::age(d.role.seen_us,d.now_us);
    if(report_age!=UINT32_MAX)std::snprintf(v,sizeof v,"%lus",(unsigned long)(report_age/1000));else std::snprintf(v,sizeof v,"--");
    c.row(83,"REPORT AGE",v);c.metric(96,"EPOCH",p?p->source_epoch:UINT32_MAX);
    c.fill(9,108,117,1,Bezel);
    c.row(118,"SOURCE",health.source_recent?(health.time_valid?"PERMITTED":"EXPIRED"):"UNKNOWN",health.time_valid?Green:Dim);
    c.row(131,"TIME",health.source_recent?(health.time_valid?"VALID AD":"INVALID AD"):"NOT SAMPLED",health.time_valid?Green:Dim);
    c.metric(144,"TX AGE ms",p&&p->origin_age_us!=UINT32_MAX?p->origin_age_us/1000:UINT32_MAX);
    c.metric(157,"TX TTL ms",p&&p->source_ttl_us!=UINT32_MAX?p->source_ttl_us/1000:UINT32_MAX);
    c.row(170,"GPS/PPS","UNKNOWN",Dim);c.row(183,"UTC ERROR","UNKNOWN",Dim);
    c.fill(9,196,117,1,Bezel);c.text(9,205,"ROOT BOOT",1,Dim);
    if(d.role.known)std::snprintf(v,sizeof v,"%016llX",(unsigned long long)d.role.root_boot);else std::snprintf(v,sizeof v,"--");
    c.text(9,218,v,1,White);
  }
  else if(s.page==6) {
    const auto& r=d.mesh.roles;
    char birth[32];v12::td_mesh::epoch_start_text(birth,sizeof birth,r.birth_utc_us);
    const auto epoch=v12::td_mesh::epoch_time(d.mesh,d.now_us);
    const bool known=epoch.basis!=v12::td_mesh::EpochBasis::Unknown;
    const bool current=known&&epoch.current&&!d.owner_stale;
    c.text(9,57,"EPOCH START / UTC",1,Dim);
    if(r.wire_version==3&&std::strlen(birth)==19){
      birth[10]=0;c.text(9,74,birth,2,Cyan);c.text(20,96,birth+11,2,White);
    }else{c.text(9,74,"--",2,Dim);c.text(9,98,"NO SIGNED START",1,Dim);}
    c.text(9,124,epoch.basis==v12::td_mesh::EpochBasis::ReportedUtc?"ELAPSED EST":"ELAPSED",1,Dim);
    const auto seconds=epoch.elapsed_us/1000000;
    if(known&&seconds>=86400)std::snprintf(v,sizeof v,"%llud",(unsigned long long)(seconds/86400));else std::snprintf(v,sizeof v,"%s",known?" ":"--");
    c.text(9,137,v,1,White);
    if(known)std::snprintf(v,sizeof v,"%02u:%02u:%02u",unsigned(seconds/3600%24),unsigned(seconds/60%60),unsigned(seconds%60));else std::snprintf(v,sizeof v,"--:--:--");
    c.text(9,151,v,2,known?White:Dim);
    const auto up=d.now_us/1000000;
    if(up/86400>=1000)std::snprintf(v,sizeof v,"%llud",(unsigned long long)(up/86400));
    else if(up>=86400)std::snprintf(v,sizeof v,"%llud %02u:%02u",(unsigned long long)(up/86400),unsigned(up/3600%24),unsigned(up/60%60));
    else std::snprintf(v,sizeof v,"%02u:%02u:%02u",unsigned(up/3600),unsigned(up/60%60),unsigned(up%60));
    c.row(181,"UPTIME",v,Dim);
    td_name(name,sizeof name,r.known?r.root:0);c.row(197,"GM",name);
    c.text(9,216,known?(current?"GM7 UTC / LIVE":"GM7 UTC / LAST"):"WAIT FOR GM7 UTC",1,current?Green:Dim);
  } else if(s.page==7) {
    const auto& q=d.mesh.reference;
    const bool live=!d.owner_stale&&v12::td_mesh::current(q,d.mesh.selected,d.now_us);
    td_name(name,sizeof name,q.reported?q.scope.root:0);c.row(59,live?"GM":"LAST GM",name,q.reported?White:Dim);
    if(q.reported)std::snprintf(v,sizeof v,"%s %s",q.wire_version==3?"GM7":q.wire_version==2?"GD2":"GD1",live?"LIVE":"LAST");else std::snprintf(v,sizeof v,"WAITING");
    c.row(73,"REPORT",v,live?Green:Dim);
    const auto flag=[&](unsigned bit){return !live||!(q.fields&bit)?"--":q.values&bit?"YES":"NO";};
    std::snprintf(v,sizeof v,"FIX %s / PPS %s",flag(v12::td_mesh::Fix),flag(v12::td_mesh::Pps));c.text(9,91,v,1,live?White:Dim);
    c.row(106,"QUALIFIED",flag(v12::td_mesh::Qualified),live&&(q.fields&v12::td_mesh::Qualified)&&(q.values&v12::td_mesh::Qualified)?Green:Dim);
    c.metric(122,"SV",live&&(q.fields&v12::td_mesh::Satellites)?q.sats:UINT32_MAX);
    if(live&&(q.fields&v12::td_mesh::Hdop)&&q.hdop_centi!=UINT16_MAX)std::snprintf(v,sizeof v,"%u.%02u",q.hdop_centi/100,q.hdop_centi%100);else std::snprintf(v,sizeof v,"--");
    c.row(138,"HDOP",v,live?Cyan:Dim);
    const bool ptp=live&&(q.fields&v12::td_mesh::Ptp);
    c.metric(154,"PTP CLASS",ptp?q.clock_class:UINT32_MAX);
    if(ptp&&q.accuracy!=254)std::snprintf(v,sizeof v,"0x%02X",q.accuracy);else std::snprintf(v,sizeof v,ptp?"UNKNOWN":"--");c.row(170,"ACCURACY",v,Dim);
    if(ptp&&q.variance!=UINT16_MAX)std::snprintf(v,sizeof v,"0x%04X",q.variance);else std::snprintf(v,sizeof v,ptp?"NONE":"--");c.row(186,"VAR",v,Dim);
    if(ptp)std::snprintf(v,sizeof v,"%u/%u",q.priority1,q.priority2);else std::snprintf(v,sizeof v,"--");c.row(202,"P1/P2",v);
    c.text(9,219,"REPORTED / NO UTC",1,Amber);
  }
  if (s.mode_banner[0]) {
    c.fill(8, 104, 119, 34, Panel);
    char banner[24];
    std::snprintf(banner, sizeof banner, "%s", s.mode_banner);
    for (auto &ch : banner)
      if (ch >= 'a' && ch <= 'z')
        ch = char(ch - 'a' + 'A');
    c.center(117, banner, 1, Cyan);
  }
  for (unsigned i = 0; i < v12::TdPageCount; i++)
    c.fill(18 + i * 13, 234, 6, 3, i == s.page ? Amber : Bezel);
}
static void native_paint(Canvas& c,const View& s) {
 if(s.td.enabled){td_paint(c,s);return;}
 c.fill(0,0,Width,Height,Bg);
 c.text(12,9,"SS3",2,White);
 char mode[8];if(s.depth)std::snprintf(mode,sizeof mode,"RC%u",unsigned(s.depth));else std::snprintf(mode,sizeof mode,"RC");
 c.text(99,12,mode,1,s.relay_ready?Green:s.relay_online?Amber:Dim);
 c.fill(8,31,119,1,Bezel);c.center(37,s.upstream,1,s.time_valid?Cyan:Dim);
 time(c,s,53);c.center(82,s.role,1,s.time_valid?(s.held?Amber:Green):Amber);
 if(s.page==0){
  if(s.time_valid)dial(c,s);else{c.center(118,"WAITING FOR CLOCK",1,Amber);c.center(142,"GM / RELAY SEARCH",1,Dim);}
  char value[20];if(s.native_rms_ns==UINT32_MAX)std::snprintf(value,sizeof value,"RMS --");
  else std::snprintf(value,sizeof value,"RMS %lu ns",(unsigned long)s.native_rms_ns);
  c.center(179,value,1,Cyan);
  std::snprintf(value,sizeof value,"HOLD %lu ms",(unsigned long)s.hold_ms);c.center(196,value,1,Dim);
  c.center(214,s.relay_only?"RELAY ONLY":"RELAY CLIENT",1,s.relay_only?Amber:Cyan);
 }else{
  c.row(107,"UPSTREAM",s.held?"HELD":s.time_valid?"LIVE":"NONE",s.held?Amber:s.time_valid?Green:Dim);
  c.row(128,"SERVER",s.relay_ready?"READY":s.relay_online?"STANDBY":"WAIT",s.relay_ready?Green:Amber);
  c.metric(151,"RX",s.accepted);c.metric(173,"LOCK S",uint32_t(std::min<uint64_t>(s.lock_seconds,UINT32_MAX)));
  c.center(199,"BLE 100%",1,Cyan);c.center(219,"TX EST / UNQUALIFIED",1,Amber);
 }
 if(s.page) c.center(227,s.relay_only?"RELAY ONLY":"RELAY CLIENT",1,s.relay_only?Amber:Cyan);
 if(s.mode_banner[0]){char label[24];std::snprintf(label,sizeof label,"%s",s.mode_banner);for(auto& ch:label)if(ch>='a'&&ch<='z')ch=char(ch-'a'+'A');c.panel(8,110,119,43);c.center(128,label,1,Cyan);}
 for(unsigned i=0;i<2;++i)c.fill(57+int(i)*14,235,6,3,i==s.page?Cyan:Dim);
}
static void paint(Canvas& c,const View& s) {
 if(s.native_dashboard){native_paint(c,s);return;}
 c.fill(0,0,Width,Height,Bg);header(c,s);
 if(s.page==0){
  c.panel(8,29,119,47);c.text(14,35,s.relative?"REL":"UTC",1,Dim);
  c.text(98,35,!s.time_valid?"WAIT":s.held?"HOLD":"BLE",1,!s.time_valid?Dim:s.held?Amber:Green);time(c,s,48);
  c.panel(8,81,119,94);
  if(s.time_valid)dial(c,s);else{c.center(99,s.presentation_explicit?s.reason:s.ble_enabled?"ACQUIRING":"BLE OFF",1,s.presentation==HeldClock::Fault?Red:Amber);c.center(120,"TIME --",2,Dim);c.center(153,"PUBLIC LISTENER",1,Dim);}
  precision(c,s,181);
 }else if(s.page==1){
  c.panel(8,29,119,47);c.text(14,35,"UTC",1,Dim);c.text(98,35,s.time_valid?(s.held?"HOLD":"BLE"):"WAIT",1,s.held?Amber:s.time_valid?Green:Dim);time(c,s,48);
  precision(c,s,82);c.row(137,"STATUS",s.presentation_explicit?s.presentation==HeldClock::Fault?"FAULT":s.presentation==HeldClock::Rejoining?"CORRECTING":s.presentation==HeldClock::Stale?"STALE":s.held?"HELD":s.time_valid?"RECEIVING":"WAITING":s.time_valid?(s.held?"HOLD":"TRACK"):"ACQUIRE",s.held?Amber:s.time_valid?Green:Amber);
  c.metric(154,"AGE S",s.age_s);c.metric(171,"P99 ns",s.p99,Cyan);c.metric(188,"MAX ns",s.maximum,Cyan);c.metric(205,"SAMPLES",s.samples);c.center(220,"OUTPUT UNQUALIFIED",1,Amber);
 }else{
  const char* titles[]={"CLOCK","DASHBOARD","HOLDOVER","DRIFT","BLE RX","QUALITY","SYSTEM","EXT PPS"};
  c.text(12,33,titles[std::min(unsigned(s.page),7u)],2,White);c.fill(12,54,111,1,Bezel);
  if(s.page==2){c.metric(65,"EST 1us MS",s.hold_ms);c.metric(87,"TRIALS",s.hold_trials);c.metric(109,"AGE S",s.age_s);c.metric(131,"P95 ns",s.p95,Cyan);c.metric(153,"P99 ns",s.p99,Cyan);c.center(184,"EMPIRICAL ESTIMATE",1,Amber);c.center(203,"PTS NOT QUALIFIED",1,Amber);}
  if(s.page==3){signed_metric(c,65,"RATE PPB",s.drift_mppb,s.fit);signed_metric(c,87,"TRIM PPB",s.trim_mppb,s.fit);signed_metric(c,109,"RES PPB",s.residual_mppb,s.fit);c.metric(131,"SIG PPB",s.rate_sigma);c.metric(153,"SPAN S",s.span_s);c.center(184,"SOFTWARE TIME MAP",1,Dim);c.center(203,"XTAL ACTUATOR NONE",1,Amber);}
  if(s.page==4){c.row(65,"LISTEN",s.ble_enabled?"ON":"OFF",s.ble_enabled?Green:Dim);c.row(83,"SCANNER",s.scan?"ACTIVE":"IDLE");c.metric(101,"GM PAGES",s.gm_pages);c.metric(119,"GM V8",s.gm_records);c.metric(137,"UTC RF",s.utc_physical);c.metric(155,"UTC MAP",s.utc_maps);c.metric(173,"REJECT",s.rx_rejected);c.metric(191,"DROPS",s.drops,s.drops?Red:White);c.center(215,"SS3 V8 R8 / RX ONLY",1,Amber);}
  if(s.page==5){c.metric(65,"P95 ns",s.p95,Cyan);c.metric(87,"P99 ns",s.p99,Cyan);c.metric(109,"MAX ns",s.maximum,Cyan);c.metric(131,"SAMPLES",s.samples);c.metric(153,"EXPIRED",s.expired);c.row(175,"LOCK","UNAVAILABLE",Dim);c.center(203,"INTERNAL CAUSAL",1,Dim);c.center(218,"UTC BOUND UNKNOWN",1,Amber);}
  if(s.page==6){char id[12];std::snprintf(id,sizeof(id),"%08lX",(unsigned long)s.root);c.row(65,"ROOT",s.root?id:"--");std::snprintf(id,sizeof(id),"%08lX",(unsigned long)s.provider);c.row(87,"PROVIDER",s.provider?id:"--");c.metric(109,"RADIO ERR",s.radio_error,s.radio_error?Red:White);c.metric(131,"CAP ERR",s.capture_error,s.capture_error?Red:White);c.metric(153,"HEAP",s.heap_internal);c.row(175,"CLOCK","20MHz");c.row(197,"PTS / GPS","OFF",Dim);c.center(218,"GM UNPROVISIONED",1,Amber);}
  if(s.page==7){c.row(65,"INPUT",s.pps_enabled?"GPIO6":"DISABLED",s.pps_enabled?Cyan:Dim);c.metric(87,"EDGES",s.pps_enabled?s.pps_edges:UINT32_MAX);c.metric(109,"DROPS",s.pps_enabled?s.pps_drops:UINT32_MAX);c.metric(131,"AGE MS",s.pps_age_ms);c.metric(153,"MAP ns",s.pps_bracket_ns);c.row(175,"MAPPING",s.pps_mapped?"BRACKETED":"UNKNOWN",Dim);c.center(203,"NO UTC FROM PULSE",1,Amber);c.center(218,"DIAGNOSTIC ONLY",1,Dim);}
 }
 footer(c,s);
}
// PToB's three timing stems and shared bus, from the project's brand mark.
// Render geometry directly at panel resolution; no image decoder or heap work.
static void splash(Canvas& c){
 constexpr uint16_t Slate=0x32ef,Paper=0xf7de,Soft=0xbe58;
 c.fill(0,0,Width,Height,Slate);
 for(unsigned stem=0;stem<3;++stem){
  const int x=40+int(stem)*27,top=49+int(stem)*12;
  c.fill(x-1,top,3,60-int(stem)*12,Paper);
 }
 c.fill(31,84,72,3,Paper);
 for(int x:{40,67,94}){for(int y=-9;y<=9;++y)for(int dx=-9;dx<=9;++dx){
   const int d=dx*dx+y*y;if(d<=81)c.pixel(x+dx,85+y,d>=42?Paper:Slate);
 }}
 // Preserve the mixed-case wordmark with its lower-case o.
 constexpr uint8_t lower_o[]{0x38,0x44,0x44,0x44,0x38};
 const uint8_t* glyphs[]{font::glyph('P'),font::glyph('T'),lower_o,font::glyph('B')};
 for(unsigned letter=0;letter<4;++letter)for(int col=0;col<5;++col)for(int row=0;row<7;++row)
  if(glyphs[letter][col]&(1u<<row))c.fill(33+int(letter)*18+col*3,122+row*3,3,3,Paper);
 c.center(157,"PRECISION TIME",1,Paper);c.center(170,"OVER BLUETOOTH",1,Paper);
 c.fill(55,197,25,1,Soft);c.center(210,"STARTING UP",1,Paper);
 c.center(228,"STICKS3",1,Soft);
}
void render_splash(uint8_t frame[FrameBytes],RenderAudit* audit){
 if(audit)*audit={};
 Canvas c{frame,audit};splash(c);
}
void render_splash_strip(uint8_t strip[StripBytes],unsigned first){
 if(first>=Height||first%StripRows)return;
 Canvas c{strip,nullptr,int(first),StripRows};splash(c);
}
void render(uint8_t frame[FrameBytes],const View& s,RenderAudit* audit) {
 if(audit)*audit={};
 Canvas c{frame,audit};paint(c,s);
}
void render_strip(uint8_t strip[StripBytes],unsigned first,const View& s) {
 if(first>=Height || first%StripRows)return;
 Canvas c{strip,nullptr,int(first),StripRows};paint(c,s);
}
}
