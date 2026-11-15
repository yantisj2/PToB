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
 uint16_t* p;
 void pixel(int x,int y,uint16_t c){if(x>=0&&x<Width&&y>=0&&y<Height)p[y*Width+x]=c;}
 void rect(int x,int y,int w,int h,uint16_t c){for(int j=std::max(0,y);j<std::min(Height,y+h);++j)for(int i=std::max(0,x);i<std::min(Width,x+w);++i)p[j*Width+i]=c;}
 void blend(int x,int y,uint16_t c,unsigned a){if(x<0||y<0||x>=Width||y>=Height)return;auto d=p[y*Width+x];unsigned r=(((c>>11)*a+(d>>11)*(255-a))+127)/255,g=((((c>>5)&63)*a+((d>>5)&63)*(255-a))+127)/255,b=(((c&31)*a+(d&31)*(255-a))+127)/255;pixel(x,y,uint16_t((r<<11)|(g<<5)|b));}
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
void instrument(Canvas& c,const Model& m,uint64_t now){
 constexpr auto Bevel=rgb(73,88,68),Lume=rgb(203,211,174),Etch=rgb(148,163,132),Recess=rgb(12,23,17),Red=rgb(195,76,55);
 const auto* backdrop=instrument_background();if(backdrop)std::memcpy(c.p,backdrop,Width*Height*sizeof(uint16_t));else instrument_base(c);
 const bool live=m.time_valid&&now>=m.sampled_us&&now-m.sampled_us<250000&&now<m.until_us;
 const bool evidence=live&&!m.local_time;
 // A rotating source drum sits beside a moon-phase PLL indication. This is
 // a mechanical calendar aperture, not a numerical digital readout. No source
 // pointer or phase boundary shares the lettering's reserved rectangle.
 const auto selected=receiver_source(m,evidence);
 const char* radio=selected==Source::BLE?"BLE":selected==Source::LOR?"LOR":selected==Source::RC9?"RC9":live?"HOLD":"---";
 centered(c,156,145,radio,14,Lume);
 constexpr int phase_x=205,phase_y=154,phase_r=17;
 circle(c,phase_x,phase_y,phase_r,Recess,true);
 if(evidence&&m.locked&&!m.holding)circle(c,phase_x,phase_y,phase_r,Lume,true);
 else if(live&&m.holding){
  // The illuminated half, dark half and bevel have one geometric center.
  for(int yy=-phase_r;yy<=phase_r;++yy){const int half=int(std::sqrt(double(phase_r*phase_r-yy*yy)));c.rect(phase_x-half,phase_y+yy,half+1,1,Lume);}
 }
 circle(c,phase_x,phase_y,phase_r,Bevel);
 centered(c,247,145,"RX",14,Etch);
 circle(c,265,154,4,Recess,true);metal_ring(c,265,154,4,3,true);
 if(m.lora_on)circle(c,265,154,2,Lume,true);
 // Linear 0ns..10us RMS scale: quarter divisions are 2500ns.
 const bool rms=evidence&&m.rms_ns!=UINT32_MAX;double a=rms?-135+270*std::min(uint32_t(10000),m.rms_ns)/10000.0:180;
 needle(c,116,249,a,37,1,rms?Lume:Red);circle(c,116,249,3,Bevel,true);
 if(rms&&m.rms_ns>10000)centered(c,116,284,"OVER",14,Red);
 const bool hold=evidence&&m.hold_valid&&!m.hold_lower&&m.hold_trials&&m.source!=Source::LOR&&m.source!=Source::RC9;
 a=hold?-135+270*(std::max(1.0,std::min(60.0,m.hold_ms/1000.0))-1)/59:180;
 needle(c,294,249,a,37,1,hold?Lume:Red);circle(c,294,249,3,Bevel,true);centered(c,294,257,"s",14,Etch);
 // Mechanical power-reserve sector, with a distinct unavailable parking mark.
 needle(c,205,342,m.battery>=0?-110+220*std::min(100,int(m.battery))/100:180,30,1,m.battery>=0?Lume:Red);circle(c,205,342,3,Bevel,true);
 if(!live){c.rect(176,289,58,20,Recess);c.frame(176,289,58,20,2,Bevel);centered(c,205,291,"NO TIME",14,Lume);}
 // Main hands are the only time display. Unknown time parks at twelve.
 double seconds=live?double((m.utc_us+now-m.sampled_us)/1000000%86400)+m.tz_minutes*60:0;
 double minute=std::fmod(seconds/60,60)*6,hour=std::fmod(seconds/3600,12)*30;
 pilot(c,205,247,hour,94,true,Etch,Lume);
 pilot(c,205,247,minute,135,false,Etch,Lume);
 needle(c,205,247,live?std::fmod(seconds,60)*6:0,145,0,Red);circle(c,205,247,7,Recess,true);circle(c,205,247,4,Lume,true);
 centered(c,205,439,"MODE",20,Etch);c.line(139,453,148,449,Bevel);c.line(139,453,148,457,Bevel);c.line(271,453,262,449,Bevel);c.line(271,453,262,457,Bevel);
}
void row(Canvas& c,int y,const char* title,const char* detail){c.text(42,y,title,36,Ink);c.text(42,y+40,detail,20,Muted);c.chevron(350,y+21);c.rect(y>350?64:42,y+79,y>350?282:326,1,Rule);}
}
void View::tick(uint64_t now){if(now<activity_us){activity_us=now;return;}if(now-activity_us>=uint64_t(settings.timeout_s)*1000000)asleep=true;}
void View::button(uint64_t now){activity_us=now;if(asleep){asleep=false;return;}screen=screen==Screen::Face?Screen::Launcher:Screen::Face;}
void View::touch(int x,int y,uint64_t now){if(x<0||x>=Width||y<0||y>=Height)return;activity_us=now;if(asleep){asleep=false;return;}if(screen==Screen::Face){if(y>=430)screen=Screen::Launcher;return;}if(y<95){screen=screen==Screen::Launcher?Screen::Face:Screen::Launcher;return;}if(screen==Screen::Launcher){if(y<215)screen=Screen::Faces;else if(y<325)screen=Screen::Settings;else if(y>=400)screen=Screen::Face;}else if(screen==Screen::Faces){if(y<390){settings.face=y<250?0:1;dirty=true;screen=Screen::Face;}}else if(screen==Screen::Settings){if(y>=110&&y<220){settings.brightness=uint8_t(settings.brightness==100?10:std::min(100,int(settings.brightness)+10));dirty=true;}else if(y>=220&&y<330){settings.timeout_s=settings.timeout_s==90?120:settings.timeout_s==120?180:90;dirty=true;}}}
void render(uint16_t* pixels,const Model& m,const View& v,uint64_t now){
 Canvas c{pixels};c.rect(0,0,Width,Height,Black);if(v.asleep)return;
 // Warm the immutable dial at first presentation, even when booting Digital.
 // Subsequent face swipes copy this PSRAM layer and only draw live indicators.
 (void)instrument_background();
 char b[64];
 if(v.screen==Screen::Face&&v.settings.face==1){instrument(c,m,now);return;}
 if(v.screen!=Screen::Face){
  c.text(52,42,"<",36,Amber);c.text(89,42,v.screen==Screen::Launcher?"APPS":v.screen==Screen::Settings?"SETTINGS":"WATCH FACES",36,Ink);c.rect(42,96,326,1,Rule);
  if(v.screen==Screen::Launcher){row(c,116,"Watch faces",v.settings.face==0?"DIGITAL / selected":"INSTRUMENT / selected");row(c,226,"Settings","Brightness and screen sleep");c.text(42,357,m.lora_on?"BLE preferred / LoRa receiving":"BLE preferred / LoRa off",20,m.lora_on?Muted:Amber);row(c,405,"Return to watch","Last face opens at boot");}
  else if(v.screen==Screen::Settings){std::snprintf(b,sizeof b,"%u%% / tap to adjust",v.settings.brightness);row(c,116,"Brightness",b);std::snprintf(b,sizeof b,"%u seconds / tap to change",v.settings.timeout_s);row(c,226,"Display sleep",b);c.text(42,347,"GNSS",26,Muted);c.text(305,347,"OFF",26,Amber);c.text(42,385,"Timing stays on during display sleep.",20,Muted);c.text(64,438,v.save_failed?"Save failed / change to retry":"Saved on change",20,v.save_failed?Amber:Muted);}
  else {row(c,116,"Digital",v.settings.face==0?"SELECTED / segmented instrument":"Segmented instrument");row(c,266,"Instrument",v.settings.face==1?"SELECTED / mechanical indicators":"Mechanical indicators");c.text(64,425,"Tap a face to wear it",20,Muted);}

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
 c.text(316,120,m.tz_minutes?"LOCAL":"UTC",20,Muted);
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
 if(live&&m.source!=Source::LOR&&m.source!=Source::RC9&&m.hold_valid&&m.hold_trials)(m.hold_ms<100000?std::snprintf(b,sizeof b,"%s%u.%us",m.hold_lower?">=":"",unsigned(m.hold_ms/1000),unsigned((m.hold_ms%1000)/100)):m.hold_ms<3600000?std::snprintf(b,sizeof b,"%s%um",m.hold_lower?">=":"",unsigned(m.hold_ms/60000)):std::snprintf(b,sizeof b,"%s%uh",m.hold_lower?">=":"",unsigned(m.hold_ms/3600000)));else std::snprintf(b,sizeof b,"--");c.text(250,395,b,26,Ink);
 c.text(66,450,m.lora_on?"LOR RX":"LOR OFF",20,m.lora_on?Muted:Amber);
 // A printed MODE key belongs to the watch, not a web-style Apps link.
 c.frame(160,442,90,34,5,Amber);c.text(181,446,"MODE",20,Amber);
 const auto hours=m.uptime_s/3600;
 if(hours<1000)std::snprintf(b,sizeof b,"UP %uh",unsigned(hours));
 else std::snprintf(b,sizeof b,"UP %ud",unsigned(std::min(uint64_t(999),hours/24)));
 c.text(271,450,b,20,Muted);
}
}
