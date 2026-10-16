/* Actual native capture implementation, public driver calls replaced. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/capture.c"
int main(int argc,char **argv)
{
 assert(argc==2);failure=(unsigned)strtoul(argv[1],NULL,10);
 struct gpio_dt_spec p={&gpio1,11};struct r2_capture edge;
 assert(!r2_capture_pps(&edge,0));
 fake_now=1000;assert(!r2_capture_start(&p,fake_now));
 raw_now=UINT32_C(0xfffff000);fake_now=1100;r2_capture_poll(fake_now);
 if(failure==1){fake_now=21000;r2_capture_poll(fake_now);}
 if(failure>=1&&failure<=8){assert(r2_capture_error()<0&&!accepting&&!requested&&!ppi_owned&&!timer_owned&&releases==1);}
 else{
  assert(accepting&&requested&&route_enabled&&timer_started);
  raw_pps=UINT32_C(0xfffffff0);raw_now=32;fake_now=1400;
  assert(r2_capture_pps(&edge,fake_now)&&edge.tick==UINT64_C(0xfffffff0)&&edge.epoch==1);
  raw_now+=10000;fake_now+=625;
  assert(r2_capture_pps(&edge,fake_now)&&edge.tick==UINT64_C(0xfffffff0)); /* IRQ delay does not move edge */
  struct r2_capture current;
  assert(r2_capture_now(&current)&&current.kind==R2_CAPTURE_NOW&&current.epoch==edge.epoch&&current.tick>edge.tick);
  uint64_t sampled=0;assert(r2_capture_now_at(&current,&sampled)&&sampled==(uint64_t)fake_now);
  if(failure==12){
   /* IRQ entry sampled time before another serialized current-capture read. */
   int64_t arrival=fake_now;fake_now+=100;raw_now+=1600;
   assert(r2_capture_now(&current));
   assert(r2_capture_pps(&edge,arrival)&&!r2_capture_error());
  }
  if(failure==13){
   /* Stale CC0 rejects this PPS, not continuity of the shared radio timer. */
   raw_pps=raw_now-4000001;
   assert(!r2_capture_pps(&edge,fake_now));
   assert(r2_capture_now(&current)&&!r2_capture_error());
   raw_pps=raw_now;assert(r2_capture_pps(&edge,fake_now)&&edge.epoch==1);
  }
  if(failure==14){
   --fake_now;
   assert(!r2_capture_now(&current)&&r2_capture_error()==-ESTALE&&!accepting);
  }
  if(failure==11){fake_now+=1000001;r2_capture_poll(fake_now);assert(!accepting&&r2_capture_error()==-ESTALE);}
  int rc=r2_capture_stop();
  if(failure==9){assert(rc==-EIO&&requested&&timer_owned&&ppi_owned);failure=0;assert(!r2_capture_stop());}
  else if(failure==10){assert(rc==-EIO&&requested&&!timer_owned&&!ppi_owned);failure=0;assert(!r2_capture_stop());}
  else assert(!rc);
  assert(!requested&&!timer_owned&&!ppi_owned&&!route_enabled&&releases==1);
 }
 assert(!r2_capture_pps(&edge,fake_now));assert(r2_capture_start(&p,fake_now)==-EALREADY);
 puts("PASS capture: hardware CC, wrap/delay, pre-arm fence, bounded HF and resource unwind");
}
