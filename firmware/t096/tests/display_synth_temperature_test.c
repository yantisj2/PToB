#include <assert.h>
#include <stdio.h>
#include "../src/display.c"
int main(void){
 int16_t q=999;
 hf_status=CLOCK_CONTROL_STATUS_OFF;
 assert(r2_display_temperature(&q)==-EAGAIN&&q==999&&!temp_init_calls);
 fake_now=4000;hf_status=CLOCK_CONTROL_STATUS_ON;temp_raw=94;temp_elapsed=1;
 assert(!r2_display_temperature(&q)&&q==94&&!temp_owned);
 assert(temp_init_calls==1&&temp_measure_calls==1&&temp_uninit_calls==1);
 fake_now=7999;temp_raw=200;assert(!r2_display_temperature(&q)&&q==94&&temp_measure_calls==1);
 fake_now=8000;temp_measure_error=NRFX_ERROR_INTERNAL;
 assert(r2_display_temperature(&q)==-EAGAIN&&!temp_owned&&temp_uninit_calls==2);
 fake_now=9000;assert(r2_display_temperature(&q)==-EAGAIN&&temp_measure_calls==2);
 fake_now=12000;temp_measure_error=0;temp_raw=-9;
 assert(!r2_display_temperature(&q)&&q==-9);
 fake_now=16000;temp_raw=0;assert(!r2_display_temperature(&q)&&q==0);
 fake_now=20000;temp_raw=341;assert(r2_display_temperature(&q)==-EAGAIN&&!temp_owned);
 fake_now=24000;temp_raw=-161;assert(r2_display_temperature(&q)==-EAGAIN&&!temp_owned);
 fake_now=28000;temp_raw=150;temp_elapsed=2;
 assert(r2_display_temperature(&q)==-EAGAIN&&!temp_owned);
 fake_now=32000;temp_elapsed=0;temp_init_error=NRFX_ERROR_ALREADY;
 unsigned released=temp_uninit_calls;
 assert(r2_display_temperature(&q)==-EBUSY&&temp_uninit_calls==released&&!temp_owned);
 fake_now=36000;temp_init_error=0;temp_raw=160;
 assert(!r2_display_temperature(&q)&&q==160&&!temp_owned);
 puts("Synth TEMP: HFXO status gate,4s sampling/cache,negative/zero readings,fault/range/preemption invalidation,exclusive init and unconditional release PASS");
}
