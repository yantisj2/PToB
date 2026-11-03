/* MIT. Jonathan Yantis, 2026. Actual slow owner, authenticated wire and fake boundaries. */
#define main existing_owner_main
#include "lora_ram_fixture.h"
#undef main
static struct lp_scope scope;
static uint8_t context[16];
#define TRAIN_PAIRS ((unsigned)(LP_TRAIN_US/LP_BEAT_US+1))
static unsigned warm_pairs=TRAIN_PAIRS;static uint64_t first_slot;
static void display_diagnostics(void){
 char out[2976],field[96];int n=r2_lora_diagnostics_json(out,sizeof out,0);
 assert(n>0&&(size_t)n<sizeof out&&out[n]==0);
 struct r2_lora_view v=published;view_advance(&v,now_us);
 bool available=v.utc_us&&v.requested&&v.active&&!v.fault;
 assert(strstr(out,available?"\"display_available\":true":"\"display_available\":false"));
 assert(strstr(out,v.display_acquiring?"\"display_acquiring\":true":"\"display_acquiring\":false"));
 assert(strstr(out,v.display_held?"\"display_held\":true":"\"display_held\":false"));
 snprintf(field,sizeof field,"\"display_utc_us\":%llu",(unsigned long long)v.utc_us);assert(strstr(out,field));
 snprintf(field,sizeof field,"\"display_sampled_us\":%llu",(unsigned long long)now_us);assert(strstr(out,field));
 uint64_t reference=v.until_us>=LP_HOLD_US?v.until_us-LP_HOLD_US:UINT64_MAX;
 uint64_t age=available&&now_us>=reference?(now_us-reference)/1000:UINT64_MAX;
 snprintf(field,sizeof field,"\"display_ref_age_ms\":%u",age>UINT32_MAX?UINT32_MAX:(uint32_t)age);assert(strstr(out,field));
}
static void train_watch(void){
 init(false);step();assert(!published.utc_us&&!role.rx.display_valid);display_diagnostics();
 scope=(struct lp_scope){.kind=1,.emitter=1,.boot=99,.root=1,.root_boot=101,.epoch=5,.stream=1,.seed=0x51a7c003};
 assert(!lp_context(context,&scope,LP_SLOW,&test_group));
 uint64_t base=first_slot=now_us+1000000;
 for(unsigned i=0;i<warm_pairs;i++){
  uint64_t slot=base+i*LP_BEAT_US;
  struct lp_packet p={.kind=i?LP_DISC:LP_META,.schedule=LP_SLOW,.scope=scope,.serial=1,.cursor=2*i,.time_valid=true,.has_pair=true,.utc_ns=UINT64_C(1000000000000)+slot*1000,.tz=r2_tz_unknown()};
  p.delay_10ms=(uint16_t)((LP_SYNC_OFFSET_US-(lp_duration(p.kind)-10000)+9999)/10000);
  packet(&p,context,slot+lp_duration(p.kind)-10000,slot+lp_duration(p.kind)-9000);
  now_us=slot+LP_SYNC_OFFSET_US-100000;step();assert(tuning_phy==R2_LORA_FHSS);
  p=(struct lp_packet){.kind=LP_SYNC,.schedule=LP_SLOW,.cursor=2*i};
  packet(&p,context,slot+LP_SYNC_OFFSET_US+75000,slot+LP_SYNC_OFFSET_US+223000);
  p.kind=LP_FOLLOW;p.utc_ns=UINT64_C(1000000000000)+(slot+LP_SYNC_OFFSET_US)*1000;
  packet(&p,context,slot+LP_SYNC_OFFSET_US+560000,slot+LP_SYNC_OFFSET_US+745000);
  if(i+1<TRAIN_PAIRS)assert(!role.rx.trained&&role.rx.display_valid&&role.rx.display_acquiring&&published.display_acquiring&&!published.display_held&&published.utc_us&&!published.time_valid&&!published.quality_known&&published.choice==RC8_SELECT_NONE);
 }
 if(warm_pairs<TRAIN_PAIRS){display_diagnostics();return;}
 assert(role.rx.trained&&role.rx.display_valid&&!role.rx.display_acquiring&&role.rx.accepted==TRAIN_PAIRS&&published.utc_us);display_diagnostics();
 now_us=role.rx.physical_deadline_us+100;step();
 assert(published.display_held&&published.state==RC8_EXPIRED&&!published.time_valid&&!published.quality_known&&!published.rssi_valid&&published.choice==RC8_SELECT_NONE);
}
static void control_packet(bool valid,bool new_context,bool corrupt){
 uint64_t slot=now_us+1000000;struct lp_scope s=scope;if(new_context){++s.stream;++s.epoch;++s.root_boot;}
 struct lp_packet p={.kind=new_context?LP_META:LP_DISC,.schedule=LP_SLOW,.scope=s,.serial=2,.cursor=role.rx.highest+2,.time_valid=valid,.utc_ns=UINT64_C(1000000000000)+slot*1000,.tz=r2_tz_unknown()};
 assert(!tune(R2_LORA_DTS,0,true));
 if(!corrupt){packet(&p,context,slot+lp_duration(p.kind)-10000,slot+lp_duration(p.kind)-9000);return;}
 now_us=slot+lp_duration(p.kind)-9000;
 queued=(struct r2_lora_hw_event){.tick=(slot+lp_duration(p.kind)-10000)*16,.capture_epoch=7,.generation=generation,.kind=R2_LORA_HW_RX_DONE,.length=(uint8_t)lp_size(p.kind)};
 assert(!lp_encode(queued.frame,sizeof queued.frame,&p,context,&test_group));queued.frame[queued.length-1]^=1;have_event=true;step();
}
static void acquisition_pair(uint64_t slot,unsigned cursor,unsigned serial,uint64_t shift,bool meta){
 struct lp_packet p={.kind=meta?LP_META:LP_DISC,.schedule=LP_SLOW,.scope=scope,.serial=serial,.cursor=cursor,.time_valid=true,.has_pair=true,.utc_ns=UINT64_C(1000000000000)+slot*1000+shift,.tz=r2_tz_unknown()};
 p.delay_10ms=(uint16_t)((LP_SYNC_OFFSET_US-(lp_duration(p.kind)-10000)+9999)/10000);
 assert(!tune(R2_LORA_DTS,0,true));packet(&p,context,slot+lp_duration(p.kind)-10000,slot+lp_duration(p.kind)-9000);
 now_us=slot+LP_SYNC_OFFSET_US-100000;step();
 p=(struct lp_packet){.kind=LP_SYNC,.schedule=LP_SLOW,.cursor=cursor};packet(&p,context,slot+LP_SYNC_OFFSET_US+75000,slot+LP_SYNC_OFFSET_US+223000);
 p.kind=LP_FOLLOW;p.utc_ns=UINT64_C(1000000000000)+(slot+LP_SYNC_OFFSET_US)*1000+shift;packet(&p,context,slot+LP_SYNC_OFFSET_US+560000,slot+LP_SYNC_OFFSET_US+745000);
}
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);if(mode==7)now_us=(uint64_t)UINT32_MAX*1000-100000000;
 if(mode>=19)warm_pairs=1;train_watch();uint64_t original=published.until_us,accepted=role.rx.accepted;
 if(mode>=19){
  uint64_t anchor=role.rx.display_ns,reference=role.rx.display_us,tick=role.rx.display_tick;
  assert(role.rx.display_valid&&role.rx.display_acquiring&&!role.rx.trained&&role.rx.fit_count==1&&role.rx.display_rate_ppb==0);
  assert(published.display_acquiring&&!published.display_held&&published.choice==RC8_SELECT_NONE&&!published.time_valid&&!published.quality_known&&!published.rssi_valid);
  char out[1024];assert(r2_lora_utc_json(out,sizeof out,0)>0&&strstr(out,"\"clock_reason\":5"));
  struct r2_gm_coordination coord;r2_lora_coordination(&coord);assert(!coord.root);witness_enabled=true;r2_lora_shadow_publish();assert(!witness_published);
  if(mode==19){
   uint64_t before=published.utc_us,at=published.sampled_us;now_us+=1000000;step();assert(published.utc_us==before+published.sampled_us-at);
   now_us=role.rx.observed_deadline_us+2*LP_LEASE_US;step();assert(published.display_acquiring&&published.utc_us&&!role.rx.trained&&!role.rx.have_pair&&role.rx.display_us==reference&&published.age_ms==(published.sampled_us-reference)/1000);
  }else if(mode==20){
   control_packet(true,true,false);scope=role.rx.scope;assert(!lp_context(context,&scope,LP_SLOW,&test_group));
   acquisition_pair(now_us+1000000,role.rx.highest+2,3,UINT64_C(5000000000),true);
   assert(role.rx.display_ns==anchor&&role.rx.display_us==reference&&role.rx.display_tick==tick&&role.rx.display_rate_ppb==0&&published.display_acquiring&&!published.time_valid);
   assert(published.age_ms==(published.sampled_us-reference)/1000&&role.rx.fit_count==1&&!role.rx.trained);
  }else if(mode==21){
   for(unsigned i=1;i<TRAIN_PAIRS;i++){acquisition_pair(first_slot+i*LP_BEAT_US,2*i,1,0,false);if(i+1<TRAIN_PAIRS)assert(published.display_acquiring&&role.rx.display_ns==anchor&&role.rx.display_us==reference);}
   assert(role.rx.trained&&!role.rx.display_acquiring&&!published.display_acquiring&&!published.display_held&&published.time_valid&&published.quality_known&&published.choice==RC8_SELECT_LORA&&role.rx.display_us>reference);
   now_us=role.rx.physical_deadline_us+1;step();assert(published.display_held&&!published.display_acquiring&&!published.time_valid&&!published.quality_known);
  }else if(mode==22)control_packet(false,false,false);
  else if(mode==23){++scope.boot;control_packet(true,true,false);}
  else if(mode==24){r2_lora_control(true,true,RC8_SELECT_NONE);step();}
  else if(mode==25){r2_lora_control(false,true,RC8_SELECT_BLE);step();}
  else if(mode==26){fake_capture_epoch=8;step();}
  else if(mode==27){now_us-=500000;step();}
  else if(mode==28){atomic_or(&faulted,1);step();}
  else if(mode==29){r2_lora_request(false);step();step();}
  else if(mode==30){lp_rx_clear(&role.rx);report(now_us);}
  else assert(false);
  if(mode>=22)assert(!role.rx.display_valid&&!role.rx.display_acquiring&&!published.utc_us&&!published.display_acquiring);
 }else if(mode==1||mode==7){
  uint64_t utc=published.utc_us,at=published.sampled_us;now_us+=1000000;step();assert(published.utc_us==utc+published.sampled_us-at);
  now_us=role.rx.observed_deadline_us+2*LP_LEASE_US;step();
  assert(published.display_held&&published.utc_us&&published.until_us==original&&published.display_until_us==UINT64_MAX);
  assert(!role.rx.have_pair&&!role.rx.trained&&!role.rx.physical_deadline_us&&!role.rx.meta_deadline_us&&!published.time_valid);
  assert(role.rx.accepted==accepted&&!txs);witness_enabled=true;r2_lora_shadow_publish();assert(!witness_published);
  struct r2_gm_coordination c;r2_lora_coordination(&c);assert(!c.root);
  uint64_t before=published.utc_us,then=published.sampled_us;now_us+=1000000;step();assert(published.utc_us==before+published.sampled_us-then);
 }else if(mode==2){control_packet(false,false,false);assert(!role.rx.display_valid&&!published.display_held&&!published.utc_us);
 }else if(mode==3){
  uint64_t anchor=role.rx.display_ns,tick=role.rx.display_tick,reference=role.rx.display_us;
  int32_t rate=role.rx.display_rate_ppb;uint64_t previous=published.utc_us,previous_at=published.sampled_us;
  control_packet(true,true,false);
  assert(role.rx.display_valid&&published.display_held&&published.utc_us&&role.rx.scope.stream==2);
  assert(!role.rx.trained&&!role.rx.have_pair&&!role.rx.fit_count&&!role.rx.bounded);
  assert(!published.time_valid&&!published.quality_known&&!published.rssi_valid&&published.choice==RC8_SELECT_NONE);
  assert(published.utc_us==previous+published.sampled_us-previous_at&&published.until_us==original);
  /* A changed acquisition diagnostic rate cannot steer the frozen watch. */
  role.rx.rate_ppb=4326;previous=published.utc_us;previous_at=published.sampled_us;
  now_us+=1000000;step();assert(published.utc_us==previous+published.sampled_us-previous_at);
  assert(role.rx.display_rate_ppb==rate&&published.age_ms==(published.sampled_us-reference)/1000);
  scope=role.rx.scope;assert(!lp_context(context,&scope,LP_SLOW,&test_group));
  uint64_t base=now_us+1000000;uint32_t first_cursor=role.rx.highest+2;
  for(unsigned i=0;i<TRAIN_PAIRS;i++){
   uint64_t slot=base+i*LP_BEAT_US;uint32_t cursor=first_cursor+2*i;
   struct lp_packet p={.kind=i?LP_DISC:LP_META,.schedule=LP_SLOW,.scope=scope,.serial=3,.cursor=cursor,.time_valid=true,.has_pair=true,.utc_ns=UINT64_C(1005000000000)+slot*1000,.tz=r2_tz_unknown()};
   p.delay_10ms=(uint16_t)((LP_SYNC_OFFSET_US-(lp_duration(p.kind)-10000)+9999)/10000);
   assert(!tune(R2_LORA_DTS,0,true));packet(&p,context,slot+lp_duration(p.kind)-10000,slot+lp_duration(p.kind)-9000);
   now_us=slot+LP_SYNC_OFFSET_US-100000;step();
   p=(struct lp_packet){.kind=LP_SYNC,.schedule=LP_SLOW,.cursor=cursor};
   packet(&p,context,slot+LP_SYNC_OFFSET_US+75000,slot+LP_SYNC_OFFSET_US+223000);
   p.kind=LP_FOLLOW;p.utc_ns=UINT64_C(1005000000000)+(slot+LP_SYNC_OFFSET_US)*1000;
   packet(&p,context,slot+LP_SYNC_OFFSET_US+560000,slot+LP_SYNC_OFFSET_US+745000);
   if(i+1<TRAIN_PAIRS){
    assert(role.rx.display_ns==anchor&&role.rx.display_tick==tick&&role.rx.display_us==reference&&role.rx.display_rate_ppb==rate);
    assert(published.display_held&&!published.time_valid&&!published.quality_known&&!role.rx.trained);
    assert(published.age_ms==(published.sampled_us-reference)/1000&&published.until_us==original);
    uint64_t before=published.utc_us,at=published.sampled_us;now_us+=1000000;step();assert(published.utc_us==before+published.sampled_us-at);
   }
  }
  assert(role.rx.trained&&role.rx.fit_count==TRAIN_PAIRS&&!published.display_held&&published.choice==RC8_SELECT_LORA);
  assert(published.time_valid&&published.quality_known&&role.rx.display_us>reference&&role.rx.display_ns!=anchor);
 }else if(mode==15){++scope.boot;control_packet(true,true,false);assert(!role.rx.display_valid&&!published.utc_us);
 }else if(mode==16){
  struct lp_packet p={.kind=LP_META,.schedule=LP_SLOW,.scope=scope,.serial=2,.cursor=role.rx.highest+2,.time_valid=true,.utc_ns=role.rx.last_meta_ns+1000000000};
  p.scope.emitter=p.scope.root=2;++p.scope.boot;++p.scope.stream;
  assert(lp_rx_meta(&role.rx,&p,&test_group,NULL,now_us,now_us));report(now_us);assert(!role.rx.display_valid&&!published.utc_us);
 }else if(mode==17){control_packet(false,true,false);assert(!role.rx.display_valid&&!published.utc_us);
 }else if(mode==18){lp_rx_clear(&role.rx);report(now_us);assert(!role.rx.display_valid&&!published.utc_us);
 }else if(mode==4){fake_capture_epoch=8;step();assert(!role.rx.display_valid&&!published.utc_us);fake_capture_epoch=7;step();assert(!published.utc_us);
 }else if(mode==5){r2_lora_request(false);struct r2_lora_view v;r2_lora_view(&v);assert(!v.utc_us&&!v.display_held);step();step();r2_lora_request(true);step();step();assert(!published.utc_us&&!role.rx.display_valid);
 }else if(mode==6){r2_lora_control(true,true,RC8_SELECT_NONE);struct r2_lora_view v;r2_lora_view(&v);assert(!v.utc_us);step();assert(!published.utc_us&&!role.rx.display_valid);
 }else if(mode==8){r2_lora_control(false,true,RC8_SELECT_BLE);step();assert(!published.utc_us&&!role.rx.display_valid);r2_lora_control(false,true,RC8_SELECT_NONE);step();assert(!published.utc_us);
 }else if(mode==9){atomic_or(&faulted,1);step();assert(!published.utc_us&&!role.rx.display_valid);
 }else if(mode==10){now_us=role.rx.last_us-1;step();assert(!published.utc_us&&!role.rx.display_valid);
 }else if(mode==14){
  const int32_t rates[]={4326,-4326,100000,-100000};
  const uint64_t elapsed=UINT64_C(86400000000),delta=elapsed*16+7;
  for(unsigned i=0;i<sizeof rates/sizeof rates[0];i++){
   struct lp_rx copy=role.rx;copy.display_rate_ppb=rates[i];uint64_t actual;
   /* Independent wide-integer oracle: multiply before dividing. */
   __int128 expected=(__int128)copy.display_ns+(__int128)delta*125/2+
     (__int128)delta*rates[i]/16000000;
   assert(expected>0&&expected<UINT64_MAX);
   assert(lp_rx_display_estimate(&copy,copy.display_tick+delta,copy.display_epoch,copy.display_us+elapsed,&actual));
   assert(actual==(uint64_t)expected&&copy.display_valid&&!copy.have_pair&&!copy.trained);
  }
 }else if(mode==13){now_us+=10000000;step();now_us-=5000000;assert(now_us>role.rx.last_us);step();assert(!published.utc_us&&!role.rx.display_valid);now_us+=6000000;step();assert(!published.utc_us);
 }else if(mode==11){unsigned bad=owner.bad;control_packet(false,false,true);assert(owner.bad==bad+1&&role.rx.display_valid&&published.display_held&&published.utc_us);
 }else if(mode==12){struct lp_rx copy=role.rx;uint64_t ns;assert(!lp_rx_display_estimate(&copy,UINT64_MAX,7,UINT64_MAX,&ns));lp_rx_clear(&role.rx);report(now_us);assert(!published.utc_us&&!role.rx.display_valid);
 }else assert(false);
 display_diagnostics();assert(!txs);printf("PASS slow display holdover %u; authority unchanged\n",mode);return 0;
}
