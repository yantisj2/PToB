// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ui.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace corese {
void duration_text(char* out,size_t size,uint64_t seconds){
 const auto d=seconds/86400,h=seconds/3600%24,m=seconds/60%60,s=seconds%60;
 if(d)std::snprintf(out,size,"%lluD %lluH %lluM",(unsigned long long)d,(unsigned long long)h,(unsigned long long)m);
 else if(h)std::snprintf(out,size,"%lluH %lluM %lluS",(unsigned long long)h,(unsigned long long)m,(unsigned long long)s);
 else if(m)std::snprintf(out,size,"%lluM %lluS",(unsigned long long)m,(unsigned long long)s);
 else std::snprintf(out,size,"%lluS",(unsigned long long)s);
}
bool fresh(const Model& m,uint64_t now){return now>=m.sampled_us&&now-m.sampled_us<SnapshotMaxUs;}
bool retained_time(Model& m,uint64_t observed,uint64_t snapshot,uint64_t utc,uint64_t now){
 if(!observed||!utc||now<observed||snapshot<observed||observed>UINT64_MAX-RetainedHoldUs||
    now-observed>=RetainedHoldUs||snapshot-observed>=RetainedHoldUs)return false;
 m.sampled_us=now;m.source=Source::Retained;m.state=State::Hold;m.utc_valid=true;
 m.utc_at_us=snapshot;m.utc_us=utc;m.until_us=observed+RetainedHoldUs;
 m.precision_valid=m.offset_valid=false;m.precision_until_us=0;m.rms_ns=UINT32_MAX;
 m.root=m.boot=0;m.source_epoch=0;return true;
}
bool time(const Model& m,uint64_t now,uint64_t& utc){
 utc=0;
 if(!fresh(m,now)||!m.utc_valid||m.source==Source::None||m.state==State::Fault||m.state==State::Search||(m.state==State::Hold&&m.source!=Source::Retained)||(m.source==Source::Retained&&m.state!=State::Hold)||
    now<m.utc_at_us||now>=m.until_us||now-m.utc_at_us>UINT64_MAX-m.utc_us)return false;
 utc=m.utc_us+now-m.utc_at_us;return true;
}
bool precision(const Model& m,uint64_t now){
 uint64_t utc;return time(m,now,utc)&&m.precision_valid&&m.state==State::Locked&&
  m.source!=Source::Rc9&&m.source!=Source::Retained&&m.rms_ns!=UINT32_MAX&&now<m.precision_until_us;
}
bool offset(const Model& m,uint64_t now){
 return precision(m,now)&&m.offset_valid&&m.root&&m.boot&&m.source_epoch&&
  m.offset_root==m.root&&m.offset_boot==m.boot&&m.offset_epoch==m.source_epoch&&
  now>=m.offset_sampled_us&&now-m.offset_sampled_us<SnapshotMaxUs&&now<m.offset_until_us;
}
bool shadow_current(const Model& m,uint64_t now){
 uint64_t utc;const auto& h=m.holdover;
 return time(m,now,utc)&&m.source!=Source::Rc9&&m.source!=Source::Retained&&h.last20&&
  h.witness_age_us<5000000&&now-m.sampled_us<5000000-h.witness_age_us;
}
const char* clock_zone(const Model& m,uint64_t now,int64_t& shown){
 uint64_t utc=0;shown=0;if(!time(m,now,utc)||utc>uint64_t(INT64_MAX))return "UTC";
 shown=int64_t(utc);
 if(m.source==Source::Rc9||m.source==Source::Retained)return "UTC";
 return v12::clock_zone(m.tz,m.tz_until_us,now,int64_t(utc),shown);
}
void hold_duration(char* out,size_t size,const HoldoverView& h,bool current){
 if(!current||!h.ready){std::snprintf(out,size,"----");return;}
 std::snprintf(out,size,"%lu.%02lu",(unsigned long)(h.duration_ms/1000),(unsigned long)(h.duration_ms%1000/10));
}
const char* hold_error(char* out,size_t size,uint64_t ns,bool known){
 if(!known||ns==UINT64_MAX){std::snprintf(out,size,"----");return "ns";}
 const uint64_t scale=ns<1000?1:ns<1000000?1000:ns<1000000000?1000000:1000000000;
 const char* unit=scale==1?"ns":scale==1000?"us":scale==1000000?"ms":"s";
 if(ns/scale>999)std::snprintf(out,size,"HI");
 else if(scale==1)std::snprintf(out,size,"%llu",(unsigned long long)ns);
 else std::snprintf(out,size,"%llu.%02llu",(unsigned long long)(ns/scale),(unsigned long long)(ns%scale*100/scale));
 return unit;
}
bool source_pulse(const Model& m,uint64_t now){
 uint64_t utc;return m.source!=Source::Retained&&time(m,now,utc)&&utc%1000000<SourcePulseUs;
}
void DisplayMemory::retain(Model& m){
 if(m.mesh.path.known)path_=m.mesh.path;else m.mesh.path=path_;
 if(m.mesh.link.known)link_=m.mesh.link;else m.mesh.link=link_;
 if(m.mesh.capabilities.known)capabilities_=m.mesh.capabilities;else m.mesh.capabilities=capabilities_;
 if(m.mesh.reference.reported)reference_=m.mesh.reference;else m.mesh.reference=reference_;
 if(precision(m,m.sampled_us)){observed_.rms_ns=m.rms_ns;observed_.rms_at=m.sampled_us;}
 if(offset(m,m.sampled_us)){observed_.offset_ns=m.offset_ns;observed_.offset_at=m.sampled_us;}
 m.observed=observed_;
}
const char* status(const Model& m,uint64_t now){
 if(!fresh(m,now))return "STALE";
 if(m.state==State::Fault)return "FAULT";
 if(m.state==State::Hold){uint64_t utc;return time(m,now,utc)?"HOLD":"NO SOURCE";}
 uint64_t utc;
 if(time(m,now,utc)){if(m.source==Source::Rc9)return "RC9 UTC";return precision(m,now)?"LOCK EST":"ACQUIRE";}
 return m.state==State::Search?"SEARCH":"NO SOURCE";
}
const char* source_name(const Model& m,uint64_t now){
 uint64_t utc;if(!time(m,now,utc))return "NONE";
 switch(m.source){case Source::Grandmaster:return "BLE";case Source::Relay1:return "RC1";
 case Source::Relay2:return "RC2";case Source::Rc9:return "RC9";case Source::Retained:return "HOLD";case Source::None:return "NONE";}return "NONE";
}
namespace {
constexpr uint16_t rgb(unsigned r,unsigned g,unsigned b){return uint16_t((r>>3)<<11|(g>>2)<<5|(b>>3));}
constexpr auto Panel=rgb(184,190,178),Light=rgb(229,233,214),Edge=rgb(91,102,92),Ink=rgb(28,40,34);
constexpr auto Well=rgb(15,27,23),Rim=rgb(51,66,54),Green=rgb(182,239,176),Dim=rgb(107,145,115);
constexpr auto Ghost=rgb(28,47,34),Amber=rgb(245,196,105),Red=rgb(248,133,114),Key=rgb(214,217,196);
#include "../../n30/main/font.inc"
struct Canvas {
 uint16_t* f;
 void pixel(int x,int y,uint16_t c){if(x>=0&&x<Width&&y>=0&&y<Height)f[y*Width+x]=c;}
 void fill(int x,int y,int w,int h,uint16_t c){
  for(int yy=std::max(y,0);yy<std::min(y+h,Height);++yy)
   for(int xx=std::max(x,0);xx<std::min(x+w,Width);++xx)f[yy*Width+xx]=c;
 }
 void box(int x,int y,int w,int h,uint16_t c){fill(x,y,w,1,c);fill(x,y+h-1,w,1,c);fill(x,y,1,h,c);fill(x+w-1,y,1,h,c);}
 void line(int x,int y,int xx,int yy,uint16_t c){
  int dx=std::abs(xx-x),sx=x<xx?1:-1,dy=-std::abs(yy-y),sy=y<yy?1:-1,e=dx+dy;
  for(;;){pixel(x,y,c);if(x==xx&&y==yy)break;const int q=2*e;if(q>=dy){e+=dy;x+=sx;}if(q<=dx){e+=dx;y+=sy;}}
 }
 void text(int x,int y,const char* s,uint16_t c=Ink,int scale=1,int max_x=Width){
  static const uint8_t left_arrow[5]={0,8,0x14,0x22,0};
  for(;*s&&x+5*scale<=max_x;++s,x+=6*scale){const auto g=*s=='<'?left_arrow:glyph(*s);
   for(int a=0;a<5;++a)for(int b=0;b<7;++b)if(g[a]&(1<<b))fill(x+a*scale,y+b*scale,scale,scale,c);}
 }
 void center(int x,int y,int w,const char* s,uint16_t c=Ink,int scale=1){text(x+(w-(int(std::strlen(s))*6-1)*scale)/2,y,s,c,scale,x+w);}
 void well(int x,int y,int w,int h){fill(x,y,w,h,Well);box(x,y,w,h,Edge);fill(x+1,y+1,w-2,1,Ink);fill(x,y+h,w,1,Light);}
 void screw(int x,int y){
  fill(x-2,y-2,5,5,Edge);fill(x-1,y-2,3,5,Light);fill(x-2,y-1,5,3,Light);line(x-1,y+1,x+1,y-1,Edge);
 }
 void digit(int x,int y,int w,int h,char digit,uint16_t color=Green){
  constexpr uint8_t mask[]={0x3f,6,0x5b,0x4f,0x66,0x6d,0x7d,7,0x7f,0x6f};
  const unsigned bits=digit>='0'&&digit<='9'?mask[digit-'0']:digit=='-'?64:0;
  const int t=h>=30?3:2,mid=h/2;
  for(unsigned i=0;i<7;++i){const auto c=bits&(1<<i)?color:Ghost;
   if(i==0||i==3||i==6){const int yy=i==0?y:i==3?y+h-t:y+mid-t/2;
    fill(x+t,yy,w-2*t,t,c);pixel(x+t-1,yy+t/2,c);pixel(x+w-t,yy+t/2,c);
   }else{const bool right=i==1||i==2,low=i==2||i==4;
    const int xx=right?x+w-t:x,yy=low?y+mid+t/2:y+t+1;
    fill(xx,yy,t,mid-t-2,c);pixel(xx+t/2,yy-1,c);pixel(xx+t/2,yy+mid-t-2,c);
   }
  }
 }
 void digital(int x,int y,const char* s,int w,int h,uint16_t color=Green){
  for(;*s;++s){if(*s==':'){fill(x+2,y+h/3,3,3,color);fill(x+2,y+2*h/3,3,3,color);x+=9;}
   else if(*s=='.'){fill(x+1,y+h-3,3,3,color);x+=7;}
   else if(*s=='+'){fill(x+2,y+h/2-1,w-4,2,color);fill(x+w/2-1,y+h/2-5,2,10,color);x+=w+4;}
   else{digit(x,y,w,h,*s,color);x+=w+4;}}
 }
 void lamp(int x,int y,int w,const char* label,bool active,uint16_t c=Green){
  fill(x,y,w,15,Well);box(x,y,w,15,Rim);center(x,y+4,w,label,active?c:Dim);
 }
 void key(int x,int y,int w,int h,const char* label,bool down=false,const char* sub=nullptr){
  fill(x,y,w,h,down?Rim:Key);box(x,y,w,h,Edge);
  fill(x+1,y+1,w-2,1,down?Ink:Light);fill(x+1,y+1,1,h-2,down?Ink:Light);
  fill(x+1,y+h-2,w-2,1,down?Light:Edge);fill(x+w-2,y+1,1,h-2,down?Light:Edge);
  if(sub){center(x,y+8,w,label,down?Light:Ink,2);center(x,y+29,w,sub,down?Light:Ink);}
  else center(x,y+(h-7)/2,w,label,down?Light:Ink);
 }
};
void clock_string(const Model& m,uint64_t now,char (&out)[12]){
 uint64_t utc;if(!time(m,now,utc)){std::snprintf(out,sizeof out,"--:--:--");return;}
 int64_t shown=0;clock_zone(m,now,shown);
 const auto seconds=unsigned((shown/1000000%86400+86400)%86400);
 std::snprintf(out,sizeof out,"%02u:%02u:%02u",seconds/3600,seconds/60%60,seconds%60);
}
void value(char* out,size_t size,int64_t ns,bool valid,bool sign){
 if(!valid){std::snprintf(out,size,"----");return;}
 // Bounds before conversion prevent INT64_MIN overflow and runaway labels.
 const auto a=ns<0?uint64_t(-(ns+1))+1:uint64_t(ns);
 if(a>99999){std::snprintf(out,size,sign?(ns<0?"-HI":"+HI"):"HI");return;}
 std::snprintf(out,size,sign?"%+lld":"%lld",static_cast<long long>(ns));
}
void shell(Canvas& c,const Model& m,const char* title,uint64_t now,const char* subtitle="PRECISION TIME INSTRUMENT"){
 c.fill(0,0,Width,Height,Panel);c.box(0,0,Width,Height,Edge);c.box(1,1,Width-2,Height-2,Light);
 c.screw(6,6);c.screw(313,6);c.screw(6,233);c.screw(313,233);
 c.text(14,10,"PToB",Ink,2);c.text(76,10,title);c.text(76,22,subtitle);
 c.fill(12,34,296,1,Edge);c.fill(12,35,296,1,Light);
 uint64_t utc;const bool valid=time(m,now,utc);
 c.lamp(241,8,65,source_name(m,now),true,valid&&m.source!=Source::Retained?(source_pulse(m,now)?Green:Dim):Amber);
 if(m.synthetic)c.text(14,229,"SYNTHETIC PREVIEW",Ink);
 else c.text(14,229,"CORE S3 SE",Ink);
 char parent[25];std::snprintf(parent,sizeof parent,"%.21s",valid?m.parent:"NO SOURCE");
 c.text(308-(int(std::strlen(parent))*6-1),229,parent,Ink);
}
void rail(Canvas& c,const Model& m,uint64_t now,int y){
 char label[52];const bool live=fresh(m,now);uint64_t utc;const bool valid=time(m,now,utc);
 c.well(12,y,296,21);
 if(valid)std::snprintf(label,sizeof label,"%s  %.12s",source_name(m,now),m.parent);
 else std::snprintf(label,sizeof label,"NO CURRENT SOURCE");
 c.text(20,y+7,label,valid&&m.source!=Source::Retained?Green:Amber,1,216);
 c.text(224,y+7,live?status(m,now):"STALE",m.state==State::Fault?Red:precision(m,now)?Green:Amber);
}
void navigation(Canvas& c,const View& v){
 c.key(12,181,64,40,"HOME",v.pressed==Action::Home);
 c.key(82,181,132,40,v.screen==Screen::Clock?"DIAGNOSTICS":"PToB CLOCK",
  v.pressed==(v.screen==Screen::Clock?Action::OpenDiagnostics:Action::OpenClock));
 c.key(220,181,88,40,"FACE >",v.pressed==Action::NextFace);
}
void launcher(Canvas& c,const Model& m,const View& v,uint64_t now){
 shell(c,m,"INSTRUMENT SELECT",now);
 c.well(12,44,203,67);int64_t shown=0;const auto* zone=clock_zone(m,now,shown);
 char zone_title[40];std::snprintf(zone_title,sizeof zone_title,"CLOCK / %s",zone);c.text(21,51,zone_title,Dim);
 char t[12];clock_string(m,now,t);uint64_t utc;const bool valid=time(m,now,utc);
 c.digital(21,66,t,23,32,valid?Green:Amber);
 c.lamp(225,44,83,"SOURCE",valid);c.lamp(225,67,83,status(m,now),true,precision(m,now)?Green:Amber);
 c.lamp(225,90,83,"RX ONLY",true,Light);
 c.key(12,122,144,49,"PToB",v.pressed==Action::OpenClock,"TIME / PRECISION");
 c.key(164,122,144,49,"DIAG",v.pressed==Action::OpenDiagnostics,"SOURCE / SYSTEM");
 c.key(12,180,296,41,"",v.pressed==Action::NextFace);
 c.text(22,189,"WATCH FACE");c.text(22,203,v.face==Face::Instrument?"INSTRUMENT 01":"TIME REGISTER 02");
 c.text(238,197,"NEXT >");
 // The route is textual as well as colored; it never asserts a live link.
 if(valid){char s[32];std::snprintf(s,sizeof s,"%.8s / %s",m.parent,source_name(m,now));c.text(22,113,s);}
}
void clock(Canvas& c,const Model& m,const View& v,uint64_t now){
 shell(c,m,"TIME + PRECISION",now);rail(c,m,now,42);
 c.well(12,69,296,42);char t[12];clock_string(m,now,t);uint64_t utc;const bool valid=time(m,now,utc);
 int64_t shown=0;const auto* zone=clock_zone(m,now,shown);c.text(20,77,zone,Dim);c.digital(52,74,t,28,29,valid&&m.source!=Source::Retained?Green:Amber);
 char context[48];
 if(valid&&m.source==Source::Retained)std::snprintf(context,sizeof context,"HOLD ESTIMATE / PRECISION UNKNOWN");
 else if(valid)std::snprintf(context,sizeof context,"ROOT %.8s / %s",m.root_name,m.source==Source::Rc9?"PRECISION UNKNOWN":"INTERNAL QUALITY");
 else std::snprintf(context,sizeof context,"AWAITING LIVE SOURCE / PRECISION WITHHELD");
 c.text(20,113,context,Ink);
 c.well(12,124,142,39);c.well(162,124,146,39);
 const bool rms_live=precision(m,now),hold_live=shadow_current(m,now)&&m.holdover.ready;
 c.text(20,129,rms_live||!m.observed.rms_at?"CAUSAL RMS":"LAST RMS",Dim);c.text(170,129,m.holdover.lower_bound?"P99 <1us HOLD MIN":"P99 <1us HOLD",Dim);
 char a[20],b[20];value(a,sizeof a,rms_live?m.rms_ns:m.observed.rms_ns,rms_live||m.observed.rms_at,false);hold_duration(b,sizeof b,m.holdover,hold_live);
 c.digital(22,140,a,12,19,rms_live?Green:Amber);c.digital(172,140,b,12,19,hold_live?Green:Amber);
 c.text(133,150,"ns",Green);c.text(294,150,"s",hold_live?Green:Amber);
 if(v.face==Face::UtcRegister){
  c.fill(12,69,296,94,Panel);
  const char* labels[]={"HOURS","MINUTES","SECONDS"};
  for(int i=0;i<3;++i){const int x=12+i*101;c.well(x,69,94,62);c.center(x,76,94,labels[i],Dim);
   char digits[3]={t[i*3],t[i*3+1],0};c.digital(x+18,90,digits,25,32,valid?Green:Amber);}
  c.well(12,140,296,23);
  // This compact face has no historical-value label: show only currently
  // permitted precision here. Detailed views retain explicitly marked history.
  value(a,sizeof a,m.rms_ns,rms_live,false);hold_duration(b,sizeof b,m.holdover,hold_live);
  char s[48];std::snprintf(s,sizeof s,"RMS %.8s ns",a);c.center(12,148,142,s,rms_live?Green:Amber);
  std::snprintf(s,sizeof s,"P99 %s%.8s s",m.holdover.lower_bound&&hold_live?"MIN ":"",b);c.center(162,148,146,s,hold_live?Green:Amber);
  c.center(12,133,296,zone,Dim);
 }
 char hardware[52];const bool counter=m.xtal_hz&&m.counter_hz;
 if(counter)std::snprintf(hardware,sizeof hardware,"XTAL %luMHz / COUNT %luMHz / %.1fns%s",static_cast<unsigned long>(m.xtal_hz/1000000),static_cast<unsigned long>(m.counter_hz/1000000),1e9/double(m.counter_hz),m.counter_healthy?"":" FAULT");
 else std::snprintf(hardware,sizeof hardware,"XTAL --MHz / COUNT --MHz / --ns");
 c.text(20,169,hardware,Ink);
 // Navigation has its own stable hit regions; metrics never act as controls.
 navigation(c,v);
}
void diagnostics(Canvas& c,const Model& m,const View& v,uint64_t now){
 const auto& r=m.mesh.roles;const auto& p=m.mesh.path;const auto& l=m.mesh.link;
 const bool sampled=fresh(m,now);
 const bool path=sampled&&m.utc_valid&&m.source!=Source::Rc9&&mesh::current(p,now)&&p.nodes[0].id==m.root&&p.nodes[0].boot==m.boot&&p.source_epoch==m.source_epoch;
 const bool path_known=p.known&&p.count>=1&&p.count<=3;
 const bool link=sampled&&mesh::current(l,{m.root,m.boot,m.source_epoch},now);
 static const char* pages[]={"MESH / ACTIVE + HOT","PATH / SOURCE LINEAGE","QUALITY / RECEIVED TIME","REFERENCE / GNSS + PTP","WIRE / EPOCH + INTEGRITY","SHADOW / HOLDOVER SERVO"};
 const auto page=unsigned(v.diagnostic)<unsigned(mesh::Page::Count)?unsigned(v.diagnostic):0;
 shell(c,m,"DIAGNOSTICS",now,pages[page]);
 char s[64];
 auto stamp=[&](int x,int y,uint64_t since,bool known){
  char age[16]="--:--:--";
  const auto at=now;
  if(known&&at>=since){const auto t=(at-since)/1000000;
   if(t>=86400)duration_text(age,sizeof age,t);
   else std::snprintf(age,sizeof age,"%02u:%02u:%02u",unsigned(t/3600),unsigned(t/60%60),unsigned(t%60));}
  c.text(x,y,age,known?Green:Amber);
 };
 auto framed=[&](int x,int y,int w,int h,const char* label){
  c.well(x,y,w,h);c.box(x+2,y+2,w-4,h-4,Rim);
  const int width=std::min(w-16,int(std::strlen(label))*6+10);
  c.fill(x+8,y-3,width,10,Panel);
  c.text(x+13,y-2,label,Ink,1,x+w-8);
 };
 auto note=[&](const char* text,uint16_t color=Ink){c.center(12,171,296,text,color);};
 switch(mesh::Page(page)){
 case mesh::Page::Mesh:{
  c.well(12,42,296,17);
  if(r.known)std::snprintf(s,sizeof s,"%s %s / AGE %llus",r.wire_version>=3?"GM7":"GS",mesh::live(r,now)&&!r.withdrawn?"CURRENT REPORT":"LAST REPORT",static_cast<unsigned long long>(now>=r.received_us?(now-r.received_us)/1000000:0));
  else std::snprintf(s,sizeof s,"LISTENING FOR AUTHENTICATED MESH STATUS");
  c.text(20,47,s,r.known?Green:Amber);
  framed(12,68,296,44,r.wire_version>=3?"ACTIVE / REPORTED":"PRIMARY / REPORTED");
  c.box(183,74,112,31,Rim);
  c.text(23,81,r.known?r.primary_name:"--",r.known?Green:Amber,r.known&&std::strlen(r.primary_name)>12?1:2,170);
  c.text(191,78,"UNCHANGED",Dim);stamp(191,95,r.primary_since_us,r.known);
  c.text(23,101,r.withdrawn?"WITHDRAWN":r.wire_version>=3&&(r.flags&8)?"ACTIVE YIELD":r.known?"LAST ROLE REPORT":"NO REPORT YET",r.withdrawn?Amber:r.known?Dim:Amber);
  const bool hot=r.known&&r.hot;
  const bool nominated=mesh::hot_nomination_current(r,now);
  const bool ready=hot&&(r.wire_version!=3||(r.hot_confirmed&&now<r.hot_until_us));
  framed(12,123,296,41,r.wire_version!=3?"HOT / STANDBY":nominated?"HOT NOMINEE / CURRENT":hot?"HOT NOMINEE / LAST SEEN":"HOT NOMINEE");
  c.box(183,129,112,31,Rim);
  c.text(23,135,r.known?(r.hot?r.hot_name:"NONE"):"--",(r.wire_version>=3?nominated:ready)?Green:Amber,r.hot&&std::strlen(r.hot_name)>12?1:2,170);
  c.text(191,133,"UNCHANGED",Dim);stamp(191,149,r.hot_since_us,hot);
  if(hot&&r.wire_version>=3&&r.hot_report_known&&now>=r.hot_received_us){
   static const char* roles[]={"STANDBY","ACTIVE","PREPARING","HOT","RC"};
   std::snprintf(s,sizeof s,"SELF %s / AGE %llus",r.hot_role<5?roles[r.hot_role]:"UNKNOWN",(unsigned long long)((now-r.hot_received_us)/1000000));
  }else std::snprintf(s,sizeof s,"%s",hot?(r.wire_version>=3?"SELF REPORT PENDING":"LAST ROLE REPORT"):r.known?"NO HOT REPORTED":"STATUS UNAVAILABLE");
  c.text(23,153,s,ready?Dim:Amber,1,180);
  if(r.known&&r.wire_version>=3){std::snprintf(s,sizeof s,"TERM %llu / ANN %lu STATUS %lu",(unsigned long long)r.term,(unsigned long)r.announcements,(unsigned long)r.statuses);note(s);}
  else if(r.known){unsigned members=0;for(unsigned i=0;i<4;++i)members+=(r.members>>i)&1;
   std::snprintf(s,sizeof s,"MEMBERS %u / COORD %.12s",members,r.coordinator_name);
  note(s);
  }else note("ROLE CHANGES RESET / REPORT GAPS DO NOT");
  break;
 }
 case mesh::Page::Path:{
  framed(12,47,296,36,"ORIGINAL ROOT");
  c.text(23,61,path_known?p.nodes[0].name:"--",path?Green:Amber,2,168);
  if(path_known)std::snprintf(s,sizeof s,"EPOCH %lu",static_cast<unsigned long>(p.source_epoch));else std::snprintf(s,sizeof s,"EPOCH --");
  c.text(188,64,s,path?Green:Amber);
  c.line(160,84,160,91,Ink);c.line(157,88,160,91,Ink);c.line(163,88,160,91,Ink);
  framed(12,96,296,35,"PARENT / SELECTED");
  const auto n=path_known?unsigned(p.count-1):0;
  c.text(23,108,path_known?p.nodes[n].name:"--",path?Green:Amber,2,168);
  if(path_known)std::snprintf(s,sizeof s,n==1?"%u RELAY HOP":"%u RELAY HOPS",n);else std::snprintf(s,sizeof s,"PATH UNKNOWN");
  c.text(188,112,s,path?Green:Amber);
  c.well(12,140,296,23);
  if(path_known&&p.count==3)std::snprintf(s,sizeof s,"VIA %.12s / TRAIN %lu",p.nodes[1].name,static_cast<unsigned long>(p.nodes[n].train));
  else if(path_known)std::snprintf(s,sizeof s,"TRAIN %lu / RX EPOCH %lu",static_cast<unsigned long>(p.nodes[n].train),static_cast<unsigned long>(p.nodes[n].epoch));
  else std::snprintf(s,sizeof s,m.source==Source::Rc9?"RC9 DOES NOT EXPORT AN RC7 TIMING PATH":"AWAITING AUTHENTICATED CURRENT PATH");
  c.center(12,148,296,s,path?Green:Amber);
  if(path)note("CURRENT ADMITTED PATH / CLIENT ONLY");
  else if(path_known){std::snprintf(s,sizeof s,"LAST PATH / AGE %llus / STALE",static_cast<unsigned long long>(now>=p.sampled_us?(now-p.sampled_us)/1000000:0));note(s,Amber);}
  else note("NO ADMITTED TIMING PATH / WAITING FOR RX",Amber);
  break;
 }
 case mesh::Page::Quality:{
  const bool rms_live=precision(m,now),offset_live=offset(m,now);
  framed(12,47,142,48,rms_live||!m.observed.rms_at?"LOCAL CAUSAL RMS":"LAST CAUSAL RMS");framed(162,47,146,48,offset_live||!m.observed.offset_at?"LOCAL REL OFFSET":"LAST REL OFFSET");
  char a[20],b[20];value(a,sizeof a,rms_live?m.rms_ns:m.observed.rms_ns,rms_live||m.observed.rms_at,false);value(b,sizeof b,offset_live?m.offset_ns:m.observed.offset_ns,offset_live||m.observed.offset_at,true);
  c.digital(22,64,a,12,22,rms_live?Green:Amber);c.digital(172,64,b,12,22,offset_live?Green:Amber);
  c.text(133,79,"ns",Green);
  c.text(287,79,"ns",Green);
  c.well(12,104,296,59);
  const uint64_t total=uint64_t(l.originals)+l.missed;
  if(l.known&&total)std::snprintf(s,sizeof s,"RX %lu / MISS %lu / MATCH %lu",static_cast<unsigned long>(l.originals),static_cast<unsigned long>(l.missed),static_cast<unsigned long>(l.matched));
  else if(l.known)std::snprintf(s,sizeof s,"PAIRS %lu / REFUSED %lu",static_cast<unsigned long>(l.matched),static_cast<unsigned long>(l.rejected));
  else std::snprintf(s,sizeof s,"RX -- / MISS -- / MATCH --");
  c.text(20,111,s,link?Green:Amber);
  if(l.known&&total)std::snprintf(s,sizeof s,"DELIVERY %llu.%llu%% / N %llu",static_cast<unsigned long long>(uint64_t(l.originals)*1000/total/10),static_cast<unsigned long long>(uint64_t(l.originals)*1000/total%10),static_cast<unsigned long long>(total));
  else std::snprintf(s,sizeof s,"DELIVERY -- / NO EVENT DENOMINATOR");
  c.text(20,127,s,link&&total?Green:Amber);
  if(l.known&&l.source_age_us!=UINT32_MAX){const auto age=uint64_t(l.source_age_us)+(now>=l.sampled_us?now-l.sampled_us:0);const auto ttl=now<l.until_us?l.until_us-now:0;std::snprintf(s,sizeof s,"SOURCE AGE %llums / TTL %llums",static_cast<unsigned long long>(age/1000),static_cast<unsigned long long>(ttl/1000));}
  else std::snprintf(s,sizeof s,"SOURCE AGE -- / TTL --");
  c.text(20,148,s,link?Green:Amber);
  if(link&&l.upstream_rms_known&&m.source==Source::Rc9&&l.upstream_rms_ns!=UINT32_MAX){
   std::snprintf(s,sizeof s,"RC9 UPLINK %luns / N %u / UTC UNKNOWN",static_cast<unsigned long>(l.upstream_rms_ns),unsigned(l.upstream_samples));
  note(s);
  }else if(!rms_live&&m.observed.rms_at){std::snprintf(s,sizeof s,"LAST QUALITY / AGE %llus / STALE",static_cast<unsigned long long>(now>=m.observed.rms_at?(now-m.observed.rms_at)/1000000:0));note(s,Amber);}
  else {int64_t shown=0;std::snprintf(s,sizeof s,"UTC METRICS / CLOCK %s / BOUNDS UNKNOWN",clock_zone(m,now,shown));note(s);}
  break;
 }
 case mesh::Page::Reference:{
  const auto& q=m.mesh.reference;
  const bool empty=!m.root&&!m.boot&&!m.source_epoch;
  const bool same=q.reported&&q.scope.root==m.root&&q.scope.boot==m.boot&&q.scope.epoch==m.source_epoch;
  const bool available=sampled&&mesh::current(q,{m.root,m.boot,m.source_epoch},now);
  const bool retained=q.reported&&(same||empty)&&now>=q.received_us;
  const auto elapsed=retained?now-q.received_us:0;
  framed(12,47,296,24,retained?(empty?"LAST GM / GNSS":"ROOT / GNSS REPORTED"):"SELECTED ROOT");
  c.text(23,54,retained&&empty?q.root_name:m.root?m.root_name:"--",available?Green:Amber,2,180);
  if(retained)std::snprintf(s,sizeof s,"%s %llus",available?"AGE":"CACHED",(unsigned long long)(elapsed/1000000));
  else std::snprintf(s,sizeof s,"WAIT REPORT");
  c.text(206,58,s,available?Green:Amber,1,300);
  auto status_lamp=[&](int x,int w,const char* label,uint8_t bit){
   const bool known=retained&&(q.fields&bit),yes=known&&(q.values&bit);
   const auto color=!available||!known?Amber:yes?Green:Red;
   framed(x,82,w,26,label);
   // Opposed single-pixel bevels make these fixed annunciators, not controls.
   c.fill(x+4,89,1,14,Light);c.fill(x+w-5,89,1,14,Rim);
   c.center(x,91,w,known?(yes?"YES":"NO"):"--",color,2);
  };
  status_lamp(12,90,"FIX",mesh::Fix);status_lamp(111,90,"PPS",mesh::Pps);status_lamp(210,98,"QUAL",mesh::Qualified);
  framed(12,117,142,34,"SV USED");framed(162,117,146,34,"HDOP");
  char sv[8],hdop[10];const bool sv_known=retained&&(q.fields&mesh::Satellites);
  if(sv_known)std::snprintf(sv,sizeof sv,"%u",unsigned(q.sats));else std::snprintf(sv,sizeof sv,"--");
  mesh::reference_hdop(hdop,sizeof hdop,q.hdop_centi,retained&&(q.fields&mesh::Hdop));
  const int sv_width=int(std::strlen(sv))*30-4;
  c.digital(12+(142-sv_width)/2,124,sv,26,27,available&&sv_known?Green:Amber);
  c.digital(172,126,hdop,16,24,available&&(q.fields&mesh::Hdop)&&q.hdop_centi!=UINT16_MAX?Green:Amber);
  framed(12,159,296,18,"PTP / UTC ACCURACY UNKNOWN");
  mesh::reference_ptp(s,sizeof s,q,retained);c.text(20,168,s,available&&(q.fields&mesh::Ptp)?Green:Amber,1,304);
  break;
 }
 case mesh::Page::Wire:{
  if(r.wire_version>=3){
   framed(12,47,296,61,"EPOCH START / UTC");
   char birth[32],age[32]="--",uptime[32];mesh::epoch_start_text(birth,sizeof birth,r.birth_utc_us);
   c.text(23,60,birth,r.birth_utc_us?Green:Amber);
   uint64_t utc=0,elapsed=0;
   const bool same=r.root==m.root&&r.root_boot==m.boot&&r.epoch==m.source_epoch;
   const bool age_known=same&&m.source!=Source::Retained&&time(m,now,utc)&&mesh::epoch_elapsed(r,utc,elapsed);
   if(age_known)duration_text(age,sizeof age,elapsed/1000000);
   std::snprintf(s,sizeof s,"ELAPSED %.31s",age);c.text(23,76,s,age_known?Green:Amber);
   duration_text(uptime,sizeof uptime,now/1000000);
   std::snprintf(s,sizeof s,"LOCAL UPTIME %.29s",uptime);c.text(23,94,s,Dim);
   c.well(12,117,296,47);
   std::snprintf(s,sizeof s,"GM7 ANN %lu / STATUS %lu / ROLE %lu",(unsigned long)r.announcements,(unsigned long)r.statuses,(unsigned long)r.role_reports);c.text(20,124,s,Green);
   std::snprintf(s,sizeof s,"MAC %lu / REPLAY %lu / SCOPE %lu",(unsigned long)r.rejected_auth,(unsigned long)r.replayed,(unsigned long)r.rejected_scope);c.text(20,140,s,Green);
   std::snprintf(s,sizeof s,"TERM %llu / AGE %llus / WD %lu",(unsigned long long)r.term,(unsigned long long)(now>=r.received_us?(now-r.received_us)/1000000:0),(unsigned long)r.withdrawals);c.text(20,154,s,Green);
   note(age_known?"SIGNED BIRTH / CURRENT UTC ELAPSED":"ELAPSED NEEDS CURRENT MATCHED UTC");break;
  }
  framed(12,47,296,52,"ORIGINAL GM / TIMEBASE");
  std::snprintf(s,sizeof s,"%.16s / EPOCH %lu",r.primary_name,static_cast<unsigned long>(r.epoch));
  c.text(23,60,r.known?s:"GM -- / EPOCH --",r.known?Green:Amber);
  char uptime[32]="--",epoch[32]="--",age[32]="--";
  if(r.known&&now>=r.received_us){
   const auto elapsed=now-r.received_us;
   // Descriptive GM boot age is extrapolated from its authenticated monotonic
   // timestamp; no source epoch start timestamp exists in GS v1.
   if(r.created_us&&elapsed<=UINT64_MAX-r.created_us)duration_text(uptime,sizeof uptime,(r.created_us+elapsed)/1000000);
   if(now>=r.epoch_since_us)duration_text(epoch,sizeof epoch,(now-r.epoch_since_us)/1000000);
   duration_text(age,sizeof age,elapsed/1000000);
  }
  std::snprintf(s,sizeof s,"GM UPTIME EST %.26s",uptime);c.text(23,73,s,r.known?Green:Amber);
  std::snprintf(s,sizeof s,"EPOCH SEEN %.26s",epoch);c.text(23,86,s,r.known?Green:Amber);
  c.well(12,108,296,55);
  std::snprintf(s,sizeof s,"GS AGE %.20s / SEQ %llu",age,static_cast<unsigned long long>(r.sequence));
  c.text(20,115,r.known?s:"GS CACHE EMPTY / LISTENING",r.known?Green:Amber);
  std::snprintf(s,sizeof s,"OK %lu / MAC %lu / REPLAY %lu",static_cast<unsigned long>(r.accepted),static_cast<unsigned long>(r.rejected_auth),static_cast<unsigned long>(r.replayed));
  c.text(20,131,s,Green);
  std::snprintf(s,sizeof s,"SCOPE %lu / AGE %lu / CHANGES %lu",static_cast<unsigned long>(r.rejected_scope),static_cast<unsigned long>(r.rejected_age),static_cast<unsigned long>(r.changes));
  c.text(20,149,s,Green);
  note("EPOCH START NOT CARRIED / GS CACHE");break;
 }
 case mesh::Page::Shadow:{
  const auto& h=m.holdover;const bool current=shadow_current(m,now);
  framed(12,47,296,37,h.lower_bound?"P99 <1us HOLD / LOWER BOUND":"P99 <1us HOLD / SAMPLED");
  char duration[20];hold_duration(duration,sizeof duration,h,current);
  c.digital(23,58,duration,16,22,current&&h.ready?Green:Amber);c.text(145,73,"s",Dim);
  std::snprintf(s,sizeof s,"TRIALS %lu",(unsigned long)h.trials);c.text(193,57,s,Dim);
  std::snprintf(s,sizeof s,"BAD %lu / U %lu",(unsigned long)h.bad,(unsigned long)h.unknown);c.text(193,72,s,Dim);
  const uint64_t metrics[]={h.p95_ns,h.p99_ns,h.max_ns};const char* labels[]={"P95","P99","MAX"};
  for(unsigned i=0;i<3;++i){const int x=12+int(i)*101;char n[16],label[16];
   const bool known=current&&metrics[i]!=UINT64_MAX;const auto* unit=hold_error(n,sizeof n,metrics[i],known);
   std::snprintf(label,sizeof label,"%s %s",labels[i],unit);framed(x,96,i==2?94:93,35,label);
   if(!std::strcmp(n,"HI"))c.text(x+22,111,"HI",Amber,2,x+90);
   else c.digital(x+10,107,n,11,19,known?Green:Amber);
  }
  c.well(12,139,296,25);
  std::snprintf(s,sizeof s,"%s / N %lu/128 / AGE %llus",current?(h.holding?"FROZEN":"COMPLETE"):"WAIT BLE",(unsigned long)h.samples,(unsigned long long)(h.age20/20000000));c.text(20,143,s,current?Green:Amber);
  char peak[16];const auto* unit=hold_error(peak,sizeof peak,h.run_max_ns,current&&h.samples);
  std::snprintf(s,sizeof s,"GAPS %lu / RUN MAX %s%s",(unsigned long)h.gaps,peak,unit);c.text(20,155,s,current?Dim:Amber);
  note("SAMPLED BLE ERRORS / NO UTC BOUND");break;
 }
 case mesh::Page::Count:break;
 }
 c.key(12,181,64,40,"HOME",v.pressed==Action::Home);
 c.key(82,181,58,40,"< PREV",v.pressed==Action::PreviousDiagnostic);
 std::snprintf(s,sizeof s,"%u/6",page+1);c.center(144,198,36,s);
 c.key(184,181,58,40,"NEXT >",v.pressed==Action::NextDiagnostic);
 c.key(248,181,60,40,"PToB",v.pressed==Action::OpenClock);
}
}
void render(uint16_t* frame,const Model& m,const View& v,uint64_t now){
 if(!frame)return;
 Canvas c{frame};
 switch(v.screen){case Screen::Launcher:launcher(c,m,v,now);break;case Screen::Clock:clock(c,m,v,now);break;case Screen::Diagnostics:diagnostics(c,m,v,now);break;}
}
Action hit(const View& v,int x,int y){
 if(x<12||x>=308||y<0||y>=Height)return Action::None;
 if(v.screen==Screen::Launcher){
  if(y>=122&&y<171){if(x<156)return Action::OpenClock;if(x>=164)return Action::OpenDiagnostics;}
  if(y>=180&&y<221)return Action::NextFace;
 }else if(y>=181&&y<221){
  if(x<76)return Action::Home;
  if(v.screen==Screen::Diagnostics){
   if(x>=82&&x<140)return Action::PreviousDiagnostic;
   if(x>=184&&x<242)return Action::NextDiagnostic;
   if(x>=248)return Action::OpenClock;
   return Action::None;
  }
  if(x>=82&&x<214)return v.screen==Screen::Clock?Action::OpenDiagnostics:Action::OpenClock;
  if(x>=220)return Action::NextFace;
 }
 return Action::None;
}
void apply(View& v,Action action){
 v.pressed=Action::None;
 switch(action){case Action::Home:v.screen=Screen::Launcher;break;case Action::OpenClock:v.screen=Screen::Clock;break;
 case Action::OpenDiagnostics:v.screen=Screen::Diagnostics;break;
 case Action::NextFace:v.face=v.face==Face::Instrument?Face::UtcRegister:Face::Instrument;
  if(v.screen==Screen::Diagnostics)v.screen=Screen::Clock;
  break;
 case Action::PreviousDiagnostic:v.diagnostic=mesh::Page((unsigned(v.diagnostic)+unsigned(mesh::Page::Count)-1)%unsigned(mesh::Page::Count));break;
 case Action::NextDiagnostic:v.diagnostic=mesh::Page((unsigned(v.diagnostic)+1)%unsigned(mesh::Page::Count));break;
 case Action::None:break;}
}
Action Touch::update(View& v,bool down,int x,int y){
 Action result=Action::None;
 if(down&&!down_){armed_=hit(v,x,y);cancelled_=false;}
 if(down&&hit(v,x,y)!=armed_)cancelled_=true;
 if(!down&&down_){if(!cancelled_&&hit(v,x,y)==armed_)result=armed_;armed_=Action::None;}
 down_=down;v.pressed=down&&!cancelled_?armed_:Action::None;return result;
}
}
