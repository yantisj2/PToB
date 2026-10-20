/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis. Offline production renderer/expander. */
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
void rgb565_reference_r2_ui_render(uint8_t *,const struct r2_ui_model *,
                                  const struct r2_ui_status *,uint64_t);
static uint8_t indices[R2_UI_FRAME_BYTES+2],expanded[R2_UI_BYTES+2],direct[R2_UI_BYTES];
int main(void)
{
 /* Independently stated original wire colors catch accidental palette edits. */
 static const uint16_t colors[]={0x0000,0x0820,0x1042,0xffff,0xa534,0x37ff,
  0x4d1f,0x4fea,0xfd20,0xf9c7,0x3987,0x0821,0xbdf7,0x8c71,0x2104,0x3186,0x6b4d};
 assert(sizeof colors/sizeof colors[0]==R2_UI_COLOR_COUNT);
 for(size_t i=0;i<R2_UI_PIXELS;++i)indices[i+1]=(uint8_t)(i%R2_UI_COLOR_COUNT);
 memset(expanded,0xa5,sizeof expanded);
 assert(r2_ui_expand_rgb565(expanded+1,R2_UI_BYTES,indices+1,R2_UI_PIXELS));
 for(size_t i=0;i<R2_UI_PIXELS;++i){
  uint16_t expected=colors[i%R2_UI_COLOR_COUNT];
  assert(expanded[1+2*i]==expected>>8&&expanded[2+2*i]==(uint8_t)expected);
 }
 assert(expanded[0]==0xa5&&expanded[sizeof expanded-1]==0xa5);
 uint8_t small[3]={0xa5,0xa5,0xa5};
 assert(!r2_ui_expand_rgb565(small,1,indices+1,1)&&small[0]==0xa5);
 assert(!r2_ui_expand_rgb565(small,sizeof small,indices+1,R2_UI_PIXELS+1));
 assert(!r2_ui_expand_rgb565(NULL,2,indices+1,1));
 assert(!r2_ui_expand_rgb565(small,2,NULL,1));
 uint8_t invalid=R2_UI_COLOR_COUNT;
 assert(!r2_ui_expand_rgb565(small,2,&invalid,1)&&small[0]==0xa5);
 invalid=UINT8_MAX;assert(!r2_ui_expand_rgb565(small,2,&invalid,1));
 assert(r2_ui_expand_rgb565(small,2,indices+1,1)&&small[2]==0xa5);

 /* The same actual drawing bodies also compile through their original direct
  * RGB565 pixel sink. Compare full expanded bytes across pages and states. */
 unsigned frames=0;
 for(unsigned variant=0;variant<64;++variant){
  struct r2_ui_model m={.sampled_ms=1000,.unit="TEST1",.platform_ready=true,
   .bt_ready=true,.scope_active=(variant&1)!=0,.profile_supported=true,
   .profile_rc=(variant&2)!=0,.boundary_clock=(variant&2)!=0,
   .role=variant%3,.ble_active=(variant&4)!=0,.authenticated_gm=true,
   .follower_pps_valid=true,.gm_source_fresh=true,.source_eligible=true,
   .gm_capable=true,.local_hot_standby=(variant&8)!=0,.temperature_valid=true,
   .temperature_qc=(int16_t)(variant*4),.precision_beat_hz=16,
   .ble_time_selected=(variant&16)!=0,.ble_valid_until_us=2000000,
   .face_lock_until_us=2000000,.face_rms_ns=70,
   .clock={.valid=true,.utc_us=1792340000000000LL,.source_available=true,
    .fix_active=(variant&32)!=0,.pps_present=true,.pps_age_us=100000,
    .gps_age_us=100000,.sats_used=17,.state=R2_TIME_SYNCED},
   .precision={.authorized=true,.map_valid=true,.fit_valid=true,.locked=true,
    .rms_valid=true,.rms_ns=63,.samples=64},
   .pps_precision={.epoch=1,.age_us=100000,.rate_valid=true,.samples=64}};
  strcpy(m.root_name,"Root");strcpy(m.upstream_name,"Upstream");
  for(unsigned i=0;i<8;++i)m.lamps[i]=(uint8_t)((variant+i)%3);
  for(unsigned page=0;page<R2_UI_PAGES;++page)for(unsigned style=0;style<5;++style){
   struct r2_ui_status s={.page=page,.help=style==1,.screensaver=style>=2,
    .screensaver_position=style>=2?style-2:0,.holds={.pps_since=1,.hot_since=1,.active_since=1}};
   uint64_t now=variant%5?1000:4000;
   memset(indices,0xa5,sizeof indices);memset(expanded,0xa5,sizeof expanded);
   r2_ui_render(indices+1,&m,&s,now);
   assert(indices[0]==0xa5&&indices[sizeof indices-1]==0xa5);
   assert(r2_ui_expand_rgb565(expanded+1,R2_UI_BYTES,indices+1,R2_UI_PIXELS));
   rgb565_reference_r2_ui_render(direct,&m,&s,now);
   assert(!memcmp(expanded+1,direct,sizeof direct));++frames;
  }
 }
 printf("Framebuffer: 17 exact colors, checked bounds, %u direct/expanded pixel-identical frames PASS\n",frames);
}
