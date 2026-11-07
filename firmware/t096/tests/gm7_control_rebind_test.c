/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual production service/owner/codec/MAC. Physical timing and HCI are fake. */
#define main historical_service_main
#include "gm7_service_test.c"
#undef main
static unsigned withdraw_calls,last_reason;
void r2_radio_service_gm7_withdraw_cause(uint32_t reason,uint32_t generation){
 ++withdraw_calls;last_reason=reason;(void)generation;r2_radio_service_withdraw();
}
static void queue_packet(const struct gm7_packet *p){
 assert(fake_rx_count<3);struct r2_gm7_rx *r=&fake_rx[fake_rx_count++];memset(r,0,sizeof *r);
 r->raw=(uint32_t)((fake_hf-10)*16);r->epoch=4;r->length=gm7_wire_size(p->kind)+4;
 assert(!gm7_ad_encode(r->ad,sizeof r->ad,p,&group));
}
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)strtoul(argv[1],NULL,10);assert(mode<=19);
 setup(false);healthy_input();
 unsigned ann_epoch=mode==2?9:8,pt_epoch=mode==1?9:8;
 struct gm7_packet ann={.group=group.id,.key_epoch=group.epoch,.kind=GM7_ANNOUNCE,.flags=GM7_ELIGIBLE,
  .body.control={.sender=501,.boot=777,.birth_us=UINT64_C(1792800000000000),.founder=501,.term=2,
   .active=501,.active_boot=777,.timing_epoch=ann_epoch,.source_gen=100,.capture_gen=4,.train_gen=ann_epoch,
   .max_age_us=2750000,.source_valid_us=1500000,.clock_class=2,.accuracy=255,.variance=65535,.priority1=128,.priority2=128}};
 unsigned initial_generation=0;uint64_t retained_until=0;struct gm7_packet retained_ann={0};
 for(unsigned i=0;i<20;i++){
  tick_to(fake_hf+200000);healthy_input();
  if(mode==3)input.view.source_epoch=1000+i; /* Unqualified local GNSS generation alone. */
  if(mode==5&&i==5){ann_epoch=9;ann.body.control.timing_epoch=9;ann.body.control.train_gen=9;}
  if(i==5&&mode==8)ann.body.control.capture_gen=5;
  if(i==5&&mode==9)ann.body.control.source_gen=101;
  if(i==5&&mode==10){ann.body.control.sender=ann.body.control.active=502;ann.body.control.boot=ann.body.control.active_boot=778;--ann.body.control.birth_us;ann.body.control.founder=502;}
  if(i==5&&(mode==11||mode==19))ann.body.control.boot=ann.body.control.active_boot=778;
  ann.body.control.sequence=i+1;ann.body.control.sent_mono_us=fake_hf-10;
  ann.body.control.sent_utc_us=UINT64_C(1792800000000000)+fake_hf-10;
  if(mode==12){if(i==4)retained_ann=ann;else if(i>4)ann=retained_ann;}
  if(mode==13&&i>=5)ann.body.control.sent_mono_us=66000000;
  if(mode==18&&i==5){ann.kind=GM7_WITHDRAW;ann.flags=0;ann.body.control.source_valid_us=0;ann.body.control.max_age_us=1000000;}
  if(i){fake_timing=(struct r2_gm7_timing){.scope={.root=1,.root_identity=501,.boot=777,.source_epoch=pt_epoch,.publication_epoch=pt_epoch,.follower=true,.broadcast=true},.capture_gen=4,.counter_epoch=4,.last_correction=(fake_hf-100)*16};}
  if(mode==19&&i>=5)fake_timing=(struct r2_gm7_timing){0};
  if(mode==16&&i)fake_timing.scope.root_identity=502;
  if(mode==17&&i)fake_timing.counter_epoch=5;
  if((mode==4||mode==6||mode==7)&&i){
   struct gm7_owner donor={.group=group,.identity=501,.boot=777,.now=fake_hf,.serving=true,.eligible=true,.gm_capable=true,.source_observing=true,
    .source=source_at(fake_hf),.scope={.birth=ann.body.control.birth_us,.founder=501,.term=2,.active=501,.boot=777,.timing_epoch=ann_epoch,.source_gen=100,.capture_gen=4,.train=ann_epoch}};
   donor.source.source_gen=100;struct gm7_packet role;assert(!gm7_role_snapshot(&donor,&role,fake_hf));assert(role.body.role.role==GM7_W_ACTIVE);role.body.role.sequence=i+1;
   if(mode==6){role.body.role.timing_epoch=12;role.body.role.source_gen=200;role.body.role.train_gen=12;}
   if(mode==7)queue_packet(&ann);
   queue_packet(&role);
  }
  if(mode!=7||!i){
   if(mode==15&&i>=5){group.psk[0]^=2;queue_packet(&ann);group.psk[0]^=2;}
   else queue_packet(&ann);
   if(mode==14&&i>=5)fake_rx[fake_rx_count-1].ad[fake_rx[fake_rx_count-1].length-1]^=1;
  }
  cycle(&service_work);assert(!fake_rx_count&&!resource_fault&&!owner.failed);
  if(i==4&&(mode>=12&&mode<=15))retained_until=owner.peer[0].received.control_until;
  if(i>=5&&(mode>=12&&mode<=15))assert(owner.peer[0].received.control_until==retained_until);
  if(mode==11&&i>=5&&i<=16)assert(owner.scope.boot==777&&owner.peer[0].quarantined&&owner.peer[1].quarantined);
#ifdef CANDIDATE_FIXED
  bool mismatch=(mode==1||mode==2||mode==17)||(mode==5&&i>=5)||(mode==8&&i>=5)||(mode==10&&i>=5)||(mode==11&&owner.scope.boot==778)||mode==16;
  if(i&&mismatch){struct r2_gm7_timing tempting=fake_timing;tempting.view.map_valid=tempting.view.fit_valid=tempting.view.locked=true;
   struct gm7_preparation refused;struct gm7_bridge bridge_now;assert(bridge(&bridge_now));preparation(&refused,bridge_now,&tempting);assert(!refused.scope.active&&!refused.map_valid&&!refused.fit_valid&&!refused.phase_available);}
#endif
  if(mode==18&&i==5){assert(owner.withdrawn&&last_reason==GM7_R_WITHDRAW&&withdraw_calls==1);break;}

  if(!i)initial_generation=owner.event_generation;
  printf("CYCLE %u mode%u ann_epoch%u pt_epoch%u owner_epoch%u selected_epoch%u event_gen%u stops%u serving%u\n",i,mode,ann_epoch,i?pt_epoch:0,owner.scope.timing_epoch,owner.selected.timing_epoch,owner.event_generation,withdraw_calls,owner.serving);
 }
#ifdef CANDIDATE_FIXED
 if(mode==5||mode==8||mode==9)assert(withdraw_calls==1&&owner.event_generation==initial_generation+1);
 else if(mode==10||mode==19)assert(!withdraw_calls&&owner.event_generation==initial_generation+1&&owner.scope.boot==778);
 else if(mode==11)assert(!withdraw_calls&&owner.event_generation==initial_generation&&owner.scope.boot==777);
 else if(mode==18)assert(withdraw_calls==1&&last_reason==GM7_R_WITHDRAW);
 else assert(!withdraw_calls&&owner.event_generation==initial_generation);
#else
 if(mode==1||mode==2)assert(withdraw_calls==19&&owner.event_generation>=initial_generation+19);
 else if(mode==5||mode==8)assert(withdraw_calls==15&&owner.event_generation>=initial_generation+15);
 else if(mode==9)assert(withdraw_calls==1&&owner.event_generation==initial_generation+1);
 else if(mode==10||mode==19)assert(!withdraw_calls&&owner.event_generation==initial_generation+1&&owner.scope.boot==778);
 else if(mode==11)assert(!withdraw_calls&&owner.event_generation==initial_generation&&owner.scope.boot==777);
 else if(mode==18)assert(withdraw_calls==1&&last_reason==GM7_R_WITHDRAW);
 else assert(!withdraw_calls&&owner.event_generation==initial_generation);
#endif
 printf("PRODUCTION_CONTROL_REBIND mode%u stops%u finalgen%u initialgen%u\n",mode,withdraw_calls,owner.event_generation,initial_generation);
}
