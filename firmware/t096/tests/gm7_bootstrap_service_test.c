/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual ordinary-control service integration; fake native completion only. */
#define main historical_service_main
#include "gm7_service_test.c"
#undef main
#include "gm7_bootstrap_fixture.h"
int main(int argc,char **argv){
 assert(argc==2);setup(false);healthy_input();
 struct gm7_packet ann={.group=group.id,.key_epoch=group.epoch,.kind=GM7_ANNOUNCE,.flags=GM7_ELIGIBLE,
  .body.control={.sender=501,.boot=777,.sequence=1,.sent_utc_us=UINT64_C(1792800066000000),
   .sent_mono_us=66000000,.max_age_us=2750000,.source_valid_us=1500000,.source_gen=3,.capture_gen=4,.timing_epoch=22,.train_gen=5,
   .priority1=128,.clock_class=2,.accuracy=255,.priority2=128,.variance=65535,
   .birth_us=UINT64_C(1792800000000000),.founder=501,.term=1,.active=501,.active_boot=777}};
 struct r2_gm7_rx *rx=&fake_rx[fake_rx_count++];memset(rx,0,sizeof *rx);
 rx->raw=(uint32_t)((fake_hf-10)*16);rx->epoch=4;rx->length=GM7_CONTROL_BYTES+4;
 assert(!gm7_ad_encode(rx->ad,sizeof rx->ad,&ann,&group));
 cycle(&service_work);
 assert(management_accepted&&owner.scope.active==501&&!owner.serving&&!owner.armed);
 assert(fake_scope.follower&&fake_scope.source_epoch==22&&fake_scope.publication_epoch==5&&fake_scope.remote_capture_epoch==4);
 assert(!fake_client.source&&fake_resume_until==INT64_MAX);
 for(unsigned i=0;i<GM7_JOB_SLOTS;i++)assert(!fake_native_live[i]||scheduler.job[i].kind==GM7_STATUS||scheduler.job[i].kind==GM7_ROLE||scheduler.job[i].kind==GM7_GNSS);
 struct gm7_bootstrap_fixture f={.roster=fake_path,.anchor=fake_client,.scope=fake_scope,.at=fake_kernel};
 FILE *file=fopen(argv[1],"wb");assert(file&&fwrite(&f,sizeof f,1,file)==1&&!fclose(file));
 uint64_t contact=owner.selected_contact;
 rx=&fake_rx[fake_rx_count++];memset(rx,0,sizeof *rx);rx->raw=(uint32_t)(fake_hf*16);rx->epoch=4;rx->length=GM7_CONTROL_BYTES+4;
 assert(!gm7_ad_encode(rx->ad,sizeof rx->ad,&ann,&group));tick_to(fake_hf+1000);cycle(&service_work);
 assert(owner.selected_contact==contact);
 tick_to(fake_hf+1600000);cycle(&service_work);
 assert(fake_scope.follower&&!fake_client.source&&!owner.armed);
 puts("PASS ordinary ANN selects persistent identity/boot listening through normal G1 path; no anchor/session/source lease and duplicate cannot renew contact");return 0;
}
