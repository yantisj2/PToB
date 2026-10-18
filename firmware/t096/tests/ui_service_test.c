#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../src/ui_service.c"
int main(int argc,char **argv){
 assert(argc==2);mode=atoi(argv[1]);
 if(mode==0){r2_ui_stop();r2_ui_start();assert(!entry&&!init_calls&&!r2_ui_close_result());}
 else{
  struct r2_ui_model m={.unit="TC-01",.profile_supported=mode>=14};r2_ui_publish(&m);if((mode>=8&&mode<14)||mode==24||mode>=26)assert(!r2_ui_page(R2_UI_SHADOW_PAGE));if(mode==16)assert(!r2_ui_page(2));if(mode==17)assert(!r2_ui_page(1));r2_ui_start();assert(entry&&priority==6);entry(NULL,NULL,NULL);
  if(mode==1)assert(!init_calls&&!frame_calls&&off_calls==1&&!r2_ui_close_result());
  if(mode==2){assert(init_calls==1&&frame_calls>=10&&off_calls==1&&!r2_ui_close_result());assert(starts[1]-starts[0]>=230&&starts[2]-starts[1]>=230&&starts[2]-starts[0]>=500);assert(status.buttons==2&&status.page==2&&!status.help&&!status.poweroff);}
  if(mode==14||mode==15){assert(status.profile_toggles==1&&!status.help&&!status.poweroff&&status.page==0);assert(r2_ui_take_profile_toggle());assert(!r2_ui_take_profile_toggle());}
  if(mode==16){assert(first_saver>=300350&&first_saver<=300400&&saver_positions==7&&dim_seen&&awake_after_dim);assert(random_calls==2&&random_at[0]-first_saver>=30000&&random_at[0]-first_saver<=30020&&random_at[1]-random_at[0]>=29980&&random_at[1]-random_at[0]<=30020);assert(!status.screensaver&&status.brightness_percent==100&&status.page==3&&status.buttons==2&&!status.profile_toggles&&!status.poweroff);}
  if(mode==17){assert(!first_saver&&frame_calls>=24);for(unsigned i=1;i<24;i++)assert(starts[i]-starts[i-1]>=110&&starts[i]-starts[i-1]<=140);}
  if(mode>=18&&mode<24){assert(first_saver>=300350&&dim_seen&&awake_after_dim&&!status.screensaver&&status.brightness_percent==100&&status.page==0);if(mode==19||mode==20||mode==22||mode==23)assert(r2_ui_face_locked(&model,fake_now));}
  if(mode==24)assert(!first_saver&&!dim_seen&&status.page==R2_UI_SHADOW_PAGE&&status.shadow.samples>=600&&status.shadow.elapsed_ms>=600000&&!status.shadow.missing);
  if(mode==25)assert(first_saver&&dim_seen&&awake_after_dim&&!status.screensaver&&status.page==R2_UI_SHADOW_PAGE&&status.shadow.samples>=3);
  if(mode>=26){
   assert(status.shadow.samples>=3&&status.shadow.warmup>=11&&shadow_session.start_tick>=UINT64_C(12000000)*16);
   if(mode==26)assert(status.shadow.max_ns==37&&status.shadow.mean_tenth_ns==-370);
   if(mode==27)assert(status.shadow.max_ns==500); /* Large post-start errors stay. */
   if(mode==28)assert(status.shadow.warmup_resets&&status.shadow.warmup_skipped&&shadow_session.start_tick>=UINT64_C(19000000)*16);
   if(mode==29)assert(status.shadow.warmup_resets&&shadow_session.start_tick>=UINT64_C(18000000)*16);
   if(mode==30)assert(status.shadow.warmup_missing==1&&status.shadow.warmup_resets&&shadow_session.start_tick>=UINT64_C(19000000)*16);
   if(mode==31)assert(shadow_session.epoch==2&&shadow_session.start_tick>=UINT64_C(20000000)*16);
  }
  if(mode==7)assert(frame_calls>=3&&!buffer_changed&&status.frames>=2&&!r2_ui_close_result());
  if(mode==4)assert(status.help&&!status.poweroff&&!status.buttons);
  if(mode==5)assert(status.help&&status.poweroff&&!status.buttons);
  if(mode==3)assert(frame_calls==1&&off_calls==1&&r2_ui_close_result()==-ETIMEDOUT&&status.buttons==2&&!buffer_changed);
  if(mode==6)assert(frame_calls==1&&status.help&&status.poweroff&&!buffer_changed&&r2_ui_close_result()==-ETIMEDOUT);
  if(mode>=8&&mode<14){assert(!status.shadow.active&&!r2_ui_close_result());
   if(mode==8)assert(status.shadow.session==1&&status.shadow.samples>=3&&status.shadow.mean_tenth_ns==-370&&status.shadow.max_ns==37);
   if(mode==9||mode==10||mode==12)assert(!status.shadow.session&&!status.shadow.samples);
   if(mode==11)assert(status.shadow.session==1&&status.shadow.samples>=3&&status.shadow.elapsed_ms<12000);
   if(mode==13)assert(!status.shadow.session&&!status.shadow.samples);
  }
 }
 puts("UI worker: 4Hz/8Hz cadence,5min idle/30s movement/50% brightness,wake consumes hold,truthful time loss,original input/close/DMA fences PASS");
}
