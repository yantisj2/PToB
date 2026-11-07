/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual worker paused before SPI; higher-priority main input keeps sampling. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static void input_fixture_render(uint8_t *,const struct r2_ui_model *,const struct r2_ui_status *,uint64_t);
#define r2_ui_render input_fixture_render
#include UI_INPUT_SOURCE
#undef r2_ui_render
static bool paused;
static void input_fixture_render(uint8_t *fb,const struct r2_ui_model *m,const struct r2_ui_status *s,uint64_t now){
 r2_ui_render(fb,m,s,now);
 if(mode==62)input_main_advance(28);
 if(!paused&&frame_calls>=1&&(mode==57||mode==61)){
  paused=true;input_main_advance(600);
  struct r2_ui_status observed;r2_ui_snapshot(&observed);
  paused_input_page=observed.page;paused_menu_item=observed.menu.item;
 }
}
int main(int argc,char **argv){
 assert(argc==2);mode=atoi(argv[1]);assert(mode>=57&&mode<=62);
 struct r2_ui_model m={.platform_ready=true,.profile_supported=true,.unit="TEST"};
 r2_ui_publish(&m);if(mode==62)status.page=1;if(mode==61){status.menu.open=true;status.menu.item=0;}
 r2_ui_start();assert(priority==9);
 entry(NULL,NULL,NULL);
#ifdef R2_INPUT_BASELINE
 assert(mode==57||mode==61);
 assert(mode==61?paused_menu_item==0:paused_input_page==0);
 puts("EXPECTED317_MISSED_120MS_PULSE_DURING600MS_RENDER");
#else
 if(mode==57)assert(paused&&paused_input_page==1);
 if(mode==61)assert(paused&&paused_menu_item==1);
 if(mode==58)assert(error_input_injected&&paused_input_page==1&&!buffer_changed);
 if(mode==59)assert(status.page==1&&status.buttons==1&&status.error==-EIO);
 if(mode==60)assert(status.stopped&&status.error==-ENODEV);
 if(mode==62){assert(input_help_at>=2000&&input_help_at<2050&&input_menu_at>=2500&&input_menu_at<2550);assert(first_help&&first_help<first_menu&&first_help<input_menu_at);}
 unsigned reads=physical_button_reads;input_main_advance(1000);assert(physical_button_reads==reads);
 puts("PASS_MAIN20MS_INPUT_ORIGINAL_RULES_WITH_PAUSED_OR_FAILED_RENDER");
#endif
 return 0;
}
