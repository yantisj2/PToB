/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <limits.h>
static void save(const char *dir,unsigned mode,struct r2_ui_model *m){
 static uint8_t fb[R2_UI_FRAME_BYTES],rgb[R2_UI_BYTES];struct r2_ui_status s={.page=R2_UI_LORA_PAGE};
 r2_ui_render(fb,m,&s,1000);assert(r2_ui_expand_rgb565(rgb,sizeof rgb,fb,sizeof fb));
 char path[512];snprintf(path,sizeof path,"%s/rc8-%u.ppm",dir,mode);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n80 160\n255\n");
 for(unsigned i=0;i<sizeof rgb;i+=2){unsigned p=((unsigned)rgb[i]<<8)|rgb[i+1];fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}fclose(f);
}
int main(int argc,char **argv){assert(argc==2);
 struct r2_ui_model m={.sampled_ms=1000,.platform_ready=true,.unit="TEST",.lora={.requested=true,.active=true,.state=RC8_AVAILABLE,.choice=RC8_SELECT_LORA,.utc_us=36642000000ULL,.sampled_us=1000000,.until_us=13000000,.root_tag=0x1234,.emitter_tag=0x5678,.age_ms=4000}};
 int64_t utc;bool coarse;assert(r2_ui_display_time(&m,1000,&utc,&coarse)&&coarse&&utc==36642000000LL);assert(!r2_ui_analog(&m,1000));
 /* The explicit normal-source choice wins. RC8 cannot borrow a cached BLE
  * face permit or unrelated local coarse time while it is selected. */
 m.clock.valid=true;m.clock.utc_us=123;m.lora.choice=RC8_SELECT_BLE;
 assert(r2_ui_display_time(&m,1000,&utc,&coarse)&&utc==123&&!coarse);
 m.lora.choice=RC8_SELECT_LORA;
 assert(r2_ui_display_time(&m,1000,&utc,&coarse)&&utc==36642000000LL&&coarse);
 m.role=R2_UI_FOLLOWER;m.face_lock_until_us=2000000;m.face_rms_ns=50;
 assert(!r2_ui_face_locked(&m,1000)&&!r2_ui_analog(&m,1000));
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
 m.lora.until_us=1000000;assert(!r2_ui_display_time(&m,1000,&utc,&coarse));
 puts("PASS RC8 render states; functional estimate has no analog/BLE lock");
}
