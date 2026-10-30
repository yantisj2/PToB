#include "hold_status.hpp"
#include "display.hpp"
#include "display_transfer.hpp"
#include "w12_board_profile.hpp"
#ifndef W12_DISPLAY_PREVIEW
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_mac.h"
#include "w12_device_profile.hpp"
#endif
#include <cstring>
#include <cstdio>
#include <ctime>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#ifndef W12_DISPLAY_PREVIEW
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
namespace {
#ifndef W12_DISPLAY_PREVIEW
i2c_master_bus_handle_t bus=nullptr;
i2c_master_dev_handle_t oled=nullptr;
#endif
uint8_t fb[1024]{};
#ifndef W12_DISPLAY_PREVIEW
uint8_t displayed_fb[1024]{};
bool framebuffer_valid=false;
uint32_t display_errors=0;
#endif
uint8_t indicator_mask=0xff;
// Native landscape coordinates: the first 16 rows emit green, the remaining
// 48 emit blue. Brightness is global, so the green band uses sparse text.
const uint8_t digits[10][5]={{0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},
 {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},
 {0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
 {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e}};
const uint8_t letters[26][5]={{0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
 {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},
 {0x7f,0x09,0x09,0x09,0x01},{0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
 {0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},
 {0x7f,0x40,0x40,0x40,0x40},{0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
 {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},
 {0x7f,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},
 {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},
 {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}};
const uint8_t space[5]={},colon[5]={0,0x36,0x36,0,0},dash[5]={8,8,8,8,8};
const uint8_t dot[5]={0,0x60,0x60,0,0},slash[5]={0x20,0x10,8,4,2};
const uint8_t plus[5]={8,8,0x3e,8,8},percent[5]={0x23,0x13,8,0x64,0x62};
const uint8_t lower_a[5]={0x20,0x54,0x54,0x54,0x78};
const uint8_t lower_b[5]={0x7f,0x48,0x44,0x44,0x38};
const uint8_t lower_d[5]={0x38,0x44,0x44,0x48,0x7f};
const uint8_t lower_t[5]={0x04,0x3f,0x44,0x40,0x20};
const uint8_t arrow_glyph[5]={0,0x22,0x14,0x08,0};
const uint8_t lock_glyph[5]={0x78,0x7e,0x69,0x7e,0x78};
const uint8_t lower_u[5]={0x3c,0x40,0x40,0x20,0x7c};
const uint8_t lower_s[5]={0x48,0x54,0x54,0x54,0x24};
const uint8_t lower_c[5]={0x38,0x44,0x44,0x44,0x28};
const uint8_t lower_e[5]={0x38,0x54,0x54,0x54,0x18};
const uint8_t lower_h[5]={0x7f,0x08,0x04,0x04,0x78};
const uint8_t lower_i[5]={0,0x44,0x7d,0x40,0};
const uint8_t lower_k[5]={0x7f,0x10,0x28,0x44,0};
const uint8_t lower_l[5]={0,0x41,0x7f,0x40,0};
const uint8_t lower_m[5]={0x7c,0x04,0x78,0x04,0x78};
const uint8_t lower_n[5]={0x7c,0x08,0x04,0x04,0x78};
const uint8_t lower_o[5]={0x38,0x44,0x44,0x44,0x38};
const uint8_t lower_r[5]={0x7c,0x08,0x04,0x04,0x08};
const uint8_t lower_z[5]={0x44,0x64,0x54,0x4c,0x44};
const uint8_t* glyph(char c){
 if(c>='0'&&c<='9')return digits[c-'0'];
 if(c>='A'&&c<='Z')return letters[c-'A'];
 switch(c){case '\1':return lock_glyph;case '>':return arrow_glyph;case 'a':return lower_a;case 'b':return lower_b;case 'd':return lower_d;case 't':return lower_t;case ':':return colon;case '-':return dash;case '.':return dot;
 case '/':return slash;case '+':return plus;case '%':return percent;
 case 'u':return lower_u;case 's':return lower_s;
 case 'c':return lower_c;case 'e':return lower_e;case 'h':return lower_h;
 case 'i':return lower_i;case 'k':return lower_k;case 'l':return lower_l;
 case 'm':return lower_m;case 'n':return lower_n;case 'o':return lower_o;case 'r':return lower_r;
 case 'z':return lower_z;default:return space;}
}
void pixel(int x,int y){
 if(x<0||x>=128||y<0||y>=64)return;
 fb[(y/8)*128+x]|=uint8_t(1u<<(y&7));
}
void line(int x0,int y0,int x1,int y1){
 int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=-std::abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
 while(true){pixel(x0,y0);if(x0==x1&&y0==y1)break;
  int twice=2*err;if(twice>=dy){err+=dy;x0+=sx;}if(twice<=dx){err+=dx;y0+=sy;}}
}
void circle(int cx,int cy,int r){
 int x=r,y=0,err=1-r;
 while(x>=y){pixel(cx+x,cy+y);pixel(cx+y,cy+x);pixel(cx-y,cy+x);pixel(cx-x,cy+y);
  pixel(cx-x,cy-y);pixel(cx-y,cy-x);pixel(cx+y,cy-x);pixel(cx+x,cy-y);
  ++y;if(err<0)err+=2*y+1;else{--x;err+=2*(y-x)+1;}}
}
void text(int x,int y,const char* s,int scale=1){
 for(;*s&&x<128;++s,x+=6*scale){const uint8_t* g=glyph(*s);
  for(int col=0;col<5;++col)for(int row=0;row<7;++row)if(g[col]&(1u<<row))
   for(int dx=0;dx<scale;++dx)for(int dy=0;dy<scale;++dy)
    pixel(x+col*scale+dx,y+row*scale+dy);}
}
// A two-pixel forward shear keeps the existing seven-pixel bitmap face and
// baseline; only the requested quality prefix uses this italic treatment.
void italic_text(int x,int y,const char* s){
 for(;*s&&x<118;++s,x+=6){const auto* g=glyph(*s);
  for(int col=0;col<5;++col)for(int row=0;row<7;++row)
   if(g[col]&(1u<<row))pixel(x+col+(6-row)/3,y+row);
 }
}
const uint8_t tiny_digits[10][5]={{7,5,5,5,7},{2,6,2,2,7},
 {7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},
 {7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}};
const uint8_t* tiny_glyph(char c){
 if(c>='0'&&c<='9')return tiny_digits[c-'0'];
 static const uint8_t p[5]={6,5,6,4,4},r[5]={6,5,6,5,5};
 static const uint8_t e[5]={7,4,6,4,7},c_g[5]={7,4,4,4,7};
 static const uint8_t i[5]={7,2,2,2,7},s_g[5]={7,4,7,1,7};
 static const uint8_t o[5]={7,5,5,5,7},n_g[5]={5,7,7,7,5};
 static const uint8_t lower_n_g[5]={0,6,5,5,5},lower_s_g[5]={0,3,4,3,6};
 switch(c){case 'P':return p;case 'R':return r;case 'E':return e;
 case 'C':return c_g;case 'I':return i;case 'S':return s_g;
 case 'O':return o;case 'N':return n_g;case 'n':return lower_n_g;
 case 's':return lower_s_g;default:return nullptr;}
}
void tiny_text(int x,int y,const char* s){
 for(;*s&&x<66;++s,x+=4){
  const uint8_t* rows=tiny_glyph(*s);if(!rows)continue;
  for(int row=0;row<5;++row)for(int col=0;col<3;++col)
   if(rows[row]&(1u<<(2-col)))pixel(x+col,y+row);
 }
}
void field(int y,const char* label,const char* value){
 text(2,y,label);text(66,y,value);
}
void short_num(char* out,size_t n,uint32_t v){
 if(v==UINT32_MAX)std::snprintf(out,n,"--");
 else if(v<10000)std::snprintf(out,n,"%lu",(unsigned long)v);
 else if(v<10000000)std::snprintf(out,n,"%luK",(unsigned long)((v+500)/1000));
 else std::snprintf(out,n,"%luM",(unsigned long)((v+500000)/1000000));
}
void rate(char* out,size_t n,int32_t mppb){
 const int64_t mag=mppb<0?-(int64_t)mppb:mppb;
 std::snprintf(out,n,"%c%ld.%03ld",mppb<0?'-':'+',
               (long)(mag/1000),(long)(mag%1000));
}
uint8_t indicator_bits(bool pps,bool d1,bool d2){
 return uint8_t((pps?1:0)|(d1?2:0)|(d2?4:0));
}
void indicator_box(uint8_t mask){
 // The 8x16 corner is reserved for one box and three 4x4 lamps.
 for(int page=0;page<2;++page)for(int x=120;x<128;++x)
  fb[page*128+x]=0;
 for(int x=120;x<128;++x){pixel(x,0);pixel(x,15);}
 for(int y=0;y<16;++y){pixel(120,y);pixel(127,y);}
 const int rows[3]={1,6,11};
 for(int dot=0;dot<3;++dot)if(mask&(1u<<dot))
  for(int y=rows[dot];y<rows[dot]+4;++y)
   for(int x=122;x<126;++x)pixel(x,y);
}
void loss(char* out,size_t n,uint32_t gaps,uint32_t expected){
 if(!expected){std::snprintf(out,n,"WAIT");return;}
 const uint64_t tenths=uint64_t(gaps)*1000/expected;
 std::snprintf(out,n,"%lu.%01lu%%",(unsigned long)(tenths/10),
               (unsigned long)(tenths%10));
}
const char* mesh_role(uint8_t role){
 static const char* const names[]={"WAIT","CAND","GM","FOLLOW"};
 return names[std::min<unsigned>(role,3)];
}

int64_t shown_clock(const DisplayState& s){
 int64_t shown=int64_t(s.utc)*1000000;
#ifdef R2_TZ_WIRE
 if(s.timing_source!=DisplayState::Lora&&s.timing_source!=DisplayState::Rc9)
  (void)v12::clock_zone(s.tz,s.tz_until_us,s.tz_sampled_us,int64_t(s.utc)*1000000,shown);
#endif
 return shown/1000000;
}
[[maybe_unused]] const char* shown_zone(const DisplayState& s){
#ifdef R2_TZ_WIRE
 int64_t shown=0;
 if(s.timing_source!=DisplayState::Lora&&s.timing_source!=DisplayState::Rc9)
  return v12::clock_zone(s.tz,s.tz_until_us,s.tz_sampled_us,int64_t(s.utc)*1000000,shown);
#endif
 return "UTC";
}
void header(const DisplayState& s){
 // The green band carries selected source/state and the page title/date or UTC.
 const char* source=s.timing_source==DisplayState::Rc9?"RC9":s.timing_source==DisplayState::Lora?"LORA":
                    s.timing_source==DisplayState::Ble?"BLE":s.timing_source==DisplayState::Pts?"PTS":s.timing_source==DisplayState::Gps?"GPS":"--";
 if(s.mesh_profile)source="MESH";
 char copy_source[8];if(!s.mesh_profile && s.copy_profile && s.copy_channel>=37 && s.copy_channel<=39){std::snprintf(copy_source,sizeof(copy_source),"BLE%u",unsigned(s.copy_channel));source=copy_source;}
 const char* role=s.role;
 if(!std::strcmp(role,"HOLDOVER"))role="HOLD";
 else if(!std::strcmp(role,"ACQUIRE"))role="WAIT";
 else if(!std::strcmp(role,"GM TEST"))role="GMASTER";
 char state[28];std::snprintf(state,sizeof(state),"%s %s %s",
                             s.unit_label,source,role);
 if(s.pts_available && (s.page==display_pages::PtsFtm || s.page==display_pages::PtsRx))
  std::snprintf(state,sizeof(state),"%s %s/%s",s.unit_label,mesh_role(s.pts_gm_role),source);
 if(s.copy_profile && s.ui_role.header[0]) {
  int x=1;const auto& r=s.ui_role;
  if(r.marker_depth){
   for(int xx=0;xx<9;++xx)for(int yy=0;yy<8;++yy)if(r.marker_solid || !xx || xx==8 || !yy || yy==7)pixel(x+xx,yy);
   const uint8_t* g=tiny_digits[r.marker_depth];for(int row=0;row<5;++row)for(int col=0;col<3;++col)if(g[row]&(1u<<(2-col))){
    const int xx=x+3+col,yy=1+row;if(r.marker_solid)fb[(yy/8)*128+xx]&=uint8_t(~(1u<<(yy&7)));else pixel(xx,yy);
   }
   x+=11;
  }text(x,0,r.header);
 }else if(s.r2_dashboard){
  char followed[24];
  std::snprintf(followed,sizeof(followed),"%s > %s",s.unit_label,s.followed_clock);
  text(1,0,followed);
  text(111,0,s.r2_mode_pending?">":s.r2_relay_only?"R":"A");
 }else text(1,0,state);
 // Three lamps: disciplined clock phase, EVENT receipt, completed copy.
 // The T096 source PPS lamp has its separate actual-capture producer.
 indicator_box(indicator_bits(s.pps_blink,s.d1_rx,s.d2_rx));
 static const char* const titles[display_pages::Count]={"LOCKING","REL PREC","HOLD/GM",
  "CLK PPB","BLE RX","PTS RX","PKT LOSS","RX QUAL","TIME NET","LORA RX",
  "SOURCES","C4 DIAG","PTS FTM","SYSTEM","GPS / PPS","PRECISION"};
 const unsigned page=s.page%display_pages::Count;
 if(s.r2_dashboard && !page&&(s.timing_source==DisplayState::Lora||s.timing_source==DisplayState::Rc9)){
  text(1,8,s.timing_source==DisplayState::Rc9?"RC9 UTC":"LORA UTC");text(65,8,s.valid?"LIVE":"WAIT RX");
 }
 else if(s.r2_dashboard && !page){
  italic_text(1,8,"Sub-1us");
  text(45,8,s.r2_relative_lock?(s.utc_holdover?" HOLD":" LOCK"):
#ifdef R2_TZ_WIRE
    " ACQUIRE");
#else
    " ACQUIRING");
#endif
 }
 else if(s.copy_profile && !page)text(1,8,s.valid?(s.gps_source?"GPS UTC":"UTC"):"WAIT UTC");
 else if(s.basic_profile && !page)text(1,8,"BASIC UTC");
 else if(s.basic_profile){
  static const char* const basic_titles[display_pages::Count]={"BASIC","REL TIME","HOLD/GATE","LOCAL CLK","BLE RX","PTS","PACKETS","RX QUAL","NETWORK","LORA","SOURCES","DIAG","PTS FTM","SYSTEM","GPS / PPS","PRECISION"};
  text(1,8,basic_titles[page]);
 }
 else if(!page && s.timing_source==DisplayState::Pts)
  text(1,8,s.fit && s.pts_ready && s.precision_ns<=5000?"5us LOCK":"5us TARGET");
 else if(!page && s.timing_source==DisplayState::Lora && s.lora_profile==2)
  text(1,8,"UTC UNCAL");
 else if(!page && s.fit && s.rms_ns<1000){
  char precision[24];std::snprintf(precision,sizeof(precision),"%luns PRECISION",
                                 (unsigned long)s.rms_ns);
  tiny_text(1,9,precision);
 }else text(1,8,titles[page]);
 char clock[12];
 std::snprintf(clock,sizeof(clock),"%s",page?"--:--:--":"--/--/--");
 if(s.valid){time_t t=page?s.utc:shown_clock(s);struct tm tm{};gmtime_r(&t,&tm);
  if(page)std::snprintf(clock,sizeof(clock),"%02d:%02d:%02d",
                         tm.tm_hour,tm.tm_min,tm.tm_sec);
  else if(s.copy_profile && s.copy_local)std::snprintf(clock,sizeof(clock),"ELAPSED");
  else std::strftime(clock,sizeof(clock),"%m/%d/%y",&tm);}
#ifdef R2_TZ_WIRE
 if(s.r2_dashboard&&!page&&s.timing_source!=DisplayState::Lora&&s.timing_source!=DisplayState::Rc9)text(104,8,shown_zone(s));
 else
#endif
 if(!(s.r2_dashboard&&!page))text(70,8,clock);
}
void analog(uint32_t utc){
 constexpr int cx=112,cy=37,r=12;
 circle(cx,cy,r);circle(cx,cy,r-1);
 for(int i=0;i<12;++i){double a=double(i)*3.141592653589793/6;
  int x=int(std::lround(std::sin(a)*9)),y=int(std::lround(std::cos(a)*9));
  pixel(cx+x,cy-y);}
 const double second=(utc%60)*3.141592653589793/30;
 const double minute=((utc/60)%60)*3.141592653589793/30;
 const double hour=(((utc/3600)%12)+((utc/60)%60)/60.0)*3.141592653589793/6;
 line(cx,cy,cx+int(std::lround(std::sin(hour)*5)),cy-int(std::lround(std::cos(hour)*5)));
 line(cx,cy,cx+int(std::lround(std::sin(minute)*7)),cy-int(std::lround(std::cos(minute)*7)));
 line(cx,cy,cx+int(std::lround(std::sin(second)*10)),cy-int(std::lround(std::cos(second)*10)));
 circle(cx,cy,1);
}
void page_dashboard(const DisplayState& s){
 if(s.valid){time_t t=shown_clock(s);struct tm tm{};gmtime_r(&t,&tm);
  char hhmmss[12];std::snprintf(hhmmss,sizeof(hhmmss),"%02d:%02d:%02d",
                                tm.tm_hour,tm.tm_min,tm.tm_sec);
  text(2,21,hhmmss,2);
  if(!s.r2_dashboard||s.fit)analog(uint32_t((shown_clock(s)%86400+86400)%86400));
 }else{text(2,21,"--:--:--",2);circle(112,37,12);text(100,34,"WAIT");}
 char footer[32];
 if(s.r2_dashboard){
#ifdef PTOB_W12_DUAL_UI
  if(s.timing_source==DisplayState::Lora&&s.rc9_serving)std::snprintf(footer,sizeof footer,"LORA RC9 %u CLIENT",unsigned(s.rc9_clients));
  else
#endif
  if(s.r2_relative_lock)
   std::snprintf(footer,sizeof(footer),"\1 %02llu:%02u %s %s",(unsigned long long)(s.r2_lock_seconds/60),unsigned(s.r2_lock_seconds%60),s.r2_connection,s.r2_root_clock);
  else if(!std::strcmp(s.role,"FOLLOW") || !std::strcmp(s.role,"HOLD"))
   std::snprintf(footer,sizeof(footer),"%s %s %s",s.role,s.r2_connection,s.r2_root_clock);
  else std::snprintf(footer,sizeof(footer),"%s%s",s.role,s.r2_mode_pending?" >":s.r2_relay_only?" RELAYS":"");
 }
 else if(s.gps_source)std::snprintf(footer,sizeof(footer),"GPS %s C%u",s.ui_role.grandmaster?"SERVING":"READY",unsigned(s.gps_class));
 else if(s.mesh_profile){
  std::snprintf(footer,sizeof(footer),"%s%uH AGE %luMS",!s.valid?"STALE ":s.ui_role.held?"HOLD ":"",unsigned(s.mesh_depth),(unsigned long)s.mesh_age_ms);
 }
 else if(s.copy_profile){
  if(!s.ble_rx_enabled)std::snprintf(footer,sizeof(footer),"BLE OFF");
  else if(s.hold_1us_ms){char duration[12];format_hold_time(duration,sizeof(duration),s.hold_1us_ms);std::snprintf(footer,sizeof(footer),"P%u EST %s",s.hold_percentile,duration);}
  else if(!s.valid)std::snprintf(footer,sizeof(footer),"%s",s.copy_stale?"DIRECT TIME STALE":"WAIT COPY EPOCH");
  else std::snprintf(footer,sizeof(footer),"P%u TIMER ACQ",s.hold_percentile);
 }
 else if(s.basic_profile)std::snprintf(footer,sizeof(footer),"COARSE UTC");
 else format_hold_status(footer,sizeof(footer),s.hold_1us_ms,s.fit,s.hold_age_s,
                    s.hold_precision_ns,s.hold_trials,s.hold_animation,s.hold_estimated,s.hold_percentile,s.hold_audit_required,s.hold_audit_samples);
 text(2,45,footer);
 if(s.r2_dashboard){
#ifdef PTOB_W12_DUAL_UI
  if(s.timing_source==DisplayState::Lora&&s.rms_ns!=UINT32_MAX)std::snprintf(footer,sizeof footer,"RMS %luns N:%u",(unsigned long)s.rms_ns,unsigned(s.lora_link_samples));
  else
#endif
  if(s.shadow_holdover){
   char rms[12],p99[12];
   auto error=[](char* out,unsigned capacity,uint64_t value){
    if(value==UINT64_MAX){std::snprintf(out,capacity,"--");return;}
    const char* units[]={"ns","us","ms","s"};unsigned unit=0;uint64_t divisor=1;
    while(unit<3&&(value/divisor+unsigned(value%divisor!=0))>=1000){++unit;divisor*=1000;}
    const auto rounded=value/divisor+unsigned(value%divisor!=0);
    if(rounded>999)std::snprintf(out,capacity,">999s");else std::snprintf(out,capacity,"%llu%s",(unsigned long long)rounded,units[unit]);
   };
   error(rms,sizeof rms,s.rms_ns==UINT32_MAX?UINT64_MAX:s.rms_ns);error(p99,sizeof p99,s.shadow_p99_ns);
   std::snprintf(footer,sizeof footer,"RMS %s P99:%s",rms,p99);
  }else if(s.rms_ns!=UINT32_MAX&&s.samples){
   char duration[12];
   if(s.hold_estimated&&s.hold_1us_ms)format_hold_time(duration,sizeof(duration),s.hold_1us_ms);else std::snprintf(duration,sizeof(duration),s.hold_estimated?"0ms":"--");
   const uint32_t divisor=s.rms_ns>=1000000?1000000:s.rms_ns>=1000?1000:1;
   std::snprintf(footer,sizeof(footer),"RMS %lu%s H:%s",(unsigned long)((uint64_t(s.rms_ns)+divisor-1)/divisor),divisor==1?"ns":divisor==1000?"us":"ms",duration);
  }else if(s.hold_1us_ms>=1&&s.hold_trials>=100){
   char duration[12];format_hold_time(duration,sizeof(duration),s.hold_1us_ms);
   std::snprintf(footer,sizeof(footer),"P99 1us %s",duration);
  }else if(s.rms_ns!=UINT32_MAX)std::snprintf(footer,sizeof(footer),"RMS %luns",(unsigned long)s.rms_ns);
  else std::snprintf(footer,sizeof(footer),"RMS --");
 }
 else if(s.mesh_profile || s.gps_source)std::snprintf(footer,sizeof(footer),"BOUND UNKNOWN");
 else if(s.copy_profile){
  if(!s.ble_rx_enabled)std::snprintf(footer,sizeof(footer),"P%u CAUSAL --",s.hold_percentile);
  else if(s.copy_stale)std::snprintf(footer,sizeof(footer),"P%u CAUSAL STALE",s.hold_percentile);
  else if(s.precision_ns==UINT32_MAX)std::snprintf(footer,sizeof(footer),"P%u CAUSAL ACQUIRING",s.hold_percentile);
  else std::snprintf(footer,sizeof(footer),"P%u CAUSAL %luns",s.hold_percentile,(unsigned long)s.precision_ns);
 }else if(s.basic_profile){
  std::snprintf(footer,sizeof(footer),"BASIC / RF UNKNOWN");
 }else if(s.utc_holdover){
  std::snprintf(footer,sizeof(footer),"UTC HOLDOVER");
 }else if(s.timing_source==DisplayState::Pts){
  if(s.precision_ns==UINT32_MAX)
   std::snprintf(footer,sizeof(footer),"PTS LIVE / UNCAL");
  else if(s.precision_ns<1000)
   std::snprintf(footer,sizeof(footer),"REL %luns %s",(unsigned long)s.precision_ns,s.pts_holdover?"UP HOLD":"LIVE");
  else if(s.precision_ns<1000000)
   std::snprintf(footer,sizeof(footer),"REL %luus %s",(unsigned long)((s.precision_ns+999)/1000),s.pts_holdover?"UP HOLD":"LIVE");
  else
   std::snprintf(footer,sizeof(footer),"REL %lums %s",(unsigned long)((uint64_t(s.precision_ns)+999999)/1000000),s.pts_holdover?"UP HOLD":"LIVE");
 }else if(s.timing_source==DisplayState::Lora && s.lora_profile==2){
  std::snprintf(footer,sizeof(footer),"BLE-DERIVED UNCAL");
 }else if(s.fit && s.rms_ns<1000){
  std::snprintf(footer,sizeof(footer),"20Mhz / %luns Clock",(unsigned long)s.rms_ns);
 }else if(s.pts_enabled && s.ftm_peers){
  std::snprintf(footer,sizeof(footer),"PTS %s / %u CLOCKS",mesh_role(s.pts_gm_role),unsigned(s.ftm_peers+1));
 }else if(s.fit){
  const int64_t mag=s.resid_mppb<0?-(int64_t)s.resid_mppb:s.resid_mppb;
  const int64_t rounded=(mag+5)/10;
  std::snprintf(footer,sizeof(footer),"RESID %c%ld.%02ld PPB",
      s.resid_mppb<0?'-':'+',(long)(rounded/100),(long)(rounded%100));
 }else std::snprintf(footer,sizeof(footer),"RESID WAIT");
 text(2,56,footer);
}
void page_ble(const DisplayState& s){
 char n[24];std::snprintf(n,sizeof(n),"%08lX",(unsigned long)s.source_id);field(17,"SOURCE",s.source_id?n:"--");
 if(s.copy_profile){
  short_num(n,sizeof(n),s.beats);field(26,"EVENT RX",n);
  short_num(n,sizeof(n),s.reports);field(35,"META RX",n);
  short_num(n,sizeof(n),s.corrected);field(44,"PAIRED",n);
 }else{char a[8],b[8];short_num(a,sizeof(a),s.beats);short_num(b,sizeof(b),s.reports);std::snprintf(n,sizeof(n),"%s/%s",a,b);field(26,"D1/D2",n);short_num(n,sizeof(n),s.corrected);field(35,"MATCH",n);short_num(n,sizeof(n),s.map_max_us);field(44,"MAP US",n);}
 field(53,"BLE RX",s.ble_rx_enabled?s.scan?"SCANNING":"STANDBY":"OFF");
}
[[maybe_unused]] void page_pts(const DisplayState& s){
 char n[24];
 if(s.copy_profile){field(17,"PTS","OFF");field(26,"WIFI","OFF");field(35,"BLE RX",s.ble_rx_enabled?"ON":"OFF");return;}
 field(17,"SERVICE",!s.pts_available?"NOT BUILT":s.pts_enabled?"ON":"OFF");
 field(26,"ROOT",!s.pts_enabled?"OFF":mesh_role(s.pts_gm_role));
 short_num(n,sizeof(n),s.pts_precision_ns);field(35,"REL NS",s.pts_enabled?n:"--");
 short_num(n,sizeof(n),s.pts_age_s);field(44,"AGE S",s.pts_enabled?n:"--");
 if(s.pts_available){
  if(s.pts_enabled)std::snprintf(n,sizeof(n),"CH3 %u FTM PEER%s",unsigned(s.ftm_peers),s.ftm_peers==1?"":"S");
  else std::snprintf(n,sizeof(n),"HOLD 1.5S: ON");
  text(2,53,n);
 }
}
[[maybe_unused]] void page_pts_ftm(const DisplayState& s){
 char n[32],a[8],b[8],age[8];
 if(s.copy_profile){field(17,"PTS FTM","OFF");field(26,"RTT","--");field(35,"PEERS","--");return;}
 std::snprintf(n,sizeof(n),"MESH %s %uCLK",s.pts_enabled?mesh_role(s.pts_gm_role):"OFF",unsigned(s.ftm_peers+(s.pts_enabled?1:0)));
 text(2,17,n);
 short_num(a,sizeof(a),s.ftm_rtt_ns);short_num(age,sizeof(age),s.ftm_age_s);
 if(!s.pts_enabled || !s.ftm_enabled)std::snprintf(n,sizeof(n),"FTM OFF");
 else if(s.ftm_active)std::snprintf(n,sizeof(n),"FTM RANGING");
 else if(s.ftm_ranging_valid)std::snprintf(n,sizeof(n),"%s %sNS %uF A%sS",s.ftm_from_peer?"PEER":"LAST",a,unsigned(s.ftm_samples),age);
 else if(s.ftm_status==3 || s.ftm_status==2)std::snprintf(n,sizeof(n),"FTM %s A%sS",s.ftm_status==2?"TIMEOUT":"REJECT",age);
 else if(!s.ftm_root_native && s.pts_gm_role==3)std::snprintf(n,sizeof(n),"GM NO FTM");
 else std::snprintf(n,sizeof(n),"FTM WAIT PEER");
 text(2,26,n);
 short_num(a,sizeof(a),s.pts_gm_precision_ns);short_num(b,sizeof(b),s.pts_precision_ns);
 std::snprintf(n,sizeof(n),"GM %s RX %s NS",a,b);text(2,35,n);
 short_num(a,sizeof(a),s.pts_map_ns);
 std::snprintf(n,sizeof(n),"AP MAP %sNS UTC --",a);text(2,44,n);
 text(2,53,"FTM OFFSET UNCAL");
}
void page_packets(const DisplayState& s){
 char n[24];
 if(s.copy_profile){
  short_num(n,sizeof(n),s.beats);field(17,"EVENT RX",n);
  short_num(n,sizeof(n),s.reports);field(26,"META RX",n);
  short_num(n,sizeof(n),s.corrected);field(35,"PAIRED",n);
  short_num(n,sizeof(n),s.no_stamp);field(44,"NO STAMP",n);
  short_num(n,sizeof(n),s.expired);field(53,"EXPIRED",n);return;
 }
 loss(n,sizeof(n),s.sequence_gaps,s.sequence_expected);field(17,"D1 GAP",n);
 loss(n,sizeof(n),s.report_gaps,s.report_expected);field(26,"D2 GAP",n);
 std::snprintf(n,sizeof(n),"%lu/%lu",(unsigned long)s.sequence_gaps,(unsigned long)s.report_gaps);field(35,"MISS 1/2",n);
 short_num(n,sizeof(n),s.no_stamp);field(44,"NO RX TS",n);short_num(n,sizeof(n),s.sequence_expected);field(53,"D1 EXP",n);
}
void page_quality(const DisplayState& s){
 char n[20];short_num(n,sizeof(n),s.rejected);field(17,"REJECT",n);
 short_num(n,sizeof(n),s.duplicates);field(26,"DUP",n);
 short_num(n,sizeof(n),s.ambiguous);field(35,"AMBIG",n);
 short_num(n,sizeof(n),s.expired);field(44,"EXPIRED",n);
 short_num(n,sizeof(n),s.map_max_us);field(53,"MAP US",n);
}
void page_clock(const DisplayState& s){
 char n[24];rate(n,sizeof(n),s.drift_mppb);field(17,"DRIFT",s.fit?n:"WAIT");
 rate(n,sizeof(n),s.trim_mppb);field(26,"TRIM",s.fit?n:"WAIT");
 rate(n,sizeof(n),s.resid_mppb);field(35,"RESID",s.fit?n:"WAIT");
 short_num(n,sizeof(n),s.precision_ns);
 field(44,"REL NS",s.fit?n:"WAIT");
 short_num(n,sizeof(n),s.rms_ns);field(53,"FIT RMS",s.fit?n:"WAIT");
}
void page_hold(const DisplayState& s){
 char n[24];
 if(s.r2_dashboard){
  if(s.shadow_holdover){
   std::snprintf(n,sizeof n,s.holdover_selected?"BLE HOLD %lus":"SHADOW %lus / 120s",(unsigned long)(s.shadow_age_ms/1000));text(2,17,n);
   if(s.shadow_p99_ns!=UINT64_MAX)std::snprintf(n,sizeof n,"%lluns",(unsigned long long)s.shadow_p99_ns);else std::snprintf(n,sizeof n,"WARMUP");
   field(26,"P99",n);std::snprintf(n,sizeof n,"%lu/100",(unsigned long)s.shadow_samples);field(35,"SAMPLES",n);
   format_hold_time(n,sizeof n,s.shadow_cross_ms?s.shadow_cross_ms:s.shadow_safe_ms);
   field(44,s.shadow_cross_ms?"1us HIT":"SUB 1us",n);
   if(s.shadow_gaps){std::snprintf(n,sizeof n,"BLE GAPS %lu",(unsigned long)s.shadow_gaps);text(2,53,n);}
   else {text(2,53,s.holdover_production?(s.holdover_selected?"PRODUCTION HOLD":"VALIDATION / FOLLOW"):(s.shadow_holding?"BLE SHADOW RUNNING":"SHADOW PAUSED"));}
   return;
  }
  short_num(n,sizeof(n),s.rms_ns);field(17,"CAUSAL NS",n);
  short_num(n,sizeof(n),s.samples);field(26,"SAMPLES",n);
  short_num(n,sizeof(n),s.relative_prediction_ns);field(35,"REL EST NS",n);
  if(s.hold_estimated&&s.hold_1us_ms)format_hold_time(n,sizeof(n),s.hold_1us_ms);else std::snprintf(n,sizeof(n),s.hold_estimated?"0ms":"ACQUIRING");field(44,"1us EST",n);
  text(2,53,"UTC / RF UNCAL");return;
 }
 if(s.copy_profile){
  text(2,17,"1us SHADOW / EST");
  if(s.copy_hold_p95_ms)format_hold_time(n,sizeof(n),s.copy_hold_p95_ms);else std::snprintf(n,sizeof(n),"ACQUIRING");field(26,"P95",n);
  if(s.copy_hold_p99_ms)format_hold_time(n,sizeof(n),s.copy_hold_p99_ms);else std::snprintf(n,sizeof(n),"ACQUIRING");field(35,"P99",n);
  short_num(n,sizeof(n),s.hold_trials);field(44,"TRIALS",n);
  short_num(n,sizeof(n),s.age_s);field(53,"AGE S",n);return;
 }short_num(n,sizeof(n),s.age_s);field(17,"AGE S",n);
 short_num(n,sizeof(n),s.uncertainty_us);
 field(26,(s.timing_source==DisplayState::Lora || s.timing_source==DisplayState::Pts)?"UTC":"EST US",
       (s.timing_source==DisplayState::Lora || s.timing_source==DisplayState::Pts)?"UNCAL":s.fit?n:"WAIT");
 short_num(n,sizeof(n),s.rms_ns);field(35,"RMS NS",s.fit?n:"WAIT");
 // Test election is allowed, but an RF ADDRESS TX bound is still unknown.
 field(44,s.pts_available?"PTS ROLE":"BLE GM",s.pts_available?mesh_role(s.pts_gm_role):s.gm_trial?"READY UNC":"WAIT UNC");
 format_hold_status(n,sizeof(n),s.hold_1us_ms,s.fit,s.hold_age_s,
                    s.hold_precision_ns,s.hold_trials,s.hold_animation,s.hold_estimated,s.hold_percentile,s.hold_audit_required,s.hold_audit_samples);text(2,53,n);
}
[[maybe_unused]] void page_network(const DisplayState& s){
 char n[24];std::snprintf(n,sizeof(n),"%s",ptob_ui_name(s.root_id));field(17,"ROOT",s.root_id?n:"WAIT");
 if(s.copy_profile){
  std::snprintf(n,sizeof(n),"%s",ptob_ui_name(s.source_id));field(26,"PROVIDER",s.source_id?n:"WAIT");
  std::snprintf(n,sizeof(n),"%u RELAYS / %u LINKS",unsigned(s.mesh_depth),unsigned(s.mesh_depth+1));field(35,"PATH",n);
  std::snprintf(n,sizeof(n),"%luMS",(unsigned long)s.mesh_age_ms);field(44,"AGE",n);field(53,"OUTPUT",s.mesh_serving?"SERVING UNC":"BOUND UNKNOWN");return;
 }
 std::snprintf(n,sizeof(n),"%lu/%lu",(unsigned long)s.e0_valid,(unsigned long)s.e0_seen);field(26,"E0 OK/RX",n);
 if(s.rtt_status&1)std::snprintf(n,sizeof(n),"%ld",(long)s.rtt_offset_ns);
 else std::snprintf(n,sizeof(n),"WAIT");
 field(35,"RTT OFF",n);
 short_num(n,sizeof(n),s.rtt_age_s);
 char status[48];std::snprintf(status,sizeof(status),"S%u A%sS",
                    unsigned(s.rtt_status),n);
 field(44,"RTT",status);
 field(53,"TX EDGE","UNC");
}
[[maybe_unused]] void page_lora(const DisplayState& s){
 if(s.copy_profile){field(17,"LORA","OFF");field(26,"BLE RX",s.ble_rx_enabled?"ON":"OFF");return;}
 if(s.boundary){
  char line[40];
  if(!s.boundary_enabled)std::snprintf(line,sizeof(line),"TX DISABLED");
  else if(s.boundary_fault)std::snprintf(line,sizeof(line),"TX RADIO FAULT");
  else if(!s.boundary_air_confirmed)
   std::snprintf(line,sizeof(line),"TX ON TRY UNCAL");
  else std::snprintf(line,sizeof(line),"TX ON AIR UNCAL");
  text(2,17,line);
  if(s.boundary_tx_bound_ns==UINT32_MAX)
   std::snprintf(line,sizeof(line),"TX DONE %lu UNCAL",
                 (unsigned long)s.boundary_air_packets);
  else std::snprintf(line,sizeof(line),"LOCAL TX +/- %luUS",
    (unsigned long)((s.boundary_tx_bound_ns+999)/1000));
  text(2,26,line);
  std::snprintf(line,sizeof(line),"ROOT %08lX",(unsigned long)s.boundary_root);
  text(2,35,line);
  std::snprintf(line,sizeof(line),"SRC %08lX P%lu",(unsigned long)s.boundary_source,
                (unsigned long)(s.boundary_phase_us/1000));
  text(2,44,line);
  char age[9],skips[9],failures[9];
  if(s.boundary_age_ms==UINT32_MAX)std::snprintf(age,sizeof(age),"--");
  else short_num(age,sizeof(age),s.boundary_age_ms);
  short_num(skips,sizeof(skips),s.boundary_skips);
  short_num(failures,sizeof(failures),s.boundary_capture_failures);
  std::snprintf(line,sizeof(line),"A%s S%u K%s C%s",age,
                unsigned(s.boundary_sequence),skips,failures);
  text(2,53,line);return;
 }
 char timing[32];
 if(!s.lora_ready)std::snprintf(timing,sizeof(timing),"LORA WAIT UNCAL");
 else if(s.lora_local_us==UINT32_MAX)
  std::snprintf(timing,sizeof(timing),"LORA SW WAIT UNCAL");
 else if(s.lora_radio_timestamp)
  std::snprintf(timing,sizeof(timing),"RAD +/-%luUS UNCAL",
                    (unsigned long)s.lora_local_us);
 else std::snprintf(timing,sizeof(timing),"SW +/-%luMS UNCAL",
                    (unsigned long)((s.lora_local_us+999)/1000));
 text(2,17,timing);
 char n[48],a[9],b[9],c[9];
 std::snprintf(n,sizeof(n),"P%u SRC %08lX",unsigned(s.lora_profile),
               (unsigned long)s.lora_source);
 text(2,26,s.lora_packets?n:"SRC WAIT");
 short_num(a,sizeof(a),s.lora_matched);
 std::snprintf(n,sizeof(n),"SEQ %u OK %s",unsigned(s.lora_sequence),a);
 text(2,35,s.lora_packets?n:"SEQ WAIT");
 short_num(a,sizeof(a),s.lora_packets);
 short_num(b,sizeof(b),s.lora_missing);
 short_num(c,sizeof(c),s.lora_errors);
 std::snprintf(n,sizeof(n),"PKT %s G%s E%s",a,b,c);text(2,44,n);
 short_num(a,sizeof(a),s.lora_age_s);
 std::snprintf(n,sizeof(n),"A%s R%d S%+d",a,int(s.lora_rssi),
               int(std::lround(double(s.lora_snr_tenths)/10)));
 text(2,53,s.lora_packets?n:"A-- R-- S--");
}
[[maybe_unused]] void page_sources(const DisplayState& s){
 if(s.copy_profile){char n[24];
  field(17,"CLOCK",s.gps_source?"GPS":s.mesh_profile?"MESH":"BLE");
  field(26,"UTC",s.valid?"UNCAL":"WAIT");
  std::snprintf(n,sizeof(n),"%08lX",(unsigned long)s.source_id);field(35,"SOURCE",s.source_id?n:"--");
  short_num(n,sizeof(n),s.ble_age_s);field(44,"AGE S",n);
  field(53,"RX / TX",s.ble_rx_enabled?"ON / OFF":"OFF / OFF");return;
 }
 char line[64],a[12],off[12];
 format_hold_status(line,sizeof(line),s.hold_1us_ms,s.fit,s.hold_age_s,
                    s.hold_precision_ns,s.hold_trials,s.hold_animation,s.hold_estimated,s.hold_percentile,s.hold_audit_required,s.hold_audit_samples);
 text(2,17,line);
 short_num(a,sizeof(a),s.ble_precision_ns);
 if(s.ble_fit)std::snprintf(line,sizeof(line),"B P%sNS A%lu",a,(unsigned long)s.ble_age_s);
 else std::snprintf(line,sizeof(line),"BLE A-- O--");
 text(2,25,line);
 short_num(a,sizeof(a),s.lora_relative_ns);
 if(s.source_offset_valid){
  const int32_t magnitude=std::abs(s.source_offset_us);
  if(magnitude<1000)std::snprintf(off,sizeof(off),"%+ldUS",(long)s.source_offset_us);
  else std::snprintf(off,sizeof(off),"%+ldMS",(long)(s.source_offset_us/1000));
 }else std::snprintf(off,sizeof(off),"--");
 std::snprintf(line,sizeof(line),"L UNCAL R%s O%s",a,off);
 text(2,33,line);
 if(s.pts_offset_valid){
  if(s.pts_offset_us>-1000 && s.pts_offset_us<1000)
   std::snprintf(off,sizeof(off),"%+ldUS",(long)s.pts_offset_us);
  else std::snprintf(off,sizeof(off),"%+ldMS",(long)(s.pts_offset_us/1000));
  std::snprintf(line,sizeof(line),"P UNCAL O%s",off);
 }
 else std::snprintf(line,sizeof(line),"PTS UNCAL O--");
 text(2,41,line);
 short_num(a,sizeof(a),s.pts_precision_ns);
 if(s.pts_age_s!=UINT32_MAX)std::snprintf(line,sizeof(line),"P %s A%lu P%s",
     !s.pts_ready?"WAIT":s.pts_holdover?"HOLD":s.pts_fit?"FIT":"ACQ",(unsigned long)s.pts_age_s,a);
 else std::snprintf(line,sizeof(line),"P WAIT A-- R--");
 text(2,49,line);
 if(s.pts_available)std::snprintf(line,sizeof(line),"P %s %s",s.pts_enabled?"ON":"OFF",s.pts_window?"RX":"--");
 else std::snprintf(line,sizeof(line),"PTS NOT BUILT");
 text(2,57,line);
}
[[maybe_unused]] void page_diagnostics(const DisplayState& s){
 char n[48],a[12],b[12];
 short_num(a,sizeof(a),s.c4_age_s);
 std::snprintf(n,sizeof(n),"C4 %lu/%lu A%sS",(unsigned long)s.c4_valid,
               (unsigned long)s.c4_seen,a);text(2,17,n);
 short_num(a,sizeof(a),s.c4_rms_ns);
 if(s.c4_prediction_ns==INT32_MIN)std::snprintf(b,sizeof(b),"--");
 else std::snprintf(b,sizeof(b),"%+ld",(long)s.c4_prediction_ns);
 std::snprintf(n,sizeof(n),"PPS R%s E%s NS",a,b);text(2,26,n);
 short_num(a,sizeof(a),s.c4_peak_ns);
 short_num(b,sizeof(b),s.c4_slot_peak_ns);
 std::snprintf(n,sizeof(n),"PEAK %s SLOT %s",a,b);
 text(2,35,n);
 short_num(a,sizeof(a),s.c4_weighted);
 short_num(b,sizeof(b),s.ble_noise_ns);
 std::snprintf(n,sizeof(n),"W%s NOISE %sNS",a,b);
 text(2,44,n);
 short_num(a,sizeof(a),s.rtt_age_s);
 std::snprintf(n,sizeof(n),"RTT S%u A%sS",unsigned(s.rtt_status),a);
 text(2,53,n);
}
void basic_page(const DisplayState& s){
 char line[32],value[12];
 switch(s.page%display_pages::Count){
 case display_pages::Relative:
  if(s.copy_profile){short_num(value,sizeof(value),s.rms_ns);std::snprintf(line,sizeof(line),"EMP RMS %sNS",value);text(2,17,line);short_num(value,sizeof(value),s.relative_prediction_ns);std::snprintf(line,sizeof(line),"CAUSAL NEXT %sNS",value);text(2,26,line);text(2,35,"TOTAL BOUND UNKNOWN");text(2,44,"BIAS: UNCALIBRATED");text(2,53,"OUTPUT: NO INSTRUMENT");break;}
  text(2,17,"RF COPY: NO STAMP");text(2,26,"REL ERROR: UNKNOWN");
  text(2,35,"PREDICT: NO EVENTS");text(2,44,"BIAS/TAILS: NO SET");
  text(2,53,"NEED TX+RX COPY TS");break;
 case display_pages::Hold:
  text(2,17,"PTS DUAL: DISABLED");text(2,26,"500NS: UNQUALIFIED");
  text(2,35,"BLACKOUT: UNTESTED");text(2,44,"UTC BIAS: UNKNOWN");
  text(2,53,"BLE ONLY / 50% CAP");break;
 case display_pages::Clock:
  if(s.copy_profile){page_clock(s);break;}
  text(2,17,"GPTIMER 20MHZ XTAL");text(2,26,"CPU LOCK 240MHZ");
  text(2,35,"RF MAP: NO SOURCE");text(2,44,"DRIFT: NO RF FIT");
  text(2,53,"NO UTC FROM TICKS");break;
 case display_pages::BleRx:
  std::snprintf(line,sizeof(line),"SOURCE %08lX",(unsigned long)s.source_id);text(2,17,line);
  std::snprintf(line,sizeof(line),"SESSION %08lX",(unsigned long)s.basic_session);text(2,26,line);
  short_num(value,sizeof(value),s.ble_age_s);std::snprintf(line,sizeof(line),"FRESH AGE %sS",value);text(2,35,line);
  text(2,44,s.scan?"PASSIVE SCANNING":s.ble_rx_enabled?"WINDOW CLOSED":"BLE RX OFF");
  text(2,53,"HOLD GO: BLE ON/OFF");break;
 case display_pages::Packets:
  if(s.copy_profile){short_num(value,sizeof(value),s.beats);std::snprintf(line,sizeof(line),"EVENTS %s",value);text(2,17,line);short_num(value,sizeof(value),s.corrected);std::snprintf(line,sizeof(line),"MATCHED %s",value);text(2,26,line);short_num(value,sizeof(value),s.no_stamp);std::snprintf(line,sizeof(line),"NO CAPTURE %s",value);text(2,35,line);short_num(value,sizeof(value),s.expired);std::snprintf(line,sizeof(line),"EXPIRED %s",value);text(2,44,line);text(2,53,"EXACT COPY / CH37");break;}
  short_num(value,sizeof(value),s.basic_callbacks);std::snprintf(line,sizeof(line),"CALLBACKS %s",value);text(2,17,line);
  short_num(value,sizeof(value),s.beats);std::snprintf(line,sizeof(line),"NEW LOGICAL %s",value);text(2,26,line);
  short_num(value,sizeof(value),s.duplicates);std::snprintf(line,sizeof(line),"DUP COPIES %s",value);text(2,35,line);
  short_num(value,sizeof(value),s.basic_max_gap_ms);std::snprintf(line,sizeof(line),"MAX GAP %sMS",value);text(2,44,line);
  text(2,53,"COPY/CH: UNKNOWN");break;
 case display_pages::Quality:
  short_num(value,sizeof(value),s.basic_malformed);std::snprintf(line,sizeof(line),"MALFORMED %s",value);text(2,17,line);
  short_num(value,sizeof(value),s.basic_conflicts);std::snprintf(line,sizeof(line),"CONFLICTS %s",value);text(2,26,line);
  short_num(value,sizeof(value),s.basic_rejected);std::snprintf(line,sizeof(line),"REJECTED %s",value);text(2,35,line);
  short_num(value,sizeof(value),s.basic_queue_drops);std::snprintf(line,sizeof(line),"LOG DROPS %s",value);text(2,44,line);
  if(s.basic_callbacks){std::snprintf(line,sizeof(line),"LAST RSSI %d DBM",int(s.basic_rssi));}
  else {std::snprintf(line,sizeof(line),"LAST RSSI --");}
  text(2,53,line);break;
 case display_pages::Sources:
  if(s.copy_profile){text(2,17,"TX ADDRESS / RX DESC");text(2,26,s.copy_local?"SOURCE LOCAL DOMAIN":"POSIX LABEL / UNCAL");text(2,35,"DESC GRID 500NS");text(2,44,"FIXED RF BIAS UNKNOWN");text(2,53,"PRIVATE ABI / EXPERIMENT");break;}
  text(2,17,"PUBLIC SERVICE DATA");
  text(2,26,s.basic_local_reference?"ROOT ASSERTS LOCAL REF":"ROOT REF: UNKNOWN");
  text(2,35,"UTC: INTEGER SEC");text(2,44,"RF UNC: UNKNOWN");
  text(2,53,"TRUST: UNVERIFIED");break;
 case display_pages::PtsRx:case display_pages::PtsFtm:
  text(2,17,"PTS/WIFI: OFF");text(2,26,"GM: INELIGIBLE");
  text(2,35,"NO HOLDOVER GATE");text(2,44,"NO AUTH ROLE ACTIVE");
  text(2,53,"BLE FALLBACK ACTIVE");break;
 case display_pages::LoraRx:
  text(2,17,"LORA: OFF");text(2,26,"BLE ONLY PROFILE");
  text(2,35,"RF PREC: UNKNOWN");break;
 case display_pages::Network:
  text(2,17,"MESH/RELAY: OFF");text(2,26,"PUBLIC ACCESS: ON");
  text(2,35,"ROLE/TRUST: NO GATE");break;
 default:
  if(s.copy_profile){short_num(value,sizeof(value),s.copy_delay_us);std::snprintf(line,sizeof(line),"DELIVER %sUS",value);text(2,17,line);short_num(value,sizeof(value),s.copy_fault);std::snprintf(line,sizeof(line),"CAPTURE FAULT %s",value);text(2,26,line);short_num(value,sizeof(value),s.map_max_us);std::snprintf(line,sizeof(line),"MAP WIDTH %sUS",value);text(2,35,line);text(2,44,"RTT: NO EXCHANGE");text(2,53,"RF/CPU COST UNKNOWN");break;}
  short_num(value,sizeof(value),s.basic_scan_max_ms);std::snprintf(line,sizeof(line),"SCAN MAX %sMS",value);text(2,17,line);
  short_num(value,sizeof(value),s.basic_scan_violations);std::snprintf(line,sizeof(line),"CAP FAULTS %s",value);text(2,26,line);
  text(2,35,"WALL CAP 500MS/SEC");text(2,44,"RF DUTY: UNMEASURED");
  text(2,53,"CPU/ENERGY: UNKNOWN");break;
 }
}
#include "display_comparison.inc"
[[maybe_unused]] void page_copy_tails(const DisplayState& s){
 const bool current=!s.mesh_profile && !s.copy_stale && s.ble_rx_enabled;
 const uint32_t values[2]={current?s.copy_p95_ns:UINT32_MAX,current?s.copy_p99_ns:UINT32_MAX};
 const char* names[2]={"P95","P99"};
 for(unsigned i=0;i<2;++i){const int x=int(i)*64;
  line(x+2,17,x+61,17);line(x+2,52,x+61,52);line(x,19,x,50);line(x+63,19,x+63,50);
  text(x+5,19,names[i]);char n[16];short_num(n,sizeof(n),values[i]);
  const int w=int(std::strlen(n))*12-2;text(x+(64-w)/2,29,n,2);text(x+23,44,"ns");
 }
 char n[24];if(s.mesh_profile)std::snprintf(n,sizeof(n),"BOUNDARY: NO SAMPLES");else std::snprintf(n,sizeof(n),"%s / %lu EVENTS",current?"CAUSAL":"STALE",(unsigned long)s.copy_tail_samples);text(2,56,n);
}

#ifndef W12_DISPLAY_PREVIEW
bool command(uint8_t c){
 const uint8_t p[]={0,c};return i2c_master_transmit(oled,p,2,100)==ESP_OK;
}
bool send_data(const uint8_t* p,size_t n){
 uint8_t packet[129];packet[0]=0x40;
 std::memcpy(packet+1,p,n);
 return i2c_master_transmit(oled,packet,n+1,100)==ESP_OK;
}
bool init_i2c(){
 i2c_master_bus_config_t bc{};bc.clk_source=I2C_CLK_SRC_DEFAULT;
 bc.i2c_port=I2C_NUM_0;bc.sda_io_num=GPIO_NUM_17;bc.scl_io_num=GPIO_NUM_18;
 bc.glitch_ignore_cnt=7;bc.flags.enable_internal_pullup=true;
 const esp_err_t bus_rc=i2c_new_master_bus(&bc,&bus);
 if(bus_rc!=ESP_OK){std::printf("W12_DISPLAY i2c_bus=%d\n",bus_rc);return false;}
 uint8_t address=0x3c;
 esp_err_t rc=i2c_master_probe(bus,address,30);
 if(rc!=ESP_OK){address=0x3d;rc=i2c_master_probe(bus,address,30);}
 std::printf("W12_DISPLAY i2c_probe=%d address=%02x\n",rc,address);
 if(rc!=ESP_OK){i2c_del_master_bus(bus);bus=nullptr;return false;}
 i2c_device_config_t dc{};dc.dev_addr_length=I2C_ADDR_BIT_LEN_7;
 dc.device_address=address;dc.scl_speed_hz=400000;
 rc=i2c_master_bus_add_device(bus,&dc,&oled);
 if(rc!=ESP_OK){std::printf("W12_DISPLAY i2c_device=%d\n",rc);i2c_del_master_bus(bus);bus=nullptr;return false;}
 return true;
}
#endif
}

#ifndef W12_DISPLAY_PREVIEW
bool display_init(){
 if(!w12_board::display_verified){std::printf("W12_DISPLAY enabled=0 reason=UNVERIFIED_BOARD_PROFILE pins=UNTOUCHED\n");return true;}
 uint8_t factory[6]{};
 if(esp_read_mac(factory,ESP_MAC_EFUSE_FACTORY)!=ESP_OK || !w12_device::display_permitted(factory,w12_board::profile)){
  std::printf("W12_DISPLAY enabled=0 reason=BOUND_FACTORY_OR_BOARD_PROFILE_REFUSED pins=UNTOUCHED\n");return false;}
 std::printf("W12_DISPLAY begin\n");
 // SSD1315 I2C17/18. Meshnology W12 uses45LOW/reset47; the historical
 // Heltec R8 experiment used40HIGH/reset21. Other units retain their profile.
 gpio_config_t cfg{};cfg.pin_bit_mask=w12_board::display_pin_mask;
 cfg.mode=GPIO_MODE_OUTPUT;
 const esp_err_t gpio_rc=gpio_config(&cfg);
 if(gpio_rc!=ESP_OK){std::printf("W12_DISPLAY gpio=%d\n",gpio_rc);return false;}
 gpio_set_level(gpio_num_t(w12_board::vext),w12_board::vext_on);
 std::printf("W12_DISPLAY profile=%d vext=%d level=%d reset=%d sda=17 scl=18\n",w12_board::profile,w12_board::vext,w12_board::vext_on,w12_board::reset);
 vTaskDelay(pdMS_TO_TICKS(50));
 gpio_set_level(gpio_num_t(w12_board::reset),0);
 vTaskDelay(pdMS_TO_TICKS(10));
 gpio_set_level(gpio_num_t(w12_board::reset),1);
 vTaskDelay(pdMS_TO_TICKS(100));
 std::printf("W12_DISPLAY probe\n");
 if(!init_i2c())return false;
 std::printf("W12_DISPLAY interface=i2c scan=FAST clock=f0 bus_hz=400000 redraw_ms=500\n");
 const uint8_t init[]={0xae,0xd5,DisplayTiming::clock(DisplayTiming::Fast),0xa8,0x3f,0xd3,0,0x40,0x8d,0x14,
     0x20,0x02,0xa1,0xc8,0xda,0x12,0x81,0x60,0xd9,0xf1,0xdb,0x40,0xa4,0xa6,0x2e,0xaf};
 for(uint8_t c:init)if(!command(c)){
  std::printf("W12_DISPLAY command=%02x failed\n",c);return false;
 }
 return true;
}

bool display_set_scan(DisplayTiming::Profile profile){
 return oled && command(0xd5) && command(DisplayTiming::clock(profile));
}

void display_blank(){
 if(oled)command(0xae); // Keep controller/rail alive; synchronization continues.
}
void display_wake(){
 if(oled)command(0xaf);
 framebuffer_valid=false;
}

void display_sleep(){
 if(!w12_board::display_verified)return;
 if(oled)command(0xae); // SSD1315 display off before removing VEXT.
 gpio_set_level(gpio_num_t(w12_board::reset),0);
 gpio_set_level(gpio_num_t(w12_board::vext),!w12_board::vext_on);
 // The global deep-sleep GPIO hold must not leave I2C high and feed the
 // unpowered OLED through its input protection diodes.
 gpio_config_t bus_off{};
 bus_off.pin_bit_mask=(1ULL<<17)|(1ULL<<18);
 bus_off.mode=GPIO_MODE_OUTPUT;
 gpio_config(&bus_off);
 gpio_set_level(GPIO_NUM_17,0);
 gpio_set_level(GPIO_NUM_18,0);
}

#endif

void page_system(const DisplayState& s){
 field(17,"CLOCK",s.unit_label);field(26,"ROOT",ptob_ui_name(s.root_id));field(35,"VIA",ptob_ui_name(s.source_id));
 field(44,"MODE",s.gps_source?"GPS SOURCE":s.mesh_profile?"BOUNDARY":"DIRECT RX");field(53,"TX RF",s.gps_source || s.mesh_profile?"UNKNOWN":"RX ADDRESS");
}
void page_gps(const DisplayState& s){
 if(s.gps_compare){
  char n[24];field(17,"REF",s.gps_compare_fresh?"PPS UNQUAL":"WAIT GPS");
  if(!s.gps_compare_samples)std::snprintf(n,sizeof n,"--");
  else if(std::abs(int64_t(s.gps_compare_mean_ns))>=1000000)std::snprintf(n,sizeof n,"%+.1fMS",double(s.gps_compare_mean_ns)/1000000);
  else if(std::abs(int64_t(s.gps_compare_mean_ns))>=10000)std::snprintf(n,sizeof n,"%+.1fUS",double(s.gps_compare_mean_ns)/1000);
  else {std::snprintf(n,sizeof n,"%+ldNS",(long)s.gps_compare_mean_ns);}
  field(26,"MEAN",n);
  if(!s.gps_compare_samples)std::snprintf(n,sizeof n,"--");else if(s.gps_compare_rms_ns>=1000000)std::snprintf(n,sizeof n,"%.1fMS",double(s.gps_compare_rms_ns)/1000000);else if(s.gps_compare_rms_ns>=10000)std::snprintf(n,sizeof n,"%.1fUS",double(s.gps_compare_rms_ns)/1000);else std::snprintf(n,sizeof n,"%luNS",(unsigned long)s.gps_compare_rms_ns);field(35,"RMS",n);
  if(!s.gps_compare_samples)std::snprintf(n,sizeof n,"--");else if(s.gps_compare_max_ns>=1000000)std::snprintf(n,sizeof n,"%.1fMS",double(s.gps_compare_max_ns)/1000000);else if(s.gps_compare_max_ns>=10000)std::snprintf(n,sizeof n,"%.1fUS",double(s.gps_compare_max_ns)/1000);else std::snprintf(n,sizeof n,"%luNS",(unsigned long)s.gps_compare_max_ns);field(44,"MAX",n);
  std::snprintf(n,sizeof n,"%lu",(unsigned long)s.gps_compare_samples);field(53,"N",n);return;
 }

 if(!s.gps_inputs){field(17,"GPS","NO INPUT");field(26,"PPS","OPTIONAL");field(35,"CLOCK","BLE GM");field(44,"SOURCE","DISABLED");field(53,"BOUND","UNKNOWN");return;}
 char n[24];field(17,"GPS",!s.gps_valid?"WAIT FIX":s.gps_source?"SOURCE":"SHADOW");
 if(s.gps_pps_age_ms==UINT32_MAX)std::snprintf(n,sizeof(n),"--");else std::snprintf(n,sizeof(n),"%luMS",(unsigned long)s.gps_pps_age_ms);
 field(26,"PPS AGE",n);
 if(s.gps_raw_rms_ns==UINT32_MAX)std::snprintf(n,sizeof(n),"--");else std::snprintf(n,sizeof(n),"%luNS",(unsigned long)s.gps_raw_rms_ns);
 field(35,"RAW RMS",n);
 if(s.gps_mapped_rms_ns==UINT32_MAX)std::snprintf(n,sizeof(n),"--");else std::snprintf(n,sizeof(n),"%luNS",(unsigned long)s.gps_mapped_rms_ns);
 field(44,"MAP RMS",n);
 field(53,"BLE FIT","SEPARATE");
}
#ifdef PTOB_W12_DUAL_UI
void page_precision(const DisplayState& s){
 if(s.timing_source==DisplayState::Rc9){text(2,17,"RC9 FROM LORA");text(2,30,"UTC LIVE",2);text(2,48,"PRECISION UNKNOWN");text(2,57,"DIRECT CLIENT");return;}
 const bool lora=s.precision_lora;
 char n[28];text(2,17,lora?"LINK RMS":"SYNC RMS");text(86,17,lora?"50ns":s.precision_fine?"12.5ns":"50ns");
 if(s.precision_live&&s.rms_ns!=UINT32_MAX){
  const auto divisor=s.rms_ns>=1000000?1000000:s.rms_ns>=10000?1000:1;
  std::snprintf(n,sizeof n,"%lu %s",(unsigned long)(s.rms_ns/divisor),divisor==1?"ns":divisor==1000?"us":"ms");
 }else if(lora&&s.lora_time_valid)std::snprintf(n,sizeof n,"FIT %u/4",unsigned(s.lora_link_samples));
 else std::snprintf(n,sizeof n,lora?"WAIT RX":"-- ns");
 text(2,27,n,2);
 if(lora){
  if(s.lora_time_valid)std::snprintf(n,sizeof n,"LORA FIT %u/16",unsigned(s.lora_link_samples));
  else std::snprintf(n,sizeof n,"LORA NO LIVE FIT");
  text(2,45,n);
  text(2,55,"RX ONLY");text(92,55,s.precision_fine?"80MHz":"20MHz");return;
 }
 std::snprintf(n,sizeof n,"LOCK %02llu:%02u:%02u",(unsigned long long)(s.r2_lock_seconds/3600),unsigned(s.r2_lock_seconds/60%60),unsigned(s.r2_lock_seconds%60));text(2,45,n);
 if(s.precision_live)std::snprintf(n,sizeof n,"TRIM %+ldPPB",(long)s.precision_rate_ppb);else std::snprintf(n,sizeof n,"TRIM --PPB");text(2,55,n);
 text(92,55,s.precision_fine?"80MHz":"20MHz");
}
void page_w12_lora(const DisplayState& s){
 char n[28];const char* mode=s.lora_mode==1?"KEEP ON":s.lora_mode==2?"KEEP OFF":"AUTO";
 std::snprintf(n,sizeof n,"RX %s",mode);text(2,17,n);
 text(2,27,s.lora_fault?"FAULT":s.lora_active?"ON":"OFF",2);
 if(!s.lora_configured)text(44,31,"NO TX ID");
 else if(s.lora_time_valid)text(44,31,"UTC LIVE");
 else if(s.lora_transition_ms&&s.lora_mode==0&&!s.lora_fault){std::snprintf(n,sizeof n,"%lus",(unsigned long)((s.lora_transition_ms+999)/1000));text(72,31,n);}
 else if(!s.lora_fault)text(44,31,s.lora_active?"WAIT SYNC":"STANDBY");
 if(s.lora_time_valid&&s.lora_link_rms_ns!=UINT32_MAX){
  const auto divisor=s.lora_link_rms_ns>=1000000?1000000:s.lora_link_rms_ns>=1000?1000:1;
  std::snprintf(n,sizeof n,"RMS %lu%s N %u",(unsigned long)(s.lora_link_rms_ns/divisor),divisor==1?"ns":divisor==1000?"us":"ms",unsigned(s.lora_link_samples));
 }else if(s.lora_time_valid)std::snprintf(n,sizeof n,"FIT %u/4 RX %lu",unsigned(s.lora_link_samples),(unsigned long)s.lora_accepted);
 else std::snprintf(n,sizeof n,"RMS WAIT RX");
 text(2,45,n);
 if(s.rc9_clients&&s.lora_client_hold_ms){std::snprintf(n,sizeof n,"RC9 %u HOLD %lus",unsigned(s.rc9_clients),(unsigned long)((s.lora_client_hold_ms+999)/1000));text(2,55,n);}
 else text(2,55,s.rc9_serving?"RC9 AVAILABLE":"HOLD: RX MODE");
}
#endif
void display_show(const DisplayState& s){
#ifndef W12_DISPLAY_PREVIEW
 if(!oled)return;
#endif
 std::memset(fb,0,sizeof(fb));
 header(s);
#if defined(V12_R2_ENABLED) && V12_R2_ENABLED && V12_R2_CONFIG == 13
 constexpr bool g1_only=true;
#else
 constexpr bool g1_only=false;
#endif
 if(!g1_only && s.basic_profile && s.page%display_pages::Count!=display_pages::Front)basic_page(s);
 else switch(s.page%display_pages::Count){
  case display_pages::Front:page_dashboard(s);break;
  case display_pages::Relative:if constexpr(g1_only)page_hold(s);else {if(s.r2_dashboard)page_hold(s);else if(s.copy_profile)page_copy_tails(s);else page_comparison(s);}break;
  case display_pages::Hold:page_hold(s);break;
  case display_pages::Clock:page_clock(s);break;
  case display_pages::BleRx:page_ble(s);break;
  case display_pages::PtsRx:if constexpr(!g1_only)page_pts(s);break;
  case display_pages::PtsFtm:if constexpr(!g1_only)page_pts_ftm(s);break;
  case display_pages::Packets:page_packets(s);break;
  case display_pages::Quality:page_quality(s);break;
  case display_pages::Network:if constexpr(!g1_only)page_network(s);break;
  case display_pages::LoraRx:
#ifdef PTOB_W12_DUAL_UI
   page_w12_lora(s);
#else
   if constexpr(!g1_only)page_lora(s);
#endif
   break;
#ifdef PTOB_W12_DUAL_UI
  case display_pages::Precision:page_precision(s);break;
#endif
  case display_pages::Sources:if constexpr(!g1_only)page_sources(s);break;
  case display_pages::System:page_system(s);break;
  case display_pages::Gps:page_gps(s);break;
  default:if constexpr(g1_only)page_dashboard(s);else page_diagnostics(s);break;}

#ifndef W12_DISPLAY_PREVIEW
 if(!display_flush(fb,displayed_fb,framebuffer_valid,[](unsigned page,unsigned first,const uint8_t* data,unsigned n){
  return command(uint8_t(0xb0+page)) && command(uint8_t(first&15)) && command(uint8_t(0x10+(first>>4))) && send_data(data,n);
 })){
  if(++display_errors<=3 || display_errors%100==0)std::printf("W12_DISPLAY transfer_failed=%lu\n",(unsigned long)display_errors);
  return;
 }
 #endif
 indicator_mask=indicator_bits(s.pps_blink,s.d1_rx,s.d2_rx);
}

#ifndef W12_DISPLAY_PREVIEW
void display_update_indicators(bool pps,bool d1,bool d2){
 if(!oled || !framebuffer_valid)return;
 const uint8_t next=indicator_bits(pps,d1,d2);
 if(next==indicator_mask)return;
 indicator_box(next);
 // Only the eight reserved columns are sent, avoiding a full-frame transfer
 // for each pulse edge.
 for(int page=0;page<2;++page){
  if(!command(uint8_t(0xb0+page)) || !command(0x08) || !command(0x17) ||
     !send_data(fb+page*128+120,8)){framebuffer_valid=false;++display_errors;return;}
  std::memcpy(displayed_fb+page*128+120,fb+page*128+120,8);
 }
 indicator_mask=next;
}

#endif
