/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
#include <assert.h>
#include <stdio.h>
#include "../src/display.c"
static uint8_t fb[R2_UI_FRAME_BYTES],expected[R2_UI_DMA_BYTES];
static unsigned stage_count[5],refuse_at,seen,waits;
static enum r2_display_stage refuse_stage;
static bool refresh;
static void loan(const uint8_t *data,size_t size){
 assert(data==pending_dma&&cs_active&&size<=R2_UI_DMA_BYTES);++waits;
 /* Main can change source/control while sleeping. No application callback
  * runs here and the persistent DMA bytes stay owned until this returns. */
}
static bool boundary(void *context,enum r2_display_stage stage){
 assert(context==&fb&&!pending_dma);++stage_count[stage];
 if(stage==R2_DISPLAY_PREPARE&&refresh)
  for(unsigned i=0;i<sizeof fb;i++)fb[i]=(uint8_t)((i+3)%R2_UI_COLOR_COUNT);
 if(stage==refuse_stage&&++seen==refuse_at)return false;
 return true;
}
static void reset_case(void){
 fake_now=0;timeout=false;transfers=largest=waits=0;fail_transfer=delay_transfer=transfer_delay=0;
 delayed_completion=0;payload_transfers=payload_bytes=0;payload_buffer=NULL;pending_dma=NULL;
 cs_active=dc_data=false;cs_close_calls=fail_cs_close=0;injected_spi_error=-116;injected_cs_error=-EACCES;
 memset(stage_count,0,sizeof stage_count);seen=refuse_at=0;refuse_stage=R2_DISPLAY_WINDOW;refresh=false;
 spi_wait_hook=loan;
 for(unsigned i=0;i<sizeof fb;i++)fb[i]=(uint8_t)(i%R2_UI_COLOR_COUNT);
}
static int region(unsigned x,unsigned y,unsigned w,unsigned h,uint64_t deadline){
 int rc=r2_display_region(fb,x,y,w,h,deadline,boundary,&fb);
 assert(!pending_dma&&stage_count[R2_DISPLAY_RETIRED]==1&&!ui_region_budget_mode);
 return rc;
}
int main(void){
 reset_case();refresh=true;assert(!region(0,16,80,32,100));
 assert(transfers==6&&waits==6&&payload_transfers==1&&payload_bytes==5120&&!cs_active);
 assert(stage_count[R2_DISPLAY_WINDOW]==3&&stage_count[R2_DISPLAY_PREPARE]==1&&stage_count[R2_DISPLAY_EXPAND]==32&&stage_count[R2_DISPLAY_SUBMIT]==1);
 assert(r2_ui_expand_rgb565(expected,sizeof expected,fb+16*80,80*32));assert(!memcmp(wire,expected,5120));
 reset_case();assert(!region(66,19,9,9,100));assert(transfers==6&&payload_transfers==1&&payload_bytes==162);
 for(unsigned row=0;row<9;row++)assert(r2_ui_expand_rgb565(expected+row*18,sizeof expected-row*18,fb+(19+row)*80+66,9));
 assert(!memcmp(wire,expected,162));
 /* Each of the six sleeping calls may return a physical negative. */
 for(unsigned at=1;at<=6;at++)for(unsigned kind=0;kind<3;kind++){
  reset_case();fail_transfer=at;injected_spi_error=kind==0?-EIO:kind==1?-EAGAIN:-ECANCELED;
  assert(region(0,16,80,32,100)==injected_spi_error&&transfers==at&&!cs_active);
 }
 /* Successful-but-late is soft only for the additive region path. A real
  * error survives the same late resume and every CS cleanup. */
 for(unsigned at=1;at<=6;at++){
  reset_case();delay_transfer=at;transfer_delay=101;assert(region(0,16,80,32,100)==R2_DISPLAY_EXPIRED&&transfers==at);
  reset_case();delay_transfer=fail_transfer=at;transfer_delay=101;injected_spi_error=-EIO;
  assert(region(0,16,80,32,100)==-EIO&&transfers==at);
 }
 reset_case();assert(region(0,16,80,32,10)==R2_DISPLAY_BUDGET_DECLINED&&!transfers);
 reset_case();assert(region(0,16,80,32,21)==R2_DISPLAY_BUDGET_DECLINED&&transfers==5&&!payload_transfers);
 for(unsigned stage=R2_DISPLAY_WINDOW;stage<=R2_DISPLAY_SUBMIT;stage++){
  unsigned limit=stage==R2_DISPLAY_WINDOW?3:stage==R2_DISPLAY_EXPAND?32:1;
  for(unsigned at=1;at<=limit;at++){
   reset_case();refuse_stage=(enum r2_display_stage)stage;refuse_at=at;
   assert(region(0,16,80,32,100)==R2_DISPLAY_OBSOLETE&&!payload_transfers&&!cs_active);
  }
 }
 /* End-of-command and final CS failures override every soft/zero result.
  * When a real SPI failure preceded cleanup, preserve that first error. */
 for(unsigned at=1;at<=2;at++){
  reset_case();fail_cs_close=at;assert(region(0,16,80,32,10)==-EACCES);
 }
 reset_case();refuse_stage=R2_DISPLAY_WINDOW;refuse_at=1;fail_cs_close=2;
 assert(region(0,16,80,32,100)==-EACCES);
 reset_case();delay_transfer=6;transfer_delay=101;fail_cs_close=4;
 assert(region(0,16,80,32,100)==-EACCES);
 reset_case();fail_transfer=6;injected_spi_error=-EIO;fail_cs_close=4;
 assert(region(0,16,80,32,100)==-EIO);fail_cs_close=0;assert(!r2_display_recover()&&!cs_active);
 reset_case();delay_transfer=6;transfer_delay=300;assert(r2_display_frame(fb)==-ETIMEDOUT&&!ui_region_budget_mode);
 puts("PASS production region pixels/6 SPI loans, all boundaries, soft/physical/late domains, CS precedence and legacy250ms behavior");
}
