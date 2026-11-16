/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual PT/mailbox/precision/codec/native-start bodies; fake HAL/HCI only. */
#include "gm7_pt_radio_fixture.inc"
#include "gm7_bridge.h"
static struct r2_enrollment path;
static struct r2_source_clock local;
static struct r2_radio_scope grant;
static struct r2_group_client anchor;
static bool running;
static bool replay_record;
static bool audit_lora;
static unsigned duplicate_shortenings;
static struct rc8_export audited_lora;
static bool replay_query;
static uint32_t replay_hash;
static unsigned optin_unlocks;
static unsigned control_race;
static void replace_follower_control(void){
 fake_after_stats=NULL;
 if(control_race==4){r2_radio_service_withdraw();}
 if(control_race==0)++grant.source_epoch;
 if(control_race==1)++grant.publication_epoch;
 if(control_race==2)++grant.remote_capture_epoch;
 if(control_race==3)++grant.boot;
 r2_radio_service_gm7_publish(grant,&anchor,&path,-1,fake_time,anchor.until);
}
static void setup_pt(void){
 path=(struct r2_enrollment){.count=2,.identity={17,99},.group={.id=7,.epoch=1,.psk={1}}};
 assert(!r2_radio_service_init(0,&path));r2_radio_service_continuous(true);fake_adv_success=true;
 local=(struct r2_source_clock){.epoch=1,.head=1,.count=1,.utc_bound=true,.utc_source_epoch=3,.utc_offset_ns=1000000000000LL};
 local.maps[0]=(struct r2_source_map){.from=8000000,.until=32000000,.anchor_ns=1000000000,.generation=11,.rate_q16=16000000*UINT64_C(65536)};
 r2_radio_service_source(&local,true,3);
 grant=(struct r2_radio_scope){.root=0,.root_identity=17,.boot=88,.source_epoch=4,.local_source_epoch=3,.publication_epoch=5,.until_us=fake_time+1000000,.gm=true};
}
static void publish_own(uint64_t resume){struct r2_group_client empty={0};r2_radio_service_gm7_publish(grant,&empty,&path,1000000000000LL,fake_time,resume);}
static void follower(void){
 anchor=(struct r2_group_client){.passive=true,.source=true,.root=99,.boot=77,.source_epoch=8,.publication=9,
  .uncertainty_us=UINT32_MAX,.utc_us=UINT64_C(1792800001000000),.at=fake_time,.until=fake_time+1000000,.map_until=fake_time+1000000};
 grant=(struct r2_radio_scope){.root=1,.root_identity=99,.boot=77,.source_epoch=8,.publication_epoch=9,.remote_capture_epoch=4,.until_us=anchor.until,.follower=true};
 r2_radio_service_gm7_publish(grant,&anchor,&path,-1,fake_time,anchor.until);
}
static void replace_grant(void){fake_after_stats=NULL;++grant.boot;++grant.source_epoch;++grant.publication_epoch;publish_own(grant.until_us);}
static void overflow_resume_probe(void){
 if(!pt_input.stopped)return;
 fake_mutex_unlocked=NULL;assert(error==-ENOBUFS);publish_own(grant.until_us);assert(pt_input.stopped&&error==-ENOBUFS);
}
static void first_optin_probe(void){
 if(++optin_unlocks!=2)return; /* After ordinary final-export precheck. */
 fake_mutex_unlocked=NULL;struct r2_radio_scope original=pt_input.grant;
 ++grant.boot;publish_own(grant.until_us);
 assert(!pt_input.gm7_managed&&pt_input.gm7_busy&&same(pt_input.grant,original));
}
static void expire_after_copy(void){fake_mutex_unlocked=NULL;fake_time=capture_us=grant.until_us;capture_tick=fake_time*16;}
static void stop_after_query(void){fake_mutex_unlocked=NULL;r2_radio_service_withdraw();}
static void drive_stream_range(unsigned first,unsigned samples){
  uint64_t base=1000000000;struct r2_enrollment receive_roster=enrollment;
  for(unsigned i=first;i<samples;i++){
   uint64_t ns=base+(uint64_t)i*62500000;
   struct r2_precision_packet p={.root=0,.boot=77,.source_epoch=8,.train=9,.publication=i+1,.capture_epoch=4,.flags=1,
    .created_ns=ns-1000000,.expires_ns=base+(i/16+1)*UINT64_C(1000000000)+450000000,.utc_offset_ns=INT64_C(1792800000000000000)};
   if(i){p.count=1;p.correction[0]=(struct r2_precise_correction){.publication=i,.event=(uint16_t)i,.channel=3,.ticks=(base+(uint64_t)(i-1)*62500000)*2/125,.generation=9,.expires_ns=p.expires_ns};}
   enrollment=receive_roster;enrollment.identity[0]=99;enrollment.identity[1]=17;if(!i)generic_frame=ns-2000000;
   packet=(struct r2_radio_record){.raw=16000000+i*1000000+144,.epoch=1,.length=R2_RELAY_AD_BYTES,.event=(uint16_t)(i+1),.channel=3};
   assert(!generic_encode(packet.ad,&p));enrollment=receive_roster;
   fake_time=capture_us=1000000+(uint64_t)i*62500+1000;capture_tick=(uint64_t)packet.raw+16000;queued=true;precision_step(fake_time);
   assert(!queued&&!error&&!pt_input.source_ready&&!pt_output.scope.gm);
   if(audit_lora&&lora_publishes){
    if(audited_lora.valid&&audited_lora.anchor_tick==lora_export.anchor_tick&&audited_lora.anchor_ns==lora_export.anchor_ns&&audited_lora.rate_ppb==lora_export.rate_ppb&&audited_lora.phase_ns==lora_export.phase_ns&&
       (lora_export.until_tick<audited_lora.until_tick||lora_export.expires_ns<audited_lora.expires_ns)){
      fprintf(stderr,"stream shortening %u until%lld expiry%lld last%llu delivery%llu\n",i,(long long)(lora_export.until_tick-audited_lora.until_tick),(long long)(lora_export.expires_ns-audited_lora.expires_ns),(unsigned long long)precision.last_accepted,(unsigned long long)precision.servo.output.delivery);++duplicate_shortenings;
    }
    audited_lora=lora_export;
   }

   if(replay_query){struct r2_gm7_follow_diagnostic d;r2_radio_service_gm7_follow_diagnostic(&d,fake_time);assert(d.data_until_tick==precision.data_until);}
   if(replay_record){
    const r2_servo_map_t *m=&precision.servo.output;
    uint64_t values[]={pt_output.utc,pt_output.valid_until,pt_output.utc_valid,pt_output.view.map_valid,
     precision.accepted,precision.rejected,precision.data_until,precision.last_available,precision.last_accepted,
     m->delivery,(uint64_t)m->anchor,(uint64_t)(int64_t)m->rate,(uint64_t)(int64_t)m->phase,m->valid,
     precision.servo.count,precision.servo.span,(uint64_t)precision.servo.rate,
     pt_output.scope.root_identity,pt_output.scope.boot,pt_output.scope.source_epoch,precision.train,precision.remote_capture};
    replay_hash=(replay_hash*16777619u)^r2_crc32(values,sizeof values);
   }
  }
}
static void drive_stream(unsigned samples){drive_stream_range(0,samples);}
int main(int argc,char **argv){
 assert(argc==2);unsigned test=(unsigned)strtoul(argv[1],NULL,10);
 if(test>=100){char mode_text[16];snprintf(mode_text,sizeof mode_text,"%u",test-100);char *historical[]={argv[0],mode_text};return historical_radio_main(2,historical);}
 setup_pt();
#ifdef R2_LORA_FIXED500_EXPERIMENT
 if(test==95){
  lora_needed=true;ble_needed=false;
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);follower();precision_poll(fake_time,&running);
  drive_stream(900);assert(lora_publishes&&pt_output.utc_valid);
  struct r2_radio_precision original=precision;struct pt_output output=pt_output;
  struct pt_input input=pt_input;struct r2_radio_scope active=active_scope;
  uint64_t base_tick=capture_tick,base_us=fake_time;
  lora_hold_limit=lora_export;
  for(unsigned expired=0;expired<2;expired++)for(unsigned fence=0;fence<10;fence++){
   precision=original;pt_output=output;pt_input=input;active_scope=active;
   capture_tick=expired?original.data_until+16:lora_hold_limit.until_tick;
   fake_time=capture_us=base_us+(capture_tick-base_tick)/16;
   capture_test_epoch=1;capture_test_available=true;lora_hold_available=true;
   if(fence==1)precision.data_until=lora_hold_limit.until_tick; /* Tighter capture permission. */
   if(fence==2)precision.origin_expires_ns-=1000; /* Tighter authenticated UTC permission. */
   if(fence==3)capture_test_epoch=2;
   if(fence==4)pt_input.stopped=true;
   if(fence==5)++pt_input.grant.boot;
   if(fence==6)precision.servo.fault=true;
   if(fence==7)++pt_output.scope.remote_capture_epoch;
   if(fence==8)lora_hold_available=false;
   if(fence==9)for(unsigned j=0;j<32;j++)precision.servo.output_quality.squares[j]=
    UINT64_C(1000000)*precision.servo.output_quality.predictions[j];
   unsigned withdrawn=lora_withdraws,published=lora_publishes,held=lora_hold_queries;
   precision_step(fake_time);
   if(!fence){
    assert(lora_hold_queries==held+1&&lora_withdraws==withdrawn&&lora_publishes==published);
    assert(source_gate==R2_LORA_SOURCE_BLE_HOLDOVER);
    if(!expired)assert(pt_output.utc_valid&&fake_time<pt_output.valid_until);
    else assert(!pt_output.utc_valid);
   }else if(fence==9&&expired){
    /* Expired normal quality is not renewed by the existing bounded copy. */
    assert(lora_publishes==published);
   }else{
    if(!(lora_withdraws>withdrawn&&lora_publishes==published))
     fprintf(stderr,"expiry fence%u expired%u publish%u withdraw%u holds%u gate%u\n",fence,expired,lora_publishes-published,lora_withdraws-withdrawn,lora_hold_queries-held,source_gate);
    assert(lora_withdraws>withdrawn&&lora_publishes==published);
   }
  }
  puts("PASS managed producer retains unchanged admitted LoRa at conservative-cap and BLE expiry; tighter capture/UTC, scope, stop, capture, fault and absent-copy fences remain");return 0;
 }
 if(test==94){
  lora_needed=true;ble_needed=false;
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);follower();precision_poll(fake_time,&running);
  drive_stream(900);assert(lora_publishes&&pt_output.utc_valid);
  struct rc8_export first=lora_export;
  struct r2_precision_packet p={.root=0,.boot=77,.source_epoch=8,.train=9,.publication=901,.capture_epoch=4,.flags=1,
   .created_ns=precision.permission_created+1000,.expires_ns=precision.permission_expires-150001000,.utc_offset_ns=precision.permission_offset};
  struct r2_enrollment saved=enrollment;enrollment.identity[0]=99;enrollment.identity[1]=17;
  packet=(struct r2_radio_record){.raw=(uint32_t)(capture_tick+160000),.epoch=1,.length=R2_RELAY_AD_BYTES,.event=901,.channel=3};
  assert(!generic_encode(packet.ad,&p));enrollment=saved;
  fake_time=capture_us+=11000;capture_tick+=176000;queued=true;precision_step(fake_time);
  assert(!queued&&lora_export.anchor_tick==first.anchor_tick&&lora_export.expires_ns==first.expires_ns);
  fprintf(stderr,"restriction clip %lld ticks data%llu expiry%llu\n",(long long)(first.until_tick-lora_export.until_tick),(unsigned long long)precision.data_until,(unsigned long long)precision.permission_expires);
  assert(lora_export.until_tick==first.until_tick);
  assert(precision.permission_expires==p.expires_ns+150000000);
  p.publication++;p.expires_ns-=20000000;
  saved=enrollment;enrollment.identity[0]=99;enrollment.identity[1]=17;
  packet.raw=(uint32_t)(capture_tick+16000);packet.event++;
  assert(!generic_encode(packet.ad,&p));enrollment=saved;
  fake_time=capture_us+=2000;capture_tick+=32000;queued=true;precision_step(fake_time);
  assert(!queued&&lora_export.anchor_tick==first.anchor_tick);
  assert(lora_export.until_tick<first.until_tick&&lora_export.until_tick<=precision.data_until);
  assert(precision.permission_expires==p.expires_ns+150000000);
  puts("PASS stable conservative LoRa cap across tiny authenticated expiry restriction; larger restriction still clips");return 0;
 }
 if(test==93){
  lora_needed=true;ble_needed=false;
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);follower();precision_poll(fake_time,&running);
  audit_lora=true;drive_stream(900);assert(lora_publishes&&pt_output.utc_valid);assert(!duplicate_shortenings);
  struct rc8_export first=lora_export;unsigned shortened=0;
  for(unsigned i=0;i<1000;i++){
   capture_tick+=497;fake_time=capture_us+=31;
   unsigned before=lora_publishes;precision_step(fake_time);assert(lora_publishes==before+1);
   assert(lora_export.anchor_tick==first.anchor_tick&&lora_export.anchor_ns==first.anchor_ns&&lora_export.phase_ns==first.phase_ns&&lora_export.rate_ppb==first.rate_ppb);
   if(lora_export.until_tick<first.until_tick||lora_export.expires_ns<first.expires_ns){
    if(!shortened)fprintf(stderr,"duplicate shortened tick %llu->%llu expiry %llu->%llu data%llu last%llu delivery%llu\n",(unsigned long long)first.until_tick,(unsigned long long)lora_export.until_tick,(unsigned long long)first.expires_ns,(unsigned long long)lora_export.expires_ns,(unsigned long long)precision.data_until,(unsigned long long)precision.last_accepted,(unsigned long long)precision.servo.output.delivery);
    ++shortened;
   }
   first=lora_export;
  }
  assert(!shortened);puts("PASS immutable BLE LoRa export across paired-clock quantization");return 0;
 }
 if(test==90||test==91||test==92){
  lora_needed=false;ble_needed=test!=91;replay_record=true;replay_query=test==92;
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);follower();precision_poll(fake_time,&running);
  drive_stream(900);assert(precision.accepted>400&&pt_output.utc_valid&&!lora_publishes);
  assert(ble_needed?(ble_observations>400&&observed_ble.qualified):!ble_observations);
  printf("BLE_MODEL_REPLAY %08x accepted%u rejected%u until%llu observer%u\n",replay_hash,
   precision.accepted,precision.rejected,(unsigned long long)pt_output.valid_until,ble_observations);
  fake_time=capture_us+=2000000;capture_tick+=32000000;precision_step(fake_time);
  assert(!pt_output.utc_valid&&!pt_output.view.map_valid);
  puts("PASS identical realMAC900packet BLE model/UTC/permission trajectory with receiver observation ON/OFF; silence cannot extend original map life");return 0;
 }
#endif
 if(test==32){
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);follower();precision_poll(fake_time,&running);drive_stream(900);
  struct r2_radio_precision saved=precision;
  struct pt_input original_in=pt_input;struct pt_output original_out=pt_output;
  struct r2_radio_scope original_grant=grant,original_active=active_scope;
  uint32_t original_revision=gm7_control_revision;
  for(control_race=0;control_race<6;control_race++){
   precision=saved;pt_input=original_in;pt_output=original_out;grant=original_grant;active_scope=original_active;
   gm7_control_revision=gm7_precision_revision=original_revision;
   fake_after_stats=replace_follower_control;precision_step(fake_time);
   if(control_race==5){assert(pt_output.utc_valid&&gm7_control_revision==original_revision);continue;}
   assert(gm7_control_revision!=original_revision&&!pt_output.utc_valid&&!pt_output.view.map_valid);
   assert(!pt_output.context.source&&!gm7_direct.root);
   /* Old accepted pairs and independent permission cannot survive the next
    * job after CONTROL replaced or withdrew/re-admitted the same parent. */
   precision_step(fake_time);
   assert(!precision.permission_anchor&&!precision.last_accepted&&!pt_output.utc_valid);
  }
  puts("PASS actual PT input-copy/control replacement and stop-resume ABA fences; ordinary same-context refresh preserved");return 0;
 }
 if(test==31){
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);follower();precision_poll(fake_time,&running);drive_stream(900);
  assert(pt_output.utc_valid);uint64_t old=precision.data_until;
  uint64_t delta=old+16000-capture_tick;capture_tick+=delta;fake_time=capture_us+=delta/16;
  int64_t root_ns;assert(r2_servo_mapped(&precision.servo.output,capture_tick,&root_ns));
  struct r2_precision_packet fresh={.root=1,.boot=77,.source_epoch=8,.train=9,.publication=1001,.capture_epoch=4,.flags=1,
   .created_ns=(uint64_t)root_ns-10000000,.expires_ns=(uint64_t)root_ns+1000000000,.utc_offset_ns=INT64_C(1792800000000000000)};
  struct r2_radio_precision baseline=precision;
  struct r2_capture rx={capture_tick-144,1,R2_CAPTURE_RADIO},now={capture_tick,1,R2_CAPTURE_NOW};
  /* Actual authenticated GRv3 count-zero delivery, without a correction. */
  struct r2_enrollment receive_roster=enrollment;enrollment.identity[0]=99;enrollment.identity[1]=17;
  fresh.root=0;packet=(struct r2_radio_record){.raw=(uint32_t)rx.tick,.epoch=1,.length=R2_RELAY_AD_BYTES,.event=1001,.channel=3};
  assert(!generic_encode(packet.ad,&fresh));enrollment=receive_roster;
  assert(!generic_decode(&fresh,packet.ad,sizeof packet.ad));queued=true;precision_step(fake_time);
  assert(!queued&&!error);
  assert(precision.data_until>old&&precision.accepted==baseline.accepted&&precision.last_accepted==baseline.last_accepted);
  int64_t ns;assert(r2_radio_precision_read(&precision,now,fake_time,&ns));
  assert(!memcmp(&baseline.servo,&precision.servo,sizeof precision.servo));
  struct r2_radio_precision renewed=precision;uint64_t renewed_until=precision.data_until;
  for(unsigned negative=0;negative<16;negative++){
   precision=baseline;struct r2_precision_packet p=fresh;struct r2_capture r=rx,a=now;uint64_t kernel=fake_time;
   switch(negative){
    case 0:p.publication=baseline.original_progress;break;
    case 1:p.created_ns=(uint64_t)root_ns+10000000;break;
    case 2:p.created_ns=baseline.permission_created-1;break;
    case 3:p.expires_ns=(uint64_t)root_ns-1;break;
    case 4:++p.utc_offset_ns;break;
    case 5:++p.boot;break;case 6:++p.source_epoch;break;case 7:++p.train;break;
    case 8:++p.capture_epoch;break;case 9:++r.epoch;break;
    case 10:kernel=precision.scope.until_us;break;
    case 11:precision.permission_anchor=false;break;
    case 12:p.expires_ns=UINT64_MAX;break;
    case 13:precision.servo.fault=true;break;case 14:precision.servo.output.valid=false;break;
    case 15:precision.servo.fit_valid=false;break;
   }
   uint64_t before=precision.data_until;(void)r2_radio_precision_rx_serving_origin(&precision,&p,r,1001,3,a,kernel,(int64_t)p.created_ns+p.utc_offset_ns);
   assert(precision.data_until<=before);
  }
  precision=renewed;fresh.publication++;fresh.created_ns+=1000000;fresh.expires_ns-=200000000;
  assert(r2_radio_precision_rx_serving_origin(&precision,&fresh,rx,1002,3,now,fake_time,(int64_t)fresh.created_ns+fresh.utc_offset_ns));
  assert(precision.data_until<renewed_until);
  struct r2_radio_precision clipped=precision;
  precision=renewed;struct r2_capture delayed_now=now,delayed_rx=rx;delayed_now.tick+=R2_CAPTURE_HZ*3/10;delayed_rx.tick+=R2_CAPTURE_HZ*3/10;
  assert(r2_radio_precision_rx_serving_origin(&precision,&fresh,delayed_rx,1002,3,delayed_now,fake_time,(int64_t)fresh.created_ns+fresh.utc_offset_ns));
  assert(!precision.data_until);struct r2_radio_precision aged_barrier=precision;precision=clipped;
  /* A later accepted correction for an older original must retain the
   * separately authenticated newer permission within the same context. */
  struct r2_radio_precision restricted=precision;uint64_t restricted_until=precision.data_until;
  precision=restricted;memset(precision.original,0,sizeof precision.original);memset(precision.correction,0,sizeof precision.correction);
  uint64_t local_tick=now.tick+1000000;uint64_t remote_tick=((uint64_t)root_ns+62500000)*2/125;
  precision.original[0]=(struct r2_radio_original){.local=local_tick,.created_ns=precision.permission_created,.expires_ns=(uint64_t)root_ns+1300000000,.utc_offset_ns=fresh.utc_offset_ns,.publication=999,.event=99,.channel=3,.used=true};
  precision.correction[0]=(struct r2_radio_correction){.value={.publication=999,.event=99,.channel=3,.ticks=remote_tick,.generation=9,.expires_ns=(uint64_t)root_ns+1300000000},.arrived=local_tick+16000,.used=true};
  struct r2_capture later={local_tick+16000,1,R2_CAPTURE_NOW};
  assert(r2_radio_precision_step(&precision,later,fake_time));assert(precision.data_until==restricted_until);
  assert(precision.permission_publication==restricted.permission_publication);
  /* An aged-map revocation persists through fresh acceptance of an older,
   * longer grant. A matched current signed grant can then reacquire clock
   * authority, bounded by its tighter expiry and the new physical cap. */
  precision=aged_barrier;memset(precision.original,0,sizeof precision.original);memset(precision.correction,0,sizeof precision.correction);
  local_tick=delayed_now.tick+1000000;
  uint64_t source_ns=precision.permission_ns+(local_tick-precision.permission_local)*125/2;
  precision.original[0]=(struct r2_radio_original){.local=local_tick,.created_ns=precision.permission_created,.expires_ns=(uint64_t)root_ns+1300000000,.utc_offset_ns=fresh.utc_offset_ns,.publication=999,.event=99,.channel=3,.used=true};
  precision.correction[0]=(struct r2_radio_correction){.value={.publication=999,.event=99,.channel=3,.ticks=source_ns*2/125,.generation=9,.expires_ns=(uint64_t)root_ns+1300000000},.arrived=local_tick+16000,.used=true};
  later.tick=local_tick+16000;assert(r2_radio_precision_step(&precision,later,fake_time));
  assert(!precision.data_until&&precision.permission_publication==aged_barrier.permission_publication);
  assert(!r2_radio_precision_read(&precision,later,fake_time,&ns));
  memset(precision.original,0,sizeof precision.original);memset(precision.correction,0,sizeof precision.correction);
  local_tick=later.tick+1000000;source_ns=precision.permission_ns+(local_tick-precision.permission_local)*125/2;
  fresh.publication=1004;fresh.count=0;rx.tick=local_tick;now.tick=local_tick+16000;
  assert(r2_radio_precision_rx_serving_origin(&precision,&fresh,rx,1004,3,now,fake_time,(int64_t)fresh.created_ns+fresh.utc_offset_ns));
  assert(!precision.data_until);fresh.publication=1005;fresh.count=1;
  fresh.correction[0]=(struct r2_precise_correction){.publication=1004,.event=1004,.channel=3,.ticks=source_ns*2/125,.generation=9,.expires_ns=fresh.expires_ns};
  rx.tick=local_tick+1000000;now.tick=rx.tick+16000;
  assert(r2_radio_precision_rx_serving_origin(&precision,&fresh,rx,1005,3,now,fake_time,(int64_t)fresh.created_ns+fresh.utc_offset_ns));
  assert(r2_radio_precision_step(&precision,now,fake_time));
  assert(precision.data_until&&precision.permission_expires==fresh.expires_ns);
  uint64_t maximum=local_tick+(fresh.expires_ns-source_ns)/1000*16;
  assert(precision.data_until<=maximum&&r2_radio_precision_read(&precision,now,fake_time,&ns));
  /* An authenticated but already expired tighter grant retires cached
   * permission; it never extends or bootstraps the physical map. */
  precision=renewed;struct r2_precision_packet expired=fresh;expired.count=0;expired.publication=1006;
  expired.created_ns=(uint64_t)root_ns-1000000;expired.expires_ns=(uint64_t)root_ns-1;
  rx.tick=capture_tick-144;now.tick=capture_tick;
  assert(r2_radio_precision_rx_serving_origin(&precision,&expired,rx,1006,3,now,fake_time,(int64_t)expired.created_ns+expired.utc_offset_ns));
  assert(!precision.data_until&&precision.permission_publication==1006);

  /* Rejected corrections may advance last_available but cannot renew the
   * accepted physical model lifetime, even with a fresh signed header. */
  precision=renewed;memset(precision.original,0,sizeof precision.original);memset(precision.correction,0,sizeof precision.correction);
  now.tick=renewed.last_accepted+R2_CAPTURE_HZ*3/2;local_tick=now.tick-16000;
  uint64_t rejected_ns=renewed.permission_ns+(local_tick-renewed.permission_local)*125/2;
  precision.original[0]=(struct r2_radio_original){.local=local_tick,.created_ns=rejected_ns-1000000,.expires_ns=rejected_ns+1000000000,.utc_offset_ns=fresh.utc_offset_ns,.publication=1005,.event=105,.channel=3,.used=true};
  precision.correction[0]=(struct r2_radio_correction){.value={.publication=1005,.event=105,.channel=3,.ticks=rejected_ns*2/125,.generation=9,.expires_ns=rejected_ns+1000000000},.arrived=now.tick,.used=true};
  precision.servo.last_sequence=UINT32_MAX;
  assert(!r2_radio_precision_step(&precision,now,fake_time));
  assert(precision.last_available==now.tick&&precision.last_accepted==renewed.last_accepted);
assert(!r2_radio_precision_read(&precision,now,fake_time,&ns));
  struct r2_radio_precision_view view;r2_radio_precision_view(&precision,now,fake_time,&view);assert(!view.map_valid);
  uint64_t before=precision.data_until;fresh.publication++;fresh.created_ns=(uint64_t)root_ns+1500000000;fresh.expires_ns=fresh.created_ns+1000000000;rx.tick=now.tick-144;
  (void)r2_radio_precision_rx_serving_origin(&precision,&fresh,rx,1003,3,now,fake_time,(int64_t)fresh.created_ns+fresh.utc_offset_ns);assert(precision.data_until<=before);
  puts("PASS anchored signed grant renewal without new pair; duplicate/stale/future/expiry/offset/scope/counter/control/unknown/overflow negatives; tightergrant clips; older acceptedpair preserves independentgrant; rejected-correction physical cap");return 0;
 }
 if(test==29){
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);follower();precision_poll(fake_time,&running);drive_stream(900);
  fprintf(stderr,"causal setup utc%d data%llu modeldata%llu physical%llu tick%llu scope%d/%d accepted%u\n",pt_output.utc_valid,(unsigned long long)pt_output.gm7.data_until,(unsigned long long)precision.data_until,(unsigned long long)pt_output.capture_until,(unsigned long long)capture_tick,pt_output.scope.follower,pt_output.scope.broadcast,precision.accepted);
  assert(pt_output.utc_valid&&pt_output.gm7.data_until==precision.data_until&&pt_output.gm7.data_until<pt_output.capture_until);
  struct r2_radio_precision saved=precision;struct r2_gm7_follow_diagnostic d;
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);assert(!d.refusals&&d.utc_readable_now&&d.scope_equal&&d.binding_current);
  ++grant.source_epoch;r2_radio_service_gm7_publish(grant,&anchor,&path,-1,fake_time,anchor.until);
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert(!d.scope_equal&&d.binding_current&&d.utc_readable_now&&!(d.refusals&R2_FOLLOW_BINDING));
  --grant.source_epoch;r2_radio_service_gm7_publish(grant,&anchor,&path,-1,fake_time,anchor.until);
  assert(!memcmp(&saved,&precision,sizeof saved));
  /* Same query/capture observes data expiry while the unchanged physical
   * map, causal fit, control permission and published UTC remain available. */
  uint64_t delta=pt_output.gm7.data_until+1-capture_tick;capture_tick+=delta;fake_time=capture_us+=delta/16+1;
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert((d.refusals&(R2_FOLLOW_DATA_EXPIRED|R2_FOLLOW_EFFECTIVE_EXPIRED))==(R2_FOLLOW_DATA_EXPIRED|R2_FOLLOW_EFFECTIVE_EXPIRED));
  assert(!(d.refusals&(R2_FOLLOW_PHYSICAL_EXPIRED|R2_FOLLOW_FIT|R2_FOLLOW_OUTPUT|R2_FOLLOW_BINDING))&&d.utc_at_sample&&d.map_at_sample&&!d.utc_readable_now);
  uint64_t utc;assert(!r2_radio_service_client_clock(&utc,fake_time));
  assert(!memcmp(&saved,&precision,sizeof saved));
  capture_test_epoch=2;r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert((d.refusals&R2_FOLLOW_COUNTER)&&!(d.refusals&(R2_FOLLOW_DATA_EXPIRED|R2_FOLLOW_PHYSICAL_EXPIRED))&&!d.utc_readable_now);
  capture_test_epoch=1;capture_test_available=false;r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert((d.refusals&R2_FOLLOW_CAPTURE)&&!(d.refusals&(R2_FOLLOW_DATA_EXPIRED|R2_FOLLOW_PHYSICAL_EXPIRED)));
  capture_test_available=true;capture_tick=pt_output.capture_until;fake_time=capture_us+=2000000;
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);assert(d.refusals&R2_FOLLOW_PHYSICAL_EXPIRED);
  fake_mutex_unlocked=stop_after_query;r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert(!d.stopped&&d.grant.root_identity==99&&pt_input.stopped);
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);assert(d.stopped&&(d.refusals&R2_FOLLOW_STOPPED)&&(d.refusals&R2_FOLLOW_BINDING));
  assert(!memcmp(&saved,&precision,sizeof saved));
  puts("PASS actual PT single-capture dataexpiry vs physical/control/quality; captureunknown/epoch mismatch; atomic scope transition and no model/lease mutation");return 0;
 }
 if(test==30){
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);follower();precision_poll(fake_time,&running);drive_stream(200);
  struct r2_radio_precision saved=precision;struct r2_gm7_follow_diagnostic d;uint64_t utc;
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert(d.utc_readable_now==r2_radio_service_client_clock(&utc,fake_time));
  printf("CAUSAL200 utc%d data%llu physical%llu tick%llu refusals%u\n",d.utc_readable_now,(unsigned long long)d.data_until_tick,(unsigned long long)d.physical_until_tick,(unsigned long long)d.query_tick,d.refusals);
  uint64_t delta=pt_output.capture_until+1-capture_tick;capture_tick+=delta;fake_time=capture_us+=delta/16+1;
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert((d.refusals&R2_FOLLOW_PHYSICAL_EXPIRED)&&!d.utc_readable_now&&!r2_radio_service_client_clock(&utc,fake_time));
  assert(!memcmp(&saved,&precision,sizeof saved));
  puts("PASS original200packet fixture retained: diagnostic agrees with original clock reader; originalphysicalexpiry still fences without model changes");return 0;
 }
 if(test==28){
  publish_own(grant.until_us);unsigned initial=lora_withdraws;uint64_t sampled=fake_time;
  grant.until_us=fake_time+100;fake_time+=101;
  struct r2_group_client empty={0};r2_radio_service_gm7_publish(grant,&empty,&path,-1,sampled,grant.until_us);
  assert(lora_withdraws==initial+1);puts("PASS delayed same-source publication cannot suppress expiry withdrawal");return 0;
 }
 if(test==27){
  publish_own(grant.until_us);unsigned initial=lora_withdraws,shortened=0;uint64_t prior=grant.until_us;
  for(unsigned i=0;i<200;i++){
   uint64_t hf=1000000+i*2000;
   uint64_t kernel=(hf*32768/1000000)*1000000/32768;
   struct gm7_bridge b={.hf=hf,.kernel_lo=kernel,.kernel_hi=kernel,.epoch=1};
   assert(gm7_bridge_to_kernel(b,2000000,&grant.until_us));
   if(grant.until_us<prior)++shortened;prior=grant.until_us;
   fake_time=kernel;publish_own(grant.until_us);
   assert(pt_input.grant.until_us==grant.until_us&&!pt_input.stopped);
  }
  assert(shortened>20);
#ifdef R2_LORA_PASSIVE_FAST
  assert(lora_withdraws==initial);
#else
  assert(lora_withdraws==initial+shortened);
#endif
  ++grant.boot;publish_own(grant.until_us);assert(lora_withdraws>initial);initial=lora_withdraws;
  ++grant.local_source_epoch;publish_own(grant.until_us);assert(lora_withdraws==initial+1);initial=lora_withdraws;
  pt_input.source_ready=false;--grant.until_us;publish_own(grant.until_us);assert(lora_withdraws==initial+1);initial=lora_withdraws;
  pt_input.source_ready=true;grant.local_source_epoch=pt_input.source_epoch;grant.until_us=fake_time;publish_own(fake_time);assert(lora_withdraws==initial+1);
  puts("PASS actual HF/kernel bridge jitter retains passive own source; PT deadline still clips; root/source/expiry changes still withdraw");return 0;
 }
 if(test==0){
  follower();precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  assert(!public_client&&pt_output.scope.follower&&native_owned&&sync_object);
  handoff.root=99;handoff.valid=true;r2_radio_service_withdraw();precision_poll(fake_time,&running);
  grant=(struct r2_radio_scope){.root=0,.root_identity=17,.boot=88,.source_epoch=4,.local_source_epoch=3,.publication_epoch=5,.until_us=fake_time+1000000,.gm=true};
  publish_own(grant.until_us);assert(!pt_input.stopped&&handoff.valid&&handoff.root==99);
  r2_radio_service_gm7_tick(fake_time,NULL);assert(r2_radio_service_closed());
  uint64_t completed_retry=retry_at;
  for(unsigned n=1;n<=4;n++){fake_time=capture_us=1000000+n*50000;capture_tick=fake_time*16;
   if(!r2_radio_service_closed())r2_radio_service_stop(fake_time);
   assert(retry_at==completed_retry);
  }
  fake_time=capture_us=1300000;capture_tick=fake_time*16;precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  assert(publication==1&&pt_output.length&&fake_adv_creates==1&&native_gm_started&&r2_radio_service_serving(fake_time)&&pa_started&&ext_started&&!error);
  puts("PASS actual follower withdrawal/drain -> bounded ownGM resume -> payload and PA/EXT start; handoff not cleared");
 }else if(test==1){
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);follower();precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  assert(!public_client&&pt_output.scope.follower&&sync_object&&!pt_input.source_ready);
  drive_stream(10);
  assert(precision.accepted>=4&&!precision.rejected);puts("PASS bounded authenticated follower resumes actual captured G1 association without local GNSS/ownGM authority");
 }else if(test>=2&&test<=8){
  uint64_t until=grant.until_us;
  if(test==2)pt_input.source_ready=false;
  if(test==3)pt_input.source_epoch=10;
  if(test==4)until=fake_time;
  if(test==5)grant.until_us=fake_time;
  if(test==6)grant.root_identity=777;
  if(test==7)error=-EIO;
  if(test==8)until=0;
  publish_own(until);precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  assert(pt_input.stopped&&!publication&&!native_gm_started&&!fake_adv_creates);
  printf("PASS invalid resume gate%u remains stopped without positive output\n",test);
 }else if(test==9){
  publish_own(grant.until_us);precision_poll(fake_time,&running);struct pt_output old=pt_output;
  ++grant.boot;++grant.source_epoch;++grant.publication_epoch;publish_own(grant.until_us);
  pt_output=old;struct pt_output out;output_read(&out,fake_time);
  assert(!out.scope.gm&&!out.scope.follower&&!out.view.authorized&&!out.utc_valid&&!out.context.source&&!native_live(&old,fake_time));
  puts("PASS old in-flight different-scope publication cannot revive mailbox or native HCI authority");
 }else if(test==10){
  publish_own(grant.until_us);precision_poll(fake_time,&running);
  pt_output.valid_until=pt_output.context.until=pt_output.context.map_until=grant.until_us;
  pt_output.capture_until=capture_tick+16000000;pt_output.view.authorized=pt_output.view.map_valid=pt_output.view.locked=pt_output.utc_valid=pt_output.context.source=true;
  uint64_t original=pt_output.sampled;grant.until_us=fake_time+100000;publish_own(grant.until_us);
  struct pt_output out;output_read(&out,fake_time);assert(out.scope.until_us==grant.until_us&&out.valid_until==grant.until_us&&out.context.until==grant.until_us&&out.context.map_until==grant.until_us&&out.sampled==original);
  fake_time=capture_us=grant.until_us;capture_tick=fake_time*16;output_read(&out,fake_time);
  assert(!out.scope.gm&&!out.view.authorized&&!out.view.map_valid&&!out.utc_valid&&!out.context.source&&pt_output.sampled==original);
  puts("PASS same-scope cached output keeps original timestamp/map and obeys tightened exact expiry");
 }else if(test==11){
  publish_own(grant.until_us);precision_poll(fake_time,&running);r2_radio_service_withdraw();
  r2_radio_service_gm7_tick(fake_time,NULL);assert(pt_input.stopped&&!r2_radio_service_serving(fake_time)&&!fake_adv_creates);
  publish_own(0);r2_radio_service_gm7_tick(fake_time,NULL);assert(pt_input.stopped&&!fake_adv_creates);
  puts("PASS stop after publication and consume cannot clear stop or rewrite/resume grant");
 }else if(test==12){
  publish_own(grant.until_us);precision_poll(fake_time,&running);
  fake_capture_overflow=1;precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  assert(!pt_input.stopped&&!error&&pt_output.scope.gm&&last_overflow==1);
  puts("PASS GM precision record loss resets association and resumes existing capture path without retiring GNSS/PPS or latching error");
 }else if(test==13){
  publish_own(grant.until_us);precision_poll(fake_time,&running);unsigned before=lora_publishes;
  fake_after_stats=replace_grant;precision_step(fake_time);assert(!fake_after_stats&&!pt_output.scope.gm&&!pt_output.scope.follower&&lora_publishes==before);
  puts("PASS grant replaced while PT in flight fences LoRa export and mailbox at final commit");
 }else if(test==14){
  publish_own(grant.until_us);precision_poll(fake_time,&running);unsigned before=lora_publishes;
  fake_time=grant.until_us;precision_step(1000000);assert(!pt_output.scope.gm&&!pt_output.scope.follower&&lora_publishes==before);
  puts("PASS advanced validation-now at original expiry rejects stale in-flight LoRa/positive scope");
 }else if(test==15){
  r2_radio_service_activate();r2_radio_service_tick_reference(grant,&local,1000000000000LL,fake_time,NULL);
  fake_mutex_unlocked=first_optin_probe;precision_step(fake_time);
  assert(optin_unlocks==2&&!fake_mutex_unlocked&&!pt_input.gm7_busy&&!pt_input.gm7_managed);
  publish_own(grant.until_us);assert(pt_input.gm7_managed);precision_step(fake_time);
  assert(pt_output.scope.boot==grant.boot&&!pt_input.gm7_busy);
  puts("PASS first GM opt-in cannot overtake ordinary final-precheck/unlocked side export; unchanged grant retries after real quiescence");
 }else if(test==16){
  follower();precision_poll(fake_time,&running);assert(pt_output.scope.follower&&!pt_output.utc_valid);
  fake_mutex_unlocked=expire_after_copy;struct pt_output out;output_read(&out,fake_time);
  assert(!out.scope.gm&&out.scope.follower&&!out.acquisition&&!out.utc_valid&&!out.context.source);
  puts("PASS old control deadline cannot close broadcast listening or fabricate data/map authority");
 }else if(test>=17&&test<=20){
  follower();pt_input.stopped=true;
  if(test==17)grant.root_identity=777;
  if(test==18)grant.boot=0;
  if(test==19)grant.root=0;
  if(test==20)error=-EIO;
  r2_radio_service_gm7_publish(grant,&anchor,&path,-1,fake_time,0);
  precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  assert(pt_input.stopped&&!pt_output.scope.follower&&!native_owned&&!fake_adv_creates);
  printf("PASS invalid broadcast identity/boot/ownership/error%u retains stop\n",test);
 }else if(test==21){
  follower();precision_poll(fake_time,&running);assert(pt_output.scope.follower);
  r2_radio_service_withdraw();precision_poll(fake_time,&running);assert(pt_input.stopped);
  follower();assert(!pt_input.stopped);precision_poll(fake_time,&running);
  assert(pt_output.scope.follower&&!pt_output.scope.gm&&!error);
  puts("PASS stopped GM7 receipt-origin follower resumes only on fresh exact source grant; unknown UTC bound preserved");
 }else if(test==22){
  follower();precision_step(fake_time);
  precision.servo.count=12;precision.servo.span=160000000;precision.servo.saturated=7;
  precision_step(fake_time);struct r2_gm7_follow_diagnostic d;
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);
  assert(d.stats.count==12&&d.stats.span_us==10000000&&d.stats.saturated==7);
  struct r2_gm7_timing t;r2_radio_service_gm7_timing(&t,fake_time);
  assert(!t.first_tx_lo&&!t.first_tx_hi&&!t.first_tx_derived_end_hi&&!t.last_tx&&!t.tx_phase_us&&!t.first_tx_observed_complete);
  grant=(struct r2_radio_scope){.root=0,.root_identity=17,.boot=88,.source_epoch=4,.local_source_epoch=3,.publication_epoch=5,.until_us=fake_time+1000000,.gm=true};
  publish_own(grant.until_us);precision_step(fake_time);
  assert(!pt_output.gm7.first_tx_lo&&!pt_output.gm7.first_tx_observed_complete);
  pt_output.gm7.first_tx_lo=123;pt_output.gm7.first_tx_observed_complete=true;
  follower();precision_step(fake_time);r2_radio_service_gm7_timing(&t,fake_time);
  assert(!t.first_tx_lo&&!t.first_tx_observed_complete);
  r2_radio_service_gm7_follow_diagnostic(&d,fake_time);assert(!d.stats.count&&!d.stats.saturated);
  puts("PASS coherent follower diagnostics/GM firstTX40B union clears follower->GM->follower; no diagnostic value becomes TX proof");
 }else if(test>=23&&test<=26){
  r2_radio_service_source(&(struct r2_source_clock){0},false,0);
  follower();precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
  drive_stream(test==25?200:10);assert(precision.accepted>=4&&!pt_input.source_ready&&!pt_output.scope.gm);
  if(test==23){
   uint64_t contact=gm7_direct.tick,until=precision.data_until;
   fake_time=capture_us=grant.until_us+100000;capture_tick=fake_time*16;
   precision_poll(fake_time,&running);r2_radio_service_gm7_tick(fake_time,NULL);
   assert(pt_output.scope.follower&&!pt_input.stopped&&!pt_output.utc_valid&&
       !pt_output.view.map_valid&&gm7_direct.tick==contact&&precision.data_until==until);
   puts("PASS no localGNSS/control refresh: G1 following stays listening, actual matched packet expiry stops time and never renews contact");
  }else if(test==24){
   uint64_t contact=gm7_direct.tick;uint32_t accepted=precision.accepted;
   struct r2_enrollment receive_roster=enrollment;
   struct r2_precision_packet p={.root=0,.boot=77,.source_epoch=8,.train=9,.publication=11,
       .capture_epoch=5,.flags=1,.created_ns=1600000000,.expires_ns=2000000000,
       .utc_offset_ns=INT64_C(1792800000000000000)};
   enrollment.identity[0]=99;enrollment.identity[1]=17;memset(&generic_origin,0,sizeof generic_origin);
   packet.raw+=1000000;packet.event++;assert(!generic_encode(packet.ad,&p));enrollment=receive_roster;
   fake_time=capture_us=1700000;capture_tick=(uint64_t)packet.raw+16000;queued=true;precision_step(fake_time);
   assert(gm7_direct.tick>contact&&precision.accepted==accepted&&precision.remote_capture==4);
   contact=gm7_direct.tick;packet.raw+=10000;capture_tick+=10000;queued=true;precision_step(++fake_time);
   assert(gm7_direct.tick==contact); /* Same publication is retained replay. */
   p.source_epoch=9;p.train=10;p.publication=1;
   enrollment.identity[0]=99;enrollment.identity[1]=17;memset(&generic_origin,0,sizeof generic_origin);assert(!generic_encode(packet.ad,&p));enrollment=receive_roster;
   packet.raw+=10000;capture_tick+=10000;queued=true;precision_step(++fake_time);
   assert(gm7_direct.source==9&&gm7_direct.train==10&&gm7_direct.publication==1&&
       precision.scope.source_epoch==9&&precision.train==10&&!precision.servo.fit_valid);
   puts("PASS authenticated raw G1 liveness before bad capture/fit, no duplicate renewal, new source/train resets publication floor and retires timing fit");
  }else if(test==25){
   r2_radio_service_handoff_request(99,77,8);precision_poll(fake_time,&running);
   assert(handoff.valid&&handoff.pending&&!r2_radio_service_handoff_ready(99,77,8));
   uint64_t saved=handoff.last_accepted;precision_poll(fake_time,&running);
   assert(handoff.last_accepted==saved&&handoff.pending);
   r2_radio_service_stop(fake_time);precision_poll(fake_time,&running);
   assert(!handoff.pending&&handoff.valid&&r2_radio_service_handoff_ready(99,77,8));
   r2_radio_service_handoff_request(99,78,8);precision_poll(fake_time,&running);
   assert(!handoff.valid); /* A different boot never inherits the fit. */
   puts("PASS same real fit snapshot while listening, repeat immutability, ACK only after actual stop/drain and wrong-boot invalidation");
  }else{
   uint64_t contact=gm7_direct.tick;
   packet.ad[20]^=1;packet.raw+=10000;capture_tick+=10000;queued=true;precision_step(++fake_time);
   assert(gm7_direct.tick==contact);
   struct r2_group_client empty={0};++grant.boot;
   r2_radio_service_gm7_publish(grant,&empty,&path,-1,fake_time,0);precision_step(fake_time);
   assert(!pt_output.utc_valid&&!pt_output.view.map_valid&&pt_output.scope.boot==grant.boot);
   puts("PASS bad MAC cannot renew liveness, different selected boot cannot inherit delivered timing");
  }
 }else assert(false);
 assert(!pt_lock.held);return 0;
}
