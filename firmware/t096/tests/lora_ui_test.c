/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <limits.h>
static void save(const char *dir,unsigned mode,struct r2_ui_model *m){
 static uint8_t fb[R2_UI_FRAME_BYTES],rgb[R2_UI_BYTES];struct r2_ui_status s={.page=mode>=90?0:R2_UI_LORA_PAGE,.screensaver=mode==95};
 r2_ui_render(fb,m,&s,1000);assert(r2_ui_expand_rgb565(rgb,sizeof rgb,fb,sizeof fb));
 if(mode==90||mode==91||mode==93||mode==94){
  unsigned dial=0;for(unsigned y=55;y<112;y++)for(unsigned x=5;x<75;x++)dial+=fb[y*80+x]==R2_UI_COLOR_LCD;
  assert(mode==90||mode==91?dial>1000:dial==0);
 }
 char path[512];snprintf(path,sizeof path,"%s/rc8-%u.ppm",dir,mode);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n80 160\n255\n");
 for(unsigned i=0;i<sizeof rgb;i+=2){unsigned p=((unsigned)rgb[i]<<8)|rgb[i+1];fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}fclose(f);
}
int main(int argc,char **argv){assert(argc==2);
 struct r2_ui_model m={.sampled_ms=1000,.platform_ready=true,.unit="TEST",.lora={.requested=true,.active=true,.state=RC8_AVAILABLE,.choice=RC8_SELECT_LORA,.utc_us=36642000000ULL,.sampled_us=1000000,.until_us=13000000,.root_tag=0x1234,.emitter_tag=0x5678,.age_ms=4000}};
 int64_t utc;bool coarse;assert(r2_ui_display_time(&m,1000,&utc,&coarse)&&coarse&&utc==36642000000LL);assert(r2_ui_analog(&m,1000));
 /* The explicit normal-source choice wins. RC8 cannot borrow a cached BLE
  * face permit or unrelated local coarse time while it is selected. */
 m.clock.valid=true;m.clock.utc_us=123;m.lora.choice=RC8_SELECT_BLE;
 assert(r2_ui_display_time(&m,1000,&utc,&coarse)&&utc==123&&!coarse);
 m.lora.choice=RC8_SELECT_LORA;
 assert(r2_ui_display_time(&m,1000,&utc,&coarse)&&utc==36642000000LL&&coarse);
 m.role=R2_UI_FOLLOWER;m.face_lock_until_us=2000000;m.face_rms_ns=50;
 assert(r2_ui_face_locked(&m,1000)&&r2_ui_analog(&m,1000)&&r2_ui_saver_allowed(&m,1000));
 for(unsigned deny=0;deny<10;deny++){
  struct r2_ui_model bad=m;
  if(deny==0)bad.lora.requested=false;
  if(deny==1)bad.lora.active=false;
  if(deny==2)bad.lora.fault=true;
  if(deny==3)bad.lora.transmitter=true;
  if(deny==4)bad.lora.state=RC8_EXPIRED;
  if(deny==5)bad.lora.sampled_us=1001000;
  if(deny==6)bad.lora.until_us=1000000;
  if(deny==7)bad.lora.utc_us=UINT64_MAX;
  if(deny==8)bad.platform_ready=false;
  if(deny==9)bad.profile_switching=true;
  assert(!r2_ui_display_time(&bad,1000,&utc,&coarse)&&!r2_ui_analog(&bad,1000)&&!r2_ui_face_locked(&bad,1000)&&!r2_ui_saver_allowed(&bad,1000));
 }
 for(unsigned fraction=0;fraction<1000;fraction++){
  struct r2_ui_model bucket=m;bucket.lora.sampled_us=1000000+fraction;
  bucket.lora.utc_us=36642000000ULL+fraction;
  assert(r2_ui_display_time(&bucket,1000,&utc,&coarse)&&utc==36642000000LL&&coarse&&r2_ui_analog(&bucket,1000));
  assert(r2_ui_display_time(&bucket,1001,&utc,&coarse)&&utc==36642001000LL);
 }
 struct r2_ui_model edge=m;edge.lora.until_us=1001500;
 assert(r2_ui_display_time(&edge,1000,&utc,&coarse));
 assert(!r2_ui_display_time(&edge,1001,&utc,&coarse)); /* conservative fractional expiry */
 edge=m;edge.lora.sampled_us=1000999;edge.lora.utc_us=998;
 assert(!r2_ui_display_time(&edge,1000,&utc,&coarse)); /* fractional subtraction cannot underflow */
 edge=m;edge.lora.sampled_us=1000999;edge.lora.utc_us=(uint64_t)INT64_MAX+999;
 assert(r2_ui_display_time(&edge,1000,&utc,&coarse)&&utc==INT64_MAX);
 assert(!r2_ui_display_time(&edge,1001,&utc,&coarse));
 /* A full 4 s correction cadence with delayed map retirement. The source
  * owner renews the UI model, while the original event deadline stays 12 s.
  * No BLE role, precision fit, local PPS or link-RMS threshold is required. */
 struct r2_ui_model timeline=m;timeline.role=R2_UI_NO_ROLE;timeline.clock.valid=false;
 int64_t previous=0;
 for(uint64_t now=1000;now<181000;now+=25){
  uint64_t event=1000+(now-1000)/4000*4000;
  timeline.sampled_ms=now;timeline.lora.sampled_us=event*1000;
  timeline.lora.utc_us=36642000000ULL+(event-1000)*1000;
  timeline.lora.until_us=event*1000+RC8_HOLD_US;
  assert(r2_ui_display_time(&timeline,now,&utc,&coarse)&&coarse&&r2_ui_analog(&timeline,now));
  if(previous)assert(utc-previous==25000);previous=utc;
 }
 timeline.sampled_ms=13000;timeline.lora.sampled_us=1000000;timeline.lora.until_us=13000000;
 assert(r2_ui_display_time(&timeline,12999,&utc,&coarse)==false); /* model from future */
 timeline.sampled_ms=12999;assert(r2_ui_display_time(&timeline,12999,&utc,&coarse));
 timeline.sampled_ms=13000;assert(!r2_ui_display_time(&timeline,13000,&utc,&coarse)&&!r2_ui_analog(&timeline,13000));
 struct r2_ui_sync_display held={.utc_us=123,.at_ms=900,.loss_ms=950};
 m.boundary_clock=m.profile_supported=m.profile_rc=m.ble_active=true;
 assert(!r2_ui_sync_display_time(&held,&m,1000,&utc));
 m.clock.valid=false;m.role=R2_UI_NO_ROLE;m.boundary_clock=false;
 for(unsigned mode=0;mode<12;mode++){
  struct r2_ui_model v=m;
  if(mode==0)v.lora.requested=v.lora.active=false;
  if(mode==1){v.lora.state=RC8_ACQUIRING;v.lora.utc_us=0;v.lora.choice=RC8_SELECT_NONE;}
  if(mode==3){v.lora.choice=RC8_SELECT_BLE;}
  if(mode==4){v.lora.state=RC8_EXPIRED;v.lora.utc_us=0;v.lora.age_ms=12000;v.lora.choice=RC8_SELECT_NONE;}
  if(mode==5){v.lora.transmitter=true;v.lora.choice=RC8_SELECT_LOCAL_GM;v.lora.kind=RC8_OWN_GM;v.lora.quality_known=true;}
  if(mode==6){v.lora.fault=true;v.lora.active=false;}
  if(mode==7){v.lora.link_samples=16;v.lora.link_rms_ns=12654;v.lora.kind=RC8_SELECTED_BLE;v.lora.parent_tag=0xABCD;v.lora.quality_known=v.lora.parent_quality=true;v.lora.parent_class=248;v.lora.parent_accuracy=254;}
  if(mode==8){v.lora.link_samples=4;v.lora.link_rms_ns=54;v.lora.kind=RC8_OWN_GM;v.lora.quality_known=true;}
  if(mode==9){v.lora.link_samples=16;v.lora.link_rms_ns=16000000;v.lora.kind=RC8_SELECTED_BLE;v.lora.quality_known=true;}
  if(mode==10){v.lora.transmitter=true;v.lora.state=RC8_ACQUIRING;v.lora.utc_us=0;v.lora.quality_known=true;}
  if(mode==11){v.lora.coord_follow=true;v.lora.link_samples=4;v.lora.link_rms_ns=75;v.lora.kind=RC8_SELECTED_BLE;}
  save(argv[1],mode,&v);
 }
 for(unsigned mode=90;mode<=95;mode++){
  struct r2_ui_model v=m;v.ble_rx_supported=true;v.ble_rx_requested=false;v.ble_active=false;
  v.profile_supported=true;v.profile_rc=mode==91;v.role=R2_UI_NO_ROLE;v.clock.valid=false;
  if(mode==92)v.lora.age_ms=11999;
  if(mode==93){v.lora.until_us=1000000;v.lora.state=RC8_EXPIRED;v.lora.choice=RC8_SELECT_NONE;v.lora.utc_us=0;}
  if(mode==94){v.lora.fault=true;v.lora.active=false;v.lora.utc_us=0;}
  save(argv[1],mode,&v);
 }
 m.lora.until_us=1000000;assert(!r2_ui_display_time(&m,1000,&utc,&coarse));
 puts("PASS RC8 estimate dial, 180 s continuity, original 12 s expiry and fail-closed controls; no BLE/PLL/authority claim");
}
