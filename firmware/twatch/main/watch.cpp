// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "watch.hpp"
#include "font.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>
#include <cstdlib>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
namespace watch {
namespace {
constexpr uint16_t rgb(unsigned r,unsigned g,unsigned b){return uint16_t(((r>>3)<<11)|((g>>2)<<5)|(b>>3));}
constexpr auto Black=rgb(4,8,9),Face=rgb(17,26,27),Ink=rgb(222,237,215),Muted=rgb(152,177,162),Amber=rgb(241,184,89),Green=rgb(134,217,170),Rule=rgb(49,68,62),Dim=rgb(24,38,34);
struct Canvas {
 uint16_t* p;RenderCache* damage=nullptr;
 void pixel(int x,int y,uint16_t c){if(x>=0&&x<Width&&y>=0&&y<Height){p[y*Width+x]=c;if(damage)damage->mark(x,y);}}
 void rect(int x,int y,int w,int h,uint16_t c){for(int j=std::max(0,y);j<std::min(Height,y+h);++j)for(int i=std::max(0,x);i<std::min(Width,x+w);++i){p[j*Width+i]=c;if(damage)damage->mark(i,j);}}
 void blend(int x,int y,uint16_t c,unsigned a){if(!a||x<0||y<0||x>=Width||y>=Height)return;if(a==255){pixel(x,y,c);return;}auto d=p[y*Width+x];unsigned r=(((c>>11)*a+(d>>11)*(255-a))+127)/255,g=((((c>>5)&63)*a+((d>>5)&63)*(255-a))+127)/255,b=(((c&31)*a+(d&31)*(255-a))+127)/255;pixel(x,y,uint16_t((r<<11)|(g<<5)|b));}
 int text(int x,int y,const char* s,int size,uint16_t c){auto g=size==36?watchfont::glyphs36:size==26?watchfont::glyphs26:size==14?watchfont::glyphs14:watchfont::glyphs20;auto a=size==36?watchfont::pixels36:size==26?watchfont::pixels26:size==14?watchfont::pixels14:watchfont::pixels20;for(;*s;++s){unsigned k=unsigned(uint8_t(*s));if(k<32||k>126)k='?';auto v=g[k-32];for(int j=0;j<v.height;++j)for(int i=0;i<v.width;++i)blend(x+i+v.left,y+j+v.top,c,a[v.offset+j*v.width+i]);x+=v.advance;}return x;}
 void hseg(int x,int y,int w,int t,uint16_t c){for(int j=0;j<t;++j){int cut=std::abs(j-t/2);rect(x+cut,y+j,w-2*cut,1,c);}}
 void vseg(int x,int y,int h,int t,uint16_t c){for(int i=0;i<t;++i){int cut=std::abs(i-t/2);rect(x+i,y+cut,1,h-2*cut,c);}}
 void digit(int x,int y,int w,int h,int n,uint16_t on=Ink){static constexpr uint8_t bits[]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};unsigned b=n>=0&&n<10?bits[n]:0x40;int t=w/6,hh=h/2;auto c=[&](unsigned k){return b&(1u<<k)?on:Dim;};hseg(x+t/2,y,w-t,t,c(0));vseg(x+w-t,y+t/2,hh-t/2,t,c(1));vseg(x+w-t,y+hh+t/2,hh-t/2,t,c(2));hseg(x+t/2,y+h-t,w-t,t,c(3));vseg(x,y+hh+t/2,hh-t/2,t,c(4));vseg(x,y+t/2,hh-t/2,t,c(5));hseg(x+t/2,y+hh-t/2,w-t,t,c(6));}
 void line(int x,int y,int xx,int yy,uint16_t color){int dx=std::abs(xx-x),sx=x<xx?1:-1,dy=-std::abs(yy-y),sy=y<yy?1:-1,err=dx+dy;for(;;){pixel(x,y,color);if(x==xx&&y==yy)break;int e=2*err;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}}
 void frame(int x,int y,int w,int h,int cut,uint16_t color){line(x+cut,y,x+w-1-cut,y,color);line(x+w-1-cut,y,x+w-1,y+cut,color);line(x+w-1,y+cut,x+w-1,y+h-1-cut,color);line(x+w-1,y+h-1-cut,x+w-1-cut,y+h-1,color);line(x+w-1-cut,y+h-1,x+cut,y+h-1,color);line(x+cut,y+h-1,x,y+h-1-cut,color);line(x,y+h-1-cut,x,y+cut,color);line(x,y+cut,x+cut,y,color);}
 void chevron(int x,int y){for(int i=0;i<8;++i){rect(x+i,y+i,2,2,Muted);rect(x+i,y+14-i,2,2,Muted);}}
};
const char* source(Source s){switch(s){case Source::BLE:return "BLE";case Source::LOR:return "LOR";case Source::RC1:return "RC1";case Source::RC2:return "RC2";case Source::RC9:return "RC9";default:return "---";}}
constexpr double Pi=3.14159265358979323846;
struct Point {int x,y;};
Point polar(int x,int y,double r,double angle){const double a=angle*Pi/180;return {x+int(std::lround(std::sin(a)*r)),y-int(std::lround(std::cos(a)*r))};}
void circle(Canvas& c,int x,int y,int radius,uint16_t color,bool fill=false){for(int yy=-radius;yy<=radius;++yy){int xx=int(std::sqrt(double(radius*radius-yy*yy)));if(fill)c.rect(x-xx,y+yy,2*xx+1,1,color);else{c.pixel(x-xx,y+yy,color);c.pixel(x+xx,y+yy,color);}}if(!fill)for(int xx=-radius;xx<=radius;++xx){int yy=int(std::sqrt(double(radius*radius-xx*xx)));c.pixel(x+xx,y-yy,color);c.pixel(x+xx,y+yy,color);}}
void centered(Canvas& c,int x,int y,const char* text,int size,uint16_t color){const auto* glyph=size==36?watchfont::glyphs36:size==26?watchfont::glyphs26:size==14?watchfont::glyphs14:watchfont::glyphs20;int w=0;for(auto p=text;*p;++p)w+=glyph[unsigned(*p)-32].advance;c.text(x-w/2,y,text,size,color);}
void triangle(Canvas& c,Point a,Point b,Point d,uint16_t color){
 auto edge=[](Point p,Point q,int x,int y){return (x-p.x)*(q.y-p.y)-(y-p.y)*(q.x-p.x);};
 for(int y=std::min({a.y,b.y,d.y});y<=std::max({a.y,b.y,d.y});++y)
  for(int x=std::min({a.x,b.x,d.x});x<=std::max({a.x,b.x,d.x});++x){int e=edge(a,b,x,y),f=edge(b,d,x,y),g=edge(d,a,x,y);if((e>=0&&f>=0&&g>=0)||(e<=0&&f<=0&&g<=0))c.pixel(x,y,color);}
}
void needle(Canvas& c,int x,int y,double angle,int length,int width,uint16_t color){
 const auto tip=polar(x,y,length,angle),tail=polar(x,y,5,angle+180),shoulder=polar(x,y,length*.25,angle);
 if(!width){c.line(tail.x,tail.y,tip.x,tip.y,color);return;}
 const auto left=polar(shoulder.x,shoulder.y,width,angle-90),right=polar(shoulder.x,shoulder.y,width,angle+90);
 triangle(c,tail,left,tip,color);triangle(c,tail,tip,right,color);
}
void ticks(Canvas& c,int x,int y,int radius,int count,double start,double span,uint16_t color){for(int i=0;i<=count;++i){const auto a=polar(x,y,radius,start+span*i/count),b=polar(x,y,radius-(i%5==0?7:3),start+span*i/count);c.line(a.x,a.y,b.x,b.y,color);}}
uint16_t tone(uint16_t a,uint16_t b,double t){t=std::max(0.0,std::min(1.0,t));return uint16_t((int((a>>11)*(1-t)+(b>>11)*t)<<11)|(int(((a>>5)&63)*(1-t)+((b>>5)&63)*t)<<5)|int((a&31)*(1-t)+(b&31)*t));}
void metal_ring(Canvas& c,int cx,int cy,int outer,int inner,bool recessed){
 for(int y=-outer;y<=outer;++y)for(int x=-outer;x<=outer;++x){const int rr=x*x+y*y;if(rr>(outer+1)*(outer+1)||rr<(inner-1)*(inner-1))continue;double r=std::sqrt(double(rr));if(r>outer+.5||r<inner-.5)continue;
  double light=.5+(-x-y)/(std::max(1.0,r)*3.1);if(recessed)light=1-light;
  const auto col=tone(rgb(10,17,12),rgb(121,138,107),light);
  double edge=std::min(outer+.5-r,r-inner+.5);c.blend(cx+x,cy+y,col,unsigned(std::min(1.0,edge)*255));
 }
}
void well(Canvas& c,int x,int y,int radius){
 circle(c,x+1,y+2,radius+2,rgb(8,14,10),true);
 for(int yy=-radius;yy<=radius;++yy){int w=int(std::sqrt(double(radius*radius-yy*yy)));c.rect(x-w,y+yy,2*w+1,1,tone(rgb(13,23,16),rgb(37,50,34),double(yy+radius)/(2*radius)));}
 metal_ring(c,x,y,radius,radius-3,true);
 for(int r=radius-7;r>12;r-=4)circle(c,x,y,r,rgb(30,43,29));
}
void pilot(Canvas& c,int x,int y,double angle,int length,bool hour,uint16_t rim,uint16_t lume){
 auto shape=[&](int cx,int cy,int start,int shoulder,int end,int w,uint16_t col){
  const auto a=polar(cx,cy,start,angle),b=polar(cx,cy,shoulder,angle),tip=polar(cx,cy,end,angle);
  const auto al=polar(a.x,a.y,hour?3:2,angle-90),ar=polar(a.x,a.y,hour?3:2,angle+90),bl=polar(b.x,b.y,w,angle-90),br=polar(b.x,b.y,w,angle+90);
  triangle(c,al,ar,br,col);triangle(c,al,br,bl,col);triangle(c,bl,br,tip,col);
 };
 shape(x+2,y+3,-15,length-(hour?29:22),length,hour?11:6,rgb(6,13,8));
 shape(x,y,-15,length-(hour?29:22),length,hour?11:6,rim);
 shape(x,y,20,length-(hour?30:23),length-7,hour?8:3,lume);
}
void instrument_base(Canvas& c){ constexpr auto Case=rgb(26,38,29),Bevel=rgb(73,88,68),Dial=rgb(27,39,29),Lume=rgb(203,211,174),Etch=rgb(148,163,132),Recess=rgb(12,23,17);
 // A square case with clipped shoulders surrounds a circular mechanical dial.
 for(int y=27;y<478;++y){int inset=std::max(0,48-std::min(y-27,477-y));c.rect(22+inset,y,366-2*inset,1,Case);}
 c.frame(22,27,366,451,48,Bevel);c.frame(28,33,354,439,44,Etch);c.frame(32,37,346,431,42,Black);
 for(auto q:{Point{65,67},Point{345,67},Point{65,435},Point{345,435}}){circle(c,q.x,q.y,9,Recess,true);circle(c,q.x,q.y,9,Bevel);c.line(q.x-4,q.y+4,q.x+4,q.y-4,Etch);}
 centered(c,205,41,"PToB",26,Lume);
 circle(c,205,247,177,Recess,true);metal_ring(c,205,247,176,169,false);
 for(int y=-168;y<=168;++y)for(int x=-168;x<=168;++x){const double rr=double(x*x+y*y)/(168*168);if(rr<=1)c.pixel(205+x,247+y,tone(rgb(49,65,43),Dial,std::min(1.0,.25+rr*.55+(x+y+336)/1500.0)));}
 metal_ring(c,205,247,168,162,true);circle(c,205,247,160,Recess);
 for(int i=0;i<60;++i){if(i%15==0)continue;auto a=polar(205,247,160,i*6),b=polar(205,247,i%5==0?153:157,i*6);c.line(a.x,a.y,b.x,b.y,Etch);}
 for(int i=0;i<12;++i){if(i%3==0)continue;auto a=polar(205,247,154,i*30),b=polar(205,247,143,i*30);for(int k=-1;k<=1;++k)c.line(a.x+k,a.y,b.x+k,b.y,Lume);}
 centered(c,205,85,"12",26,Lume);centered(c,354,229,"3",26,Lume);centered(c,205,382,"6",26,Lume);centered(c,56,229,"9",26,Lume);
 // Calendar bridge: independent source drum, phase well and receiver jewel.
 // All live contents are painted after the immutable background is copied.
 centered(c,205,113,"PLL",14,Etch);
 well(c,205,154,24);
 for(int x:{136,234}){
  c.rect(x,142,41,25,Recess);c.frame(x,142,41,25,3,Bevel);
  c.line(x+3,166,x+37,166,Etch);
 }
 centered(c,156,174,"RADIO",14,Etch);
 centered(c,254,174,"LORA",14,Etch);
 for(auto q:{Point{116,249},Point{294,249}}){well(c,q.x,q.y,48);ticks(c,q.x,q.y,41,30,-135,270,Etch);}
 centered(c,116,194,"SYNC RMS",14,Etch);centered(c,294,194,"P99 HOLD",14,Etch);
 const char* rmslabels[]={"0ns","2.5","5","7.5","10us"};for(int i=0;i<5;++i){auto q=polar(116,249,29,-135+i*67.5);centered(c,q.x,q.y-7,rmslabels[i],14,Lume);}
 const char* holdlabels[]={"<=1","15","30","45",">=60"};for(int i=0;i<5;++i){auto q=polar(294,249,31,-135+i*67.5);centered(c,q.x,q.y-7,holdlabels[i],14,Lume);}
 well(c,205,337,43);ticks(c,205,342,34,20,-110,220,Etch);
 centered(c,169,343,"E",20,Lume);centered(c,241,343,"F",20,Lume);centered(c,205,365,"RESERVE",14,Etch);

}
const uint16_t* instrument_background(){
 #ifdef ESP_PLATFORM
 static auto* pixels=static_cast<uint16_t*>(heap_caps_malloc(Width*Height*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#else
 static auto* pixels=static_cast<uint16_t*>(std::malloc(Width*Height*sizeof(uint16_t)));
#endif
 static bool built=false;
 if(pixels&&!built){Canvas layer{pixels};layer.rect(0,0,Width,Height,Black);instrument_base(layer);built=true;}
 return pixels;
}
void instrument(Canvas& c,const Model& m,uint64_t now,RenderCache* cache){
 constexpr auto Bevel=rgb(73,88,68),Lume=rgb(203,211,174),Etch=rgb(148,163,132),Recess=rgb(12,23,17),Red=rgb(195,76,55);
 const auto* backdrop=instrument_background();
 if(cache&&backdrop&&cache->valid&&cache->face==1&&cache->buffer==c.p){
  std::memcpy(cache->changed,cache->used,sizeof cache->used);
  for(int row=0;row<RenderCache::Rows;++row)for(int col=0;col<RenderCache::Cols;++col){
   if(!cache->used[row*RenderCache::Cols+col])continue;
   const int x=col*RenderCache::Tile,y=row*RenderCache::Tile,w=std::min(RenderCache::Tile,Width-x),h=std::min(RenderCache::Tile,Height-y);
   for(int j=0;j<h;++j)std::memcpy(c.p+(y+j)*Width+x,backdrop+(y+j)*Width+x,w*sizeof(uint16_t));
  }
 }else{
  if(backdrop)std::memcpy(c.p,backdrop,Width*Height*sizeof(uint16_t));else instrument_base(c);
  if(cache)std::memset(cache->changed,1,sizeof cache->changed);
 }
 if(cache){std::memset(cache->used,0,sizeof cache->used);cache->valid=backdrop!=nullptr;cache->face=1;cache->buffer=c.p;c.damage=cache;}
 const bool live=m.time_valid&&now>=m.sampled_us&&now-m.sampled_us<250000&&now<m.until_us;
 const bool evidence=live&&!m.local_time;
 // A rotating source drum sits beside a moon-phase PLL indication. This is
 // a mechanical calendar aperture, not a numerical digital readout. No source
 // pointer or phase boundary shares the lettering's reserved rectangle.
 const auto selected=receiver_source(m,live);
 const char* radio=selected==Source::BLE?"BLE":selected==Source::LOR?"LOR":selected==Source::RC9?"RC9":live?"HOLD":"---";
 centered(c,156,145,radio,14,Lume);
 constexpr int phase_x=205,phase_y=154,phase_r=17;
 circle(c,phase_x,phase_y,phase_r,Recess,true);
 const double phase=m.motion?m.phase_position:evidence&&m.locked&&!m.holding?1:live&&m.holding?.5:0;
 for(int yy=-phase_r;yy<=phase_r;++yy){const int half=int(std::sqrt(double(phase_r*phase_r-yy*yy)));const int lit=int((2*half+1)*phase);if(lit)c.rect(phase_x-half,phase_y+yy,lit,1,Lume);}
 circle(c,phase_x,phase_y,phase_r,Bevel);
 centered(c,247,145,"RX",14,Etch);
 circle(c,265,154,4,Recess,true);metal_ring(c,265,154,4,3,true);
 if(m.lora_on)circle(c,265,154,2,Lume,true);
 // Linear 0ns..10us RMS scale: quarter divisions are 2500ns.
 const bool rms=evidence&&m.rms_ns!=UINT32_MAX;double a=-135+270*(m.motion?m.rms_position:rms?std::min(uint32_t(10000),m.rms_ns)/10000.0:1);
 needle(c,116,249,a,37,1,Lume);circle(c,116,249,3,Bevel,true);
 if(rms&&m.rms_ns>10000)centered(c,116,284,"OVER",14,Etch);
 const bool hold=evidence&&sampled_hold(m);
 a=-135+270*(m.motion?m.hold_position:hold?(std::max(1.0,std::min(60.0,m.hold_ms/1000.0))-1)/59:0);
 needle(c,294,249,a,37,1,Lume);circle(c,294,249,3,Bevel,true);centered(c,294,hold&&m.hold_lower?251:257,hold&&m.hold_lower?">=s":"s",14,Etch);
 // Mechanical power-reserve sector, with a distinct unavailable parking mark.
 needle(c,205,342,-110+220*(m.motion?m.reserve_position:m.battery>=0?std::min(100,int(m.battery))/100.0:0),30,1,Lume);circle(c,205,342,3,Bevel,true);
 if(!live){c.rect(176,289,58,20,Recess);c.frame(176,289,58,20,2,Bevel);centered(c,205,291,"NO TIME",14,Lume);}
 // Main hands are the only time display. Unknown time parks at twelve.
 double seconds=live?double((m.utc_us+now-m.sampled_us)%86400000000ULL)/1000000.0+m.tz_minutes*60:0;
 double minute=std::fmod(seconds/60,60)*6,hour=std::fmod(seconds/3600,12)*30;
 pilot(c,205,247,hour,94,true,Etch,Lume);
 pilot(c,205,247,minute,135,false,Etch,Lume);
 needle(c,205,247,live?std::fmod(seconds,60)*6:0,145,0,Red);circle(c,205,247,7,Recess,true);circle(c,205,247,4,Lume,true);
 centered(c,205,439,"MODE",20,Etch);c.line(139,453,148,449,Bevel);c.line(139,453,148,457,Bevel);c.line(271,453,262,449,Bevel);c.line(271,453,262,457,Bevel);
}
// Solar is a fixed-location daily ephemeris, not a live weather or tide sensor.
constexpr auto SolarInk=rgb(211,235,250),SolarMuted=rgb(139,181,210),SolarNight=rgb(5,15,30),SolarTrack=rgb(32,57,76),SolarBlue=rgb(113,191,235);
// ESP32 has hardware float roots. Preserve raster decisions with double roots
// near integer bounds and alpha thresholds. For squared distances <=84^2,
// float conversion/root error is <0.00001 px (<0.003 alpha units); guards
// below are conservative. Larger discs retain the original double path.
double solar_bounds_root(double squared,double center){
 const double fast=double(std::sqrt(float(squared)));
 const double left=center-fast,right=center+fast;
 if(squared>84*84||std::abs(left-std::round(left))<.0002||std::abs(right-std::round(right))<.0002)return std::sqrt(squared);
 return fast;
}
double solar_coverage_root(double squared,double outer){
 const double fast=double(std::sqrt(float(squared))),scaled=(outer-fast)*255;
 if(squared>84*84||std::abs(scaled-std::round(scaled))<.01)return std::sqrt(squared);
 return fast;
}
void soft_disc(Canvas& c,double x,double y,double radius,uint16_t color){
 // Fill fully covered scanline spans; antialias only the limb. This also keeps
 // the moving clock medallion bounded without a square-root per interior pixel.
 const double outer=radius+.5,inner=std::max(0.0,radius-.5);
 for(int yy=int(std::ceil(y-outer));yy<=int(std::floor(y+outer));++yy){
  const double dy=yy-y,oh=solar_bounds_root(std::max(0.0,outer*outer-dy*dy),x);
  const int left=int(std::ceil(x-oh)),right=int(std::floor(x+oh));
  int il=right+1,ir=right;
  if(std::abs(dy)<=inner){const double ih=solar_bounds_root(std::max(0.0,inner*inner-dy*dy),x);il=int(std::ceil(x-ih));ir=int(std::floor(x+ih));if(il<=ir)c.rect(il,yy,ir-il+1,1,color);}
  for(int xx=left;xx<=right;++xx){if(xx>=il&&xx<=ir)continue;const double coverage=outer-solar_coverage_root((xx-x)*(xx-x)+dy*dy,outer);if(coverage>0)c.blend(xx,yy,color,unsigned(std::min(1.0,coverage)*255));}
 }
}
// Fixed geometry and resampled glyph masks are built once in PSRAM. Keep only
// the pointer/guard in internal RAM, leaving SPI/DMA/radio startup headroom.
struct SolarGeometryCache {
 struct XY {double x,y;};
 XY ring[151],rim[307],tick_outer[24],tick_inner[24];
 struct Pixel {int8_t x,y;uint8_t alpha;};
 struct Dot {uint8_t count;Pixel pixels[16];};
 Dot ring_dots[151];uint8_t dial_discs[2][71*71];
 uint8_t halo[55*55],digits[12][54*54];
};
static_assert(sizeof(SolarGeometryCache)<=64*1024,"Keep Solar geometry inside its PSRAM budget");
int solar_digit_index(char ch){return ch>='0'&&ch<='9'?ch-'0':ch==':'?10:ch=='-'?11:-1;}
const SolarGeometryCache* solar_geometry_cache(){
#ifdef ESP_PLATFORM
 static auto* data=static_cast<SolarGeometryCache*>(heap_caps_malloc(sizeof(SolarGeometryCache),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#else
 static auto* data=static_cast<SolarGeometryCache*>(std::malloc(sizeof(SolarGeometryCache)));
#endif
 static bool built=false;
 if(data&&!built){
  for(int i=0;i<=150;++i){const double a=(-150+i*2)*Pi/180;data->ring[i]={31*std::sin(a),-31*std::cos(a)};}
  for(int i=0;i<=306;++i){const double a=(i-4)*Pi/180;data->rim[i]={82*std::sin(a),-82*std::cos(a)};}
  for(int i=0;i<24;++i){const double a=i*15*Pi/180,r=i%3==0?26:28;data->tick_outer[i]={29*std::sin(a),-29*std::cos(a)};data->tick_inner[i]={r*std::sin(a),-r*std::cos(a)};}
  for(int y=-27;y<=27;++y)for(int x=-27;x<=27;++x){const double r=std::sqrt(double(x*x+y*y)),f=1-r/27;data->halo[(y+27)*55+x+27]=r<27?uint8_t(130*f*f):0;}
  // All four approved corner centers have the same half-pixel phase. Store
  // their exact alpha operations, not a recolored approximation of a bitmap.
  for(int n=0;n<2;++n){const double outer=(n==0?solar::CornerRadius:32)+.5;
   for(int y=-35;y<=35;++y)for(int x=-35;x<=35;++x){const double alpha=outer-std::hypot(x-.5,y-.5);data->dial_discs[n][(y+35)*71+x+35]=uint8_t(255*std::clamp(alpha,0.0,1.0));}
  }
  for(int i=0;i<=150;++i){auto& dot=data->ring_dots[i];dot.count=0;const double cx=.5+data->ring[i].x,cy=.5+data->ring[i].y;
   for(int y=int(std::ceil(cy-1.75));y<=int(std::floor(cy+1.75));++y)for(int x=int(std::ceil(cx-1.75));x<=int(std::floor(cx+1.75));++x){const double alpha=1.75-std::sqrt((x-cx)*(x-cx)+(y-cy)*(y-cy));
    if(alpha>0)dot.pixels[dot.count++]={int8_t(x),int8_t(y),uint8_t(255*std::min(1.0,alpha))};
   }
  }
  constexpr double scale=1.5;
  for(int n=0;n<12;++n){const char ch=n<10?char('0'+n):n==10?':':'-';const auto g=watchfont::glyphs36[unsigned(ch)-32];
   if(int(g.height*scale)>54||int(g.width*scale)>54)continue;
   for(int y=0;y<int(g.height*scale);++y)for(int x=0;x<int(g.width*scale);++x){
    const double sx=x/scale,sy=y/scale;const int ix=int(sx),iy=int(sy);const double fx=sx-ix,fy=sy-iy;
    const auto a=[&](int px,int py){return px<g.width&&py<g.height?watchfont::pixels36[g.offset+py*g.width+px]:0;};
    data->digits[n][y*54+x]=uint8_t(a(ix,iy)*(1-fx)*(1-fy)+a(ix+1,iy)*fx*(1-fy)+a(ix,iy+1)*(1-fx)*fy+a(ix+1,iy+1)*fx*fy);
   }
  }
  built=true;
 }
 return data;
}
void solar_subdial(Canvas& c,double cx,double cy,double progress,const char* label,const char* value,uint16_t color,bool active=true){
 // Four self-contained circular complications replace the perimeter indicators.
 const auto* geometry=solar_geometry_cache();const int ox=int(std::floor(cx)),oy=int(std::floor(cy));
 const bool fixed=geometry&&cx-ox==.5&&cy-oy==.5;
 if(fixed){for(int disc=0;disc<2;++disc)for(int y=-35;y<=35;++y)for(int x=-35;x<=35;++x)c.blend(ox+x,oy+y,disc?SolarNight:SolarTrack,geometry->dial_discs[disc][(y+35)*71+x+35]);}
 else{soft_disc(c,cx,cy,solar::CornerRadius,SolarTrack);soft_disc(c,cx,cy,32,SolarNight);}
 for(int i=0;i<24;++i){
  if(geometry){const auto p=geometry->tick_outer[i],q=geometry->tick_inner[i];c.line(int(std::lround(cx+p.x)),int(std::lround(cy+p.y)),int(std::lround(cx+q.x)),int(std::lround(cy+q.y)),SolarTrack);}
  else{const double a=i*15*Pi/180,r=i%3==0?26:28;c.line(int(std::lround(cx+29*std::sin(a))),int(std::lround(cy-29*std::cos(a))),int(std::lround(cx+r*std::sin(a))),int(std::lround(cy-r*std::cos(a))),SolarTrack);}
 }
 const double end=-150+300*std::clamp(progress,0.0,1.0);
 for(int i=0;i<=150;++i){const double angle=-150+i*2;
  if(fixed){const auto& dot=geometry->ring_dots[i];const auto ink=active&&angle<=end?color:SolarTrack;for(unsigned k=0;k<dot.count;++k){const auto p=dot.pixels[k];c.blend(ox+p.x,oy+p.y,ink,p.alpha);}}
  else if(geometry){const auto p=geometry->ring[i];soft_disc(c,cx+p.x,cy+p.y,1.25,active&&angle<=end?color:SolarTrack);}
  else{const double a=angle*Pi/180;soft_disc(c,cx+31*std::sin(a),cy-31*std::cos(a),1.25,active&&angle<=end?color:SolarTrack);}
 }
 if(active){const double a=end*Pi/180;soft_disc(c,cx+31*std::sin(a),cy-31*std::cos(a),2.3,SolarInk);}
 int width=0;for(auto p=value;*p;++p)width+=watchfont::glyphs20[unsigned(*p)-32].advance;
 const int tx=int(std::lround(cx)),ty=int(std::lround(cy));
 centered(c,tx,ty-21,label,14,SolarMuted);centered(c,tx,ty-3,value,width>54?14:20,active?SolarInk:SolarMuted);
}
void solar_medallion(Canvas& c,double cx,double cy){
 soft_disc(c,cx,cy,83,SolarTrack);soft_disc(c,cx,cy,81,rgb(7,20,34));
 // Reference-inspired pale rim, with one quiet upper-left break.
 const auto* geometry=solar_geometry_cache();
 for(int i=0;i<=306;++i){if(geometry){const auto p=geometry->rim[i];soft_disc(c,cx+p.x,cy+p.y,1.2,SolarInk);}else{const double a=(i-4)*Pi/180;soft_disc(c,cx+82*std::sin(a),cy-82*std::cos(a),1.2,SolarInk);}}
}
void solar_time(Canvas& c,int cx,int y,const char* text){
 // Resample the bundled antialiased atlas rather than introduce another font.
 constexpr double scale=1.5;const auto* glyph=watchfont::glyphs36;const auto* atlas=watchfont::pixels36;const auto* geometry=solar_geometry_cache();
 int width=0;for(auto p=text;*p;++p)width+=glyph[unsigned(*p)-32].advance;
 double x=cx-width*scale/2;
 for(auto p=text;*p;++p){const auto g=glyph[unsigned(*p)-32];const int digit=solar_digit_index(*p);
  for(int yy=0;yy<int(g.height*scale);++yy)for(int xx=0;xx<int(g.width*scale);++xx){
   if(geometry&&digit>=0&&int(g.height*scale)<=54&&int(g.width*scale)<=54){c.blend(int(x+g.left*scale)+xx,y+int(g.top*scale)+yy,SolarInk,geometry->digits[digit][yy*54+xx]);continue;}
   const double sx=xx/scale,sy=yy/scale;const int ix=int(sx),iy=int(sy);const double fx=sx-ix,fy=sy-iy;
   auto a=[&](int px,int py){return px<g.width&&py<g.height?atlas[g.offset+py*g.width+px]:0;};
   const unsigned alpha=unsigned(a(ix,iy)*(1-fx)*(1-fy)+a(ix+1,iy)*fx*(1-fy)+a(ix,iy+1)*(1-fx)*fy+a(ix+1,iy+1)*fx*fy);
   c.blend(int(x+g.left*scale)+xx,y+int(g.top*scale)+yy,SolarInk,alpha);
  }x+=g.advance*scale;
 }
}
void lunar_disc(Canvas& c,int cx,int cy,double phase){
 constexpr int radius=13;soft_disc(c,cx,cy,radius,rgb(40,60,83));
 const double terminator=std::cos(phase*2*Pi);
 for(int y=-radius;y<=radius;++y){const double half=std::sqrt(double(radius*radius-y*y));
  for(int x=-radius;x<=radius;++x){if(std::abs(x)>half)continue;
   const bool lit=phase<.5?x>=half*terminator:x<=-half*terminator;
   if(lit)c.blend(cx+x,cy+y,tone(rgb(155,181,202),SolarInk,.65-.2*y/radius),unsigned(std::min(1.0,half-std::abs(x)+.5)*255));
  }
 }
}
void solar_base(Canvas& c,const calendar::Snapshot& a,bool known){
 c.rect(0,0,Width,Height,Black);
 for(int y=22;y<480;++y){const int inset=std::max(0,55-std::min(y-22,479-y));c.rect(18+inset,y,374-2*inset,1,tone(rgb(12,27,43),SolarNight,double(y-22)/458));}
 c.frame(18,22,374,458,55,SolarTrack);
 // The actual morning/evening events form chords on the FIXED civil dial.
 // Seasonal daylight changes their height; civil solar-noon offset changes tilt.
 const auto horizon=solar::chord(a.today.sunrise,a.today.sunset),civil=solar::chord(a.today.dawn,a.today.dusk),nautical=solar::chord(a.today.nautical_dawn,a.today.nautical_dusk),astro=solar::chord(a.today.astronomical_dawn,a.today.astronomical_dusk);
 const double day=known?std::max(0.0,std::min(1.0,(a.solar_altitude_deg+12)/30)):0;
 const double warm=known?std::max(0.0,1-std::abs(a.solar_altitude_deg+1)/12):0;
 const auto zenith=tone(rgb(9,23,49),rgb(35,115,176),day),near=tone(tone(rgb(34,58,87),rgb(116,170,192),day),rgb(255,177,111),warm);
 // The minute-cached background has no per-pixel pow/trigonometry. A small
 // daylight palette and column heights keep tilted-band construction bounded.
 uint16_t daylight[256];for(int i=0;i<256;++i)daylight[i]=tone(zenith,near,std::pow(i/255.0,1.8));
 // Work one column at a time: retaining all four height arrays cost 6560
 // bytes of internal DRAM needed by the SPI/DMA driver during cold startup.
 for(int xx=-solar::SkyRadius;xx<=solar::SkyRadius;++xx){const int half=int(std::sqrt(double(solar::SkyRadius*solar::SkyRadius-xx*xx))),x=solar::CenterX+xx;
  if(!known){c.rect(x,solar::CenterY-half,1,2*half+1,SolarNight);continue;}
  const float h=horizon.y(x),ci=civil.y(x),n=nautical.y(x),a=astro.y(x);
  for(int y=solar::CenterY-half;y<=solar::CenterY+half;++y){uint16_t color;
   if(y<h)color=daylight[std::clamp(int(255*(y-85)/std::max(1.0f,h-85)),0,255)];
   else if(y<ci)color=tone(rgb(39,83,116),rgb(30,62,95),(y-h)/std::max(1.0f,ci-h));
   else if(y<n)color=tone(rgb(27,54,86),rgb(20,38,66),(y-ci)/std::max(1.0f,n-ci));
   else if(y<a)color=tone(rgb(18,34,60),rgb(10,23,42),(y-n)/std::max(1.0f,a-n));
   else color=tone(rgb(10,23,42),rgb(5,14,27),(y-a)/std::max(1.0f,417-a));
   c.pixel(x,y,color);
  }
 }
 // An illustrative star field, deliberately not a named constellation map.
 uint32_t seed=0x51a7;for(int i=0;known&&i<110;++i){seed=seed*1664525+1013904223;const int x=58+int(seed%294);seed=seed*1664525+1013904223;const int y=102+int(seed%298);const int r2=(x-205)*(x-205)+(y-251)*(y-251);
  if(r2<151*151&&r2>90*90&&(y>nautical.y(x)||day<.15)){const unsigned alpha=y>astro.y(x)?150:day<.15?110:55;c.blend(x,y,SolarInk,alpha);if(i%11==0){c.blend(x-1,y,SolarInk,alpha/3);c.blend(x+1,y,SolarInk,alpha/3);c.blend(x,y-1,SolarInk,alpha/3);c.blend(x,y+1,SolarInk,alpha/3);}}
 }
 circle(c,205,251,166,SolarTrack);
 // The civil scale never rotates: 06 and 18 share the horizontal diameter.
 for(int i=0;known&&i<96;++i){const double angle=solar::hour_angle(i/4.0);const auto p=polar(205,251,162,angle),q=polar(205,251,i%4==0?153:i%2==0?158:160,angle);c.line(p.x,p.y,q.x,q.y,i%4==0?SolarMuted:SolarTrack);}
 for(int h=0;known&&h<24;h+=2){const auto p=polar(205,251,143,solar::hour_angle(h));char b[4];std::snprintf(b,sizeof b,"%02d",h);
  const auto bg=c.p[p.y*Width+p.x];const double luminance=.2126*std::pow((bg>>11)/31.0,2.2)+.7152*std::pow(((bg>>5)&63)/63.0,2.2)+.0722*std::pow((bg&31)/31.0,2.2);
  const auto g0=watchfont::glyphs20[unsigned(b[0])-32],g1=watchfont::glyphs20[unsigned(b[1])-32];
  const int top=std::min(g0.top,g1.top),bottom=std::max(g0.top+g0.height,g1.top+g1.height);
  centered(c,p.x,p.y-(top+bottom)/2,b,20,luminance>.19?rgb(4,17,30):p.y<horizon.y(p.x)?rgb(236,247,255):SolarMuted);
 }
 circle(c,205,251,124,tone(SolarTrack,SolarMuted,.25));
 const uint64_t crossings[]={a.today.sunrise,a.today.sunset,a.today.dawn,a.today.dusk,a.today.nautical_dawn,a.today.nautical_dusk,a.today.astronomical_dawn,a.today.astronomical_dusk};
 for(unsigned i=0;known&&i<8;++i)if(crossings[i]){
  const double angle=solar::civil_angle(crossings[i],calendar::central_offset(crossings[i]));
  const auto p=polar(205,251,124,angle),lo=polar(205,251,118,angle),hi=polar(205,251,130,angle);
  c.line(lo.x,lo.y,hi.x,hi.y,SolarMuted);soft_disc(c,p.x,p.y,i<2?4.0:3.0,SolarInk);
 }
 centered(c,205,432,"SANTA ROSA BEACH",14,SolarMuted);
 centered(c,205,451,"MODE",20,SolarInk);
 c.line(158,466,167,462,SolarMuted);c.line(158,466,167,470,SolarMuted);c.line(252,466,243,462,SolarMuted);c.line(252,466,243,470,SolarMuted);
}
const uint16_t* solar_background(const calendar::Snapshot& a,bool known,uint32_t& generation){
#ifdef ESP_PLATFORM
 static auto* pixels=static_cast<uint16_t*>(heap_caps_malloc(Width*Height*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#else
 static auto* pixels=static_cast<uint16_t*>(std::malloc(Width*Height*sizeof(uint16_t)));
#endif
 static uint64_t minute=UINT64_MAX;static bool was_known=false,built=false;static uint32_t revision=0;
 const bool changed=!built||minute!=a.sampled_utc/60||known!=was_known;
 if(pixels&&changed){Canvas layer{pixels};solar_base(layer,a,known);minute=a.sampled_utc/60;was_known=known;built=true;++revision;}
 generation=revision;return pixels;
}
void solar_face(Canvas& c,const Model& m,uint64_t now,RenderCache* cache){
 const bool live=m.time_valid&&now>=m.sampled_us&&now-m.sampled_us<250000&&now<m.until_us;
 const uint64_t utc=live?(m.utc_us+now-m.sampled_us)/1000000:0;const auto& a=m.calendar;
 const bool known=live&&a.astronomy_valid&&a.today.valid&&utc>=a.sampled_utc&&utc-a.sampled_utc<120;
 uint32_t generation=0;const auto* backdrop=solar_background(a,known,generation);
 if(cache&&backdrop&&cache->generation==generation&&cache->valid&&cache->face==2&&cache->buffer==c.p){
  std::memcpy(cache->changed,cache->used,sizeof cache->used);
  for(int row=0;row<RenderCache::Rows;++row)for(int col=0;col<RenderCache::Cols;++col){if(!cache->used[row*RenderCache::Cols+col])continue;const int x=col*32,y=row*32,w=std::min(32,Width-x),h=std::min(32,Height-y);for(int j=0;j<h;++j)std::memcpy(c.p+(y+j)*Width+x,backdrop+(y+j)*Width+x,w*sizeof(uint16_t));}
 }else{if(backdrop)std::memcpy(c.p,backdrop,Width*Height*sizeof(uint16_t));else solar_base(c,a,known);if(cache)std::memset(cache->changed,1,sizeof cache->changed);}
 if(cache){std::memset(cache->used,0,sizeof cache->used);cache->valid=backdrop!=nullptr;cache->face=2;cache->generation=generation;cache->buffer=c.p;c.damage=cache;}
 const bool evidence=live&&!m.local_time&&!m.holding,rms=evidence&&m.rms_ns!=UINT32_MAX,hold=evidence&&sampled_hold(m),signal=evidence&&m.source_rssi>-127&&m.source_rssi<=0;
 char b[48];
 if(rms){if(m.rms_ns>10000)std::snprintf(b,sizeof b,">10 us");else if(m.rms_ns<1000)std::snprintf(b,sizeof b,"%u ns",unsigned(m.rms_ns));else std::snprintf(b,sizeof b,"%.1f us",m.rms_ns/1000.0);}else std::snprintf(b,sizeof b,"--");
 solar_subdial(c,solar::CornerLeft,solar::CornerTop,m.motion?m.rms_position:rms?std::min(1.0,m.rms_ns/10000.0):1,"RMS",b,SolarBlue,rms);
 if(hold)hold_text(b,sizeof b,uint64_t(m.hold_ms)*1000);else std::snprintf(b,sizeof b,"--");
 solar_subdial(c,solar::CornerRight,solar::CornerTop,m.motion?m.hold_position:hold?(std::max(1.0,std::min(60.0,m.hold_ms/1000.0))-1)/59:0,"P99",b,SolarBlue,hold);
 const char* radio=live&&(m.local_time||m.holding)?"HOLD":evidence?source(m.source):"--";
 solar_subdial(c,solar::CornerLeft,solar::CornerBottom,m.motion?m.signal_position:signal?std::max(0.0,std::min(1.0,(m.source_rssi+110)/70.0)):0,"SRC",radio,SolarBlue,signal);
 const bool tide=known&&a.tides==calendar::Coverage::Available&&a.tide_position_valid;
 solar_subdial(c,solar::CornerRight,solar::CornerBottom,m.motion?m.tide_position:tide?a.tide_position:0,"TIDE ~",tide?(a.tide_rising?"RISE":"FALL"):a.tides==calendar::Coverage::Expired?"EXP":"--",rgb(133,206,205),tide);
 const double elapsed=live?((m.utc_us+now-m.sampled_us)%1000000)/1000000.0:0;
 const double angle=known?solar::civil_angle(utc,a.local_offset_minutes,elapsed):0;
 const auto clock=m.motion?m.solar_clock:solar_opposition(angle);
 const double cx=205+clock.x,cy=251+clock.y;solar_medallion(c,cx,cy);
 const int tx=int(std::lround(cx)),ty=int(std::lround(cy));
 const int16_t solar_offset=known?a.local_offset_minutes:live?calendar::central_offset(utc):0;
 time_t local=time_t(utc)+solar_offset*60;tm date{};if(live)gmtime_r(&local,&date);
 if(live)std::strftime(b,sizeof b,"%a %d %b",&date);else std::snprintf(b,sizeof b,"NO TIME");centered(c,tx,ty-47,b,20,SolarMuted);
 if(live)std::snprintf(b,sizeof b,"%02d:%02d",date.tm_hour,date.tm_min);else std::snprintf(b,sizeof b,"--:--");solar_time(c,tx,ty-23,b);
 if(live)std::snprintf(b,sizeof b,"%s  %02d",m.local_time||m.holding?"HOLD":zone_name(2,solar_offset),date.tm_sec);else std::snprintf(b,sizeof b,"WAIT");centered(c,tx,ty+39,b,14,SolarMuted);
 if(known){
  const auto p=polar(205,251,124,angle);
  // Soft solar limb is confined to the physical marker, not decorative UI glow.
  const auto* geometry=solar_geometry_cache();
  for(int yy=-27;yy<=27;++yy)for(int xx=-27;xx<=27;++xx){
   if(geometry)c.blend(p.x+xx,p.y+yy,rgb(255,207,139),geometry->halo[(yy+27)*55+xx+27]);
   else{const double r=std::sqrt(double(xx*xx+yy*yy));if(r<27){const double falloff=1-r/27;c.blend(p.x+xx,p.y+yy,rgb(255,207,139),unsigned(130*falloff*falloff));}}
  }
  soft_disc(c,p.x,p.y,11.0,rgb(255,229,179));soft_disc(c,p.x-.6,p.y-.7,9.2,rgb(255,246,221));
  lunar_disc(c,tx,ty-66,a.moon_phase);
 }else{centered(c,tx,ty-65,"SKY --",14,SolarMuted);}
}

void row(Canvas& c,int y,const char* title,const char* detail){c.text(42,y,title,36,Ink);c.text(42,y+40,detail,20,Muted);c.chevron(350,y+21);c.rect(y>350?64:42,y+79,y>350?282:326,1,Rule);}
}
const char* zone_name(uint16_t id,int16_t offset){
 if(id==2&&offset==-360)return "CST";
 if(id==2&&offset==-300)return "CDT";
 return !offset?"UTC":"LOC";
}
void hold_text(char* out,unsigned size,uint64_t us){
 const auto value=us>=1000000?us/1000000:us>=1000?us/1000:us;
 std::snprintf(out,size,"%llu %s",static_cast<unsigned long long>(value),us>=1000000?"s":us>=1000?"ms":"us");
}
void View::tick(uint64_t now,bool usb){if(usb){asleep=false;glance_until_us=0;activity_us=now;return;}if(now<activity_us){activity_us=now;glance_until_us=0;return;}if(glance_until_us?now>=glance_until_us:now-activity_us>=uint64_t(settings.timeout_s)*1000000){asleep=true;glance_until_us=0;}}
void View::button(uint64_t now){glance_until_us=0;activity_us=now;if(asleep){asleep=false;return;}screen=screen==Screen::Face?Screen::Launcher:Screen::Face;}
void View::touch(int x,int y,uint64_t now){if(x<0||x>=Width||y<0||y>=Height)return;glance_until_us=0;activity_us=now;if(asleep){asleep=false;return;}if(screen==Screen::Face){if(y>=430)screen=Screen::Launcher;return;}if(y<95){screen=screen==Screen::Launcher?Screen::Face:Screen::Launcher;return;}if(screen==Screen::Launcher){if(y<215)screen=Screen::Faces;else if(y<325)screen=Screen::Settings;else if(y>=400)screen=Screen::Face;}else if(screen==Screen::Faces){if(y>=105&&y<425){settings.face=y<210?0:y<315?1:2;dirty=true;screen=Screen::Face;}}else if(screen==Screen::Settings){if(y>=105&&y<185){settings.brightness=uint8_t(settings.brightness==100?10:std::min(100,int(settings.brightness)+10));dirty=true;}else if(y>=185&&y<265){settings.timeout_s=settings.timeout_s==15?30:settings.timeout_s==30?60:settings.timeout_s==60?90:15;dirty=true;}else if(y>=265&&y<345){radio_request=1;}else if(y>=345&&y<425){radio_request=2;}}}
void render(uint16_t* pixels,const Model& m,const View& v,uint64_t now,RenderCache* cache){
 Canvas c{pixels};
 if(!v.asleep&&v.screen==Screen::Face&&v.settings.face==2){solar_face(c,m,now,cache);return;}
 if(!v.asleep&&v.screen==Screen::Face&&v.settings.face==1){instrument(c,m,now,cache);return;}
 if(cache){cache->valid=false;std::memset(cache->changed,1,sizeof cache->changed);}
 c.rect(0,0,Width,Height,Black);if(v.asleep)return;
 // Warm the immutable dial at first presentation, even when booting Digital.
 // Subsequent face swipes copy this PSRAM layer and only draw live indicators.
 (void)instrument_background();
 char b[64];

 if(v.screen!=Screen::Face){
  c.text(52,42,"<",36,Amber);c.text(89,42,v.screen==Screen::Launcher?"APPS":v.screen==Screen::Settings?"SETTINGS":"WATCH FACES",36,Ink);c.rect(42,96,326,1,Rule);
  if(v.screen==Screen::Launcher){row(c,116,"Watch faces",v.settings.face==0?"DIGITAL / selected":v.settings.face==1?"INSTRUMENT / selected":"SOLAR / selected");row(c,226,"Settings","Display and radio controls");c.text(42,357,m.lora_on?"BLE preferred / LoRa receiving":"BLE preferred / LoRa off",20,m.lora_on?Muted:Amber);row(c,405,"Return to watch","Last face opens at boot");}
  else if(v.screen==Screen::Settings){
   auto setting=[&](int y,const char* name,const char* value,const char* detail,bool warn=false){c.text(42,y,name,26,Ink);c.text(275,y,value,26,warn?Amber:Ink);c.text(42,y+32,detail,20,Muted);c.rect(42,y+68,326,1,Rule);};
   std::snprintf(b,sizeof b,"%u%%",v.settings.brightness);setting(108,"Brightness",b,"Tap to adjust");
   std::snprintf(b,sizeof b,"%us",v.settings.timeout_s);setting(188,"Display sleep",b,"Tap to change");
   auto radio=[&](int y,const char* name,RadioState state){const char* value=state==RadioState::On?"ON":state==RadioState::Off?"OFF":state==RadioState::Fault?"FAULT":"WAIT";setting(y,name,value,state==RadioState::On?"Tap to turn off":state==RadioState::Off?"Tap to turn on":state==RadioState::Fault?"Radio fault / retry":"Changing receiver state",state!=RadioState::On);};
   radio(268,"BLE radio",m.ble_state);radio(348,"LoRa radio",m.lora_state);
   c.text(64,438,v.save_failed?"Display save failed":"Radios reset ON at boot",20,v.save_failed?Amber:Muted);
  }
  else {row(c,111,"Digital",v.settings.face==0?"SELECTED / segmented instrument":"Segmented instrument");row(c,216,"Instrument",v.settings.face==1?"SELECTED / mechanical indicators":"Mechanical indicators");row(c,321,"Solar",v.settings.face==2?"SELECTED / sun, moon and tides":"Sun, moon and tides");c.text(64,438,"Tap a face to wear it",20,Muted);}

  return;
 }
 // Chamfered instrument bezel; all text sits inside the rounded-panel safe area.
 c.frame(18,20,374,462,42,Rule);c.frame(22,24,366,454,40,Muted);
 c.text(150,35,"PToB",36,Ink);c.text(130,73,"PRECISION TIME",20,Amber);
 c.rect(64,58,65,2,Amber);c.rect(245,58,39,2,Amber);
 for(int i=0;i<4;++i){c.line(47,77+i*4,59,65+i*4,Rule);c.line(350,65+i*4,362,77+i*4,Rule);}
 c.rect(32,104,346,200,Face);c.frame(32,104,346,200,12,Muted);c.frame(36,108,338,192,10,Rule);
 if(m.battery>=0&&m.battery<=100){std::snprintf(b,sizeof b,"%d%%",m.battery);c.text(298,42,b,20,Ink);}else c.text(305,42,"--%",20,Muted);

 const bool live=m.time_valid&&now>=m.sampled_us&&now-m.sampled_us<250000&&now<m.until_us;
 uint64_t sec=live?(m.utc_us+now-m.sampled_us)/1000000:0;
 time_t local=time_t(sec)+m.tz_minutes*60;tm date{};if(live)gmtime_r(&local,&date);
 if(live){std::strftime(b,sizeof b,"%a  %d %b",&date);for(char* p=b;*p;++p)if(*p>='a'&&*p<='z')*p-=32;}else std::snprintf(b,sizeof b,"WAITING FOR TIME");c.text(48,115,b,26,Ink);
 c.text(316,120,zone_name(m.tz_id,m.tz_minutes),20,Muted);
 int h=live?date.tm_hour:-11,min=live?date.tm_min:-11;
 c.digit(48,158,55,108,h/10);c.digit(111,158,55,108,h%10);c.digit(198,158,55,108,min/10);c.digit(261,158,55,108,min%10);
 c.rect(178,184,7,7,live?Ink:Dim);c.rect(178,233,7,7,live?Ink:Dim);
 c.digit(327,230,16,34,live?date.tm_sec/10:-1,Amber);c.digit(349,230,16,34,live?date.tm_sec%10:-1,Amber);
 for(int i=0;i<60;++i)c.rect(48+i*5,282,3,i%5==0?9:5,live&&i<=date.tm_sec?Amber:Rule);
 c.frame(32,316,346,67,8,Rule);c.rect(139,324,1,51,Rule);c.rect(264,324,1,51,Rule);
 c.text(46,322,"SOURCE",20,Muted);c.text(151,322,"SYNC RMS",20,Muted);c.text(278,322,"PLL",20,Muted);
 c.text(46,346,m.local_time?"HOLD":live?source(m.source):"---",26,Ink);
 if(live&&m.rms_ns!=UINT32_MAX)(m.rms_ns<10000?std::snprintf(b,sizeof b,"%u ns",unsigned(m.rms_ns)):m.rms_ns<1000000?std::snprintf(b,sizeof b,"%u us",unsigned(m.rms_ns/1000)):std::snprintf(b,sizeof b,">=1 ms"));else std::snprintf(b,sizeof b,"-- ns");c.text(151,346,b,26,Ink);
 c.text(278,346,!live?"WAIT":m.holding?"HOLD":m.locked?"LOCK":"ACQ",26,live&&m.locked?Green:Amber);
 c.frame(32,393,346,39,8,Rule);c.text(46,399,"P99 <1us HOLD",20,Muted);
 if(live&&sampled_hold(m))hold_text(b,sizeof b,uint64_t(m.hold_ms)*1000);else std::snprintf(b,sizeof b,"--");c.text(250,395,b,26,Ink);
 c.text(66,450,m.lora_on?"LOR":"LOR OFF",20,m.lora_on?Muted:Amber);
 if(m.lora_on){for(int i=0;i<3;++i)c.rect(104+i*6,466-i*4,3,4+i*4,m.lora_receiving?Green:Rule);}

 // A printed MODE key belongs to the watch, not a web-style Apps link.
 c.frame(160,442,90,34,5,Amber);c.text(181,446,"MODE",20,Amber);
 const auto hours=m.uptime_s/3600;
 if(hours<1000)std::snprintf(b,sizeof b,"UP %uh",unsigned(hours));
 else std::snprintf(b,sizeof b,"UP %ud",unsigned(std::min(uint64_t(999),hours/24)));
 c.text(271,450,b,20,Muted);
}
}
