#include <assert.h>
#include <stdio.h>
#include "../src/display.c"
static uint8_t fb[R2_UI_FRAME_BYTES],expected[R2_UI_BYTES];
static unsigned waits;
static void wait_preemption(const uint8_t *data,size_t size){
 /* Model a higher-priority owner running while the public SPI owner sleeps.
  * DMA must still own the same persistent bytes throughout that interval. */
 assert(data==pending_dma&&cs_active);assert(size<=R2_UI_DMA_BYTES);++waits;
}
static void reset_payload(void){payload_transfers=payload_bytes=0;payload_buffer=NULL;}
int main(void){
 assert(!r2_display_init());
 int16_t q=999;assert(r2_display_temperature(&q)==-EAGAIN&&q==999);
 temp_cal=1;assert(r2_display_temperature(&q)==-EAGAIN);
 temp_skip=1;temp_value=(struct sensor_value){23,500000};assert(!r2_display_temperature(&q)&&q==94);
 fake_now+=12001;assert(r2_display_temperature(&q)==-EAGAIN);
 ++temp_cal;temp_value=(struct sensor_value){-2,-250000};assert(!r2_display_temperature(&q)&&q==-9);
 ++temp_skip;temp_read_error=-EIO;assert(r2_display_temperature(&q)==-EAGAIN);temp_read_error=0;
 ++temp_cal;temp_value=(struct sensor_value){0,0};assert(!r2_display_temperature(&q)&&q==0);

 for(size_t i=0;i<sizeof fb;++i)fb[i]=(uint8_t)(i%R2_UI_COLOR_COUNT);
 assert(r2_ui_expand_rgb565(expected,sizeof expected,fb,sizeof fb));
 spi_wait_hook=wait_preemption;
 int64_t began=fake_now;unsigned prior=transfers;reset_payload();
 assert(!r2_display_frame(fb));assert(fake_now-began<50);
 assert(largest==R2_UI_DMA_BYTES&&transfers-prior==10&&lights==0);
 assert(payload_transfers==5&&payload_bytes==R2_UI_BYTES&&waits==10);
 assert(!memcmp(wire,expected,sizeof wire)&&!cs_active&&!pending_dma);
 assert(!r2_display_brightness(100)&&pwm_pulse==999999);
 prior=pwm_calls;assert(!r2_display_brightness(100)&&pwm_calls==prior);
 assert(!r2_display_brightness(50)&&pwm_pulse==500000);
 assert(!r2_display_brightness(100)&&pwm_pulse==999999);
 pwm_error=-EIO;assert(r2_display_brightness(50)==-EIO);pwm_error=0;
 assert(!r2_display_brightness(50)&&pwm_pulse==500000);
 assert(r2_display_brightness(101)==-EINVAL);
 timeout=true;began=fake_now;assert(r2_display_frame(fb)<0);
 assert(fake_now-began<=12&&!pending_dma);assert(!r2_display_recover());
 timeout=false;reset_payload();assert(!r2_display_frame(fb));
 /* Fail every command/data submission in turn. No later batch may rewrite
  * the failed DMA buffer, and CS/driver ownership must be released. */
 for(unsigned at=1;at<=10;++at){
  reset_payload();prior=transfers;fail_transfer=prior+at;began=fake_now;
  assert(r2_display_frame(fb)<0&&transfers-prior==at);
  assert(!pending_dma&&!cs_active&&fake_now-began<250);
  if(at>=6){size_t first=(at-6)*R2_UI_DMA_PIXELS;
   assert(r2_ui_expand_rgb565(expected,R2_UI_DMA_BYTES,fb+first,R2_UI_DMA_PIXELS));
   assert(!memcmp(frame_dma,expected,sizeof frame_dma));
  }
  fail_transfer=0;assert(!r2_display_recover());
 }
 /* Expiry during a sleeping payload call remains failure after retirement;
  * never continue with another batch or extend the original deadline. */
 reset_payload();prior=transfers;delay_transfer=prior+7;transfer_delay=300;
 assert(r2_display_frame(fb)==-ETIMEDOUT&&transfers-prior==7);
 assert(!pending_dma&&!cs_active&&payload_transfers==2);delay_transfer=0;
 /* No new submission unless its complete timeout fits remaining budget. */
 transaction_deadline=fake_now+16;prior=transfers;
 assert(transfer(frame_dma,sizeof frame_dma)==-ETIMEDOUT&&transfers==prior);
 transaction_deadline=fake_now+17;reset_payload();cs_active=dc_data=true;
 assert(!transfer(frame_dma,sizeof frame_dma));cs_active=false;
 /* Scheduler overrun remains a failure, with public ownership already ended. */
 delayed_completion=300;assert(r2_display_frame(fb)<0);assert(!pending_dma);
 assert(!r2_display_recover());reset_payload();assert(!r2_display_frame(fb));
 /* Corrupt index is contained before that batch's SPI submission. */
 reset_payload();fb[R2_UI_DMA_PIXELS]=UINT8_MAX;prior=transfers;
 assert(r2_display_frame(fb)==-EINVAL&&transfers-prior==6);
 assert(payload_transfers==1&&!pending_dma&&!cs_active);fb[R2_UI_DMA_PIXELS]=0;
 assert(!r2_display_off()&&pwm_pulse==0);
 puts("Display: five exact5120B DMA batches, every submission failure, retained DMA bytes, original250ms bound, failed overrun and recovery PASS");
}
