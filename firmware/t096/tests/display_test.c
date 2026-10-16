#include <assert.h>
#include <stdio.h>
#include "../src/display.c"
static uint8_t fb[R2_UI_BYTES];
int main(void){
 assert(!r2_display_init());
 int16_t q=999;assert(r2_display_temperature(&q)==-EAGAIN&&q==999);
 temp_cal=1;assert(r2_display_temperature(&q)==-EAGAIN);
 temp_skip=1;temp_value=(struct sensor_value){23,500000};assert(!r2_display_temperature(&q)&&q==94);
 fake_now+=12001;assert(r2_display_temperature(&q)==-EAGAIN);
 ++temp_cal;temp_value=(struct sensor_value){-2,-250000};assert(!r2_display_temperature(&q)&&q==-9);
 ++temp_skip;temp_read_error=-EIO;assert(r2_display_temperature(&q)==-EAGAIN);temp_read_error=0;
 ++temp_cal;temp_value=(struct sensor_value){0,0};assert(!r2_display_temperature(&q)&&q==0);

 int64_t began=fake_now;unsigned prior=transfers;
 assert(!r2_display_frame(fb));assert(fake_now-began<50);
 assert(largest==R2_UI_BYTES&&transfers-prior==6&&lights==0);
 assert(!r2_display_brightness(100)&&pwm_pulse==999999);
 prior=pwm_calls;assert(!r2_display_brightness(100)&&pwm_calls==prior);
 assert(!r2_display_brightness(50)&&pwm_pulse==500000);
 assert(!r2_display_brightness(100)&&pwm_pulse==999999);
 pwm_error=-EIO;assert(r2_display_brightness(50)==-EIO);pwm_error=0;
 assert(!r2_display_brightness(50)&&pwm_pulse==500000);
 assert(r2_display_brightness(101)==-EINVAL);
 timeout=true;began=fake_now;assert(r2_display_frame(fb)<0);
 assert(fake_now-began<=12&&!pending_dma);assert(!r2_display_recover());
 timeout=false;assert(!r2_display_frame(fb));
 /* No new submission unless its complete timeout fits remaining budget. */
 transaction_deadline=fake_now+36;prior=transfers;
 assert(transfer(fb,R2_UI_BYTES)==-ETIMEDOUT&&transfers==prior);
 transaction_deadline=fake_now+37;assert(!transfer(fb,R2_UI_BYTES));
 /* Scheduler overrun remains a failure, with public ownership already ended. */
 delayed_completion=300;assert(r2_display_frame(fb)<0);assert(!pending_dma);
 assert(!r2_display_recover());assert(!r2_display_frame(fb));assert(!r2_display_off()&&pwm_pulse==0);
 puts("Display: single25600B DMA frame, public synchronous timeout/abort, original250ms reservation, failed overrun retained, next-cadence recovery PASS");
}
