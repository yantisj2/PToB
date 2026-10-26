/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Production cycle/results/owner. Only elapsed CPU and native RF are fake.
 */
#define main existing_service_cases
#include "gm7_service_test.c"
#undef main
#include <stdlib.h>
static bool mac_unavailable;
static uint32_t mac_elapsed;
static uint64_t after_mac(void *context) {
  (void)context;tick_to(fake_hf+mac_elapsed);
  fake_hf_valid=!mac_unavailable;
  uint64_t now=hf_read(NULL);fake_hf_valid=true;return now;
}
static struct gm7_packet announcement(void) {
  return (struct gm7_packet){.group=group.id,.key_epoch=group.epoch,
    .kind=GM7_ANNOUNCE,.flags=GM7_ELIGIBLE,.body.control={
      .sender=501,.boot=777,.sequence=1,.sent_utc_us=UINT64_C(1792800066000000),
      .sent_mono_us=66000000,.max_age_us=2750000,.source_valid_us=1500000,
      .source_gen=3,.capture_gen=4,.timing_epoch=22,.train_gen=5,
      .priority1=128,.clock_class=2,.accuracy=255,.priority2=128,.variance=65535,
      .birth_us=UINT64_C(1792800000000000),.founder=501,.term=1,
      .active=501,.active_boot=777}};
}
static void captured(struct gm7_packet *p,uint64_t at) {
  assert(!fake_rx_count);fake_rx_count=1;
  fake_rx[0]=(struct r2_gm7_rx){.raw=(uint32_t)(at*16),.epoch=4,
                                .length=GM7_CONTROL_BYTES+4};
  assert(!gm7_ad_encode(fake_rx[0].ad,sizeof fake_rx[0].ad,p,&group));
}
static void completed_primary(void) {
  struct gm7_job *j=&scheduler.job[GM7_PRIMARY];
  assert(owner.reserved && fake_native_live[GM7_PRIMARY]);
  fake_native_live[GM7_PRIMARY]=false;
  fake_native_exposed[GM7_PRIMARY]=true;
  fake_result[fake_result_count++]=(struct r2_gm7_tx){
      .ticket=j->ticket,.generation=j->generation,.buffer_version=j->buffer_version,
      .epoch=4,.primary_address=(uint32_t)((j->window.nominal+42)*16),
      .aux_address=(uint32_t)((j->window.nominal+551)*16),
      .aux_end=(uint32_t)((j->window.nominal+1400)*16),
      .slot=GM7_PRIMARY,.complete=true,.exposed=true};
  tick_to(j->window.latest_end+15000);
}
int main(int argc,char **argv) {
  assert(argc==2);unsigned mode=(unsigned)strtoul(argv[1],NULL,10);
  assert(mode<=9);
  if(mode>=3 && mode<=5) {
    setup(false);healthy_input();
    gm7_owner_clock(&owner,after_mac,NULL);
    struct gm7_packet p=announcement();uint64_t original=fake_hf-10;
    mac_unavailable=mode==3;mac_elapsed=mode==4?2750010:100;
    captured(&p,mode==5?fake_hf+1000:original);
    uint64_t accepted=(uint32_t)atomic_get(&management_accepted);
    cycle(&service_work);
    assert(!owner.failed && !owner.scope.active &&
        (uint32_t)atomic_get(&management_accepted)==accepted);
    if(mode==3)assert(owner.time_rejects==1);
    /* An advancing, genuinely fresh packet may recover. No failed sample
     * fabricated contact, consumed a control floor or renewed the old receipt. */
    mac_unavailable=false;mac_elapsed=100;p.body.control.sequence++;
    original=fake_hf-10;captured(&p,original);healthy_input();
    cycle(&service_work);
    assert(!owner.failed && owner.scope.active==501 && fake_scope.follower);
    assert(owner.selected_contact==original+6 &&
        owner.selected_until==original+6+p.body.control.max_age_us);
    uint64_t receipt=owner.selected_contact,until=owner.selected_until;
    captured(&p,fake_hf-10);cycle(&service_work);
    assert(owner.selected_contact==receipt && owner.selected_until==until);
    printf("PASS actual post-MAC/unavailable/expired/future receipt case%u; fresh "
        "packet recovers, duplicate preserves original receipt+expiry\n",mode);
    return 0;
  }
  if(mode==6 || mode==7 || mode==9) {
    setup(true);healthy_input();
    if(mode==6) {
      fake_clock_epoch++;
      cycle(&service_work);
      assert(status.error==-ESTALE && atomic_get(&stopping) &&
             owner.stopped && !owner.serving && !fake_precision_enabled);
    } else if(mode==7) {
      uint64_t until=owner.source.until_us;
      tick_to(until);assert(!gm7_owner_output(&owner,fake_hf));
      cycle(&service_work);
      assert(!owner.serving && !owner.eligible && !owner.failed);
    } else {
      owner.sequence=UINT64_MAX;
      struct gm7_packet p;
      assert(gm7_owner_control(&owner,&p,fake_hf)==-EOVERFLOW);
      gm7_owner_contact(&owner,501,777,fake_hf-10,fake_hf-1);
      assert(owner.failed && owner.reason==GM7_R_EXHAUSTED);
      struct gm7_bridge b;assert(bridge(&b));publish(b);
      char json[2048];assert(r2_gm7_diagnostic_format(json,sizeof json,0,"source",boot,true)>0);
      assert(strstr(json,"\"owner_failed\":true") && strstr(json,"\"reason\":14") &&
             strstr(json,"\"local_time_source\":\"GNSS_PPS_MAP\""));
      struct r2_ui_model m={0};r2_service_ui(&m);
      assert((m.gm7_known&R2_UI_GM7_OWNER_FAULT) && status.error==-EOVERFLOW);
    }
    printf("PASS actual epoch/source/real-failure visibility case%u\n",mode);
    return 0;
  }
  setup(false);tick_to(fake_hf+20000);healthy_input();
  cycle(&service_work);assert(owner.reserved);
  completed_primary();
  if(mode==8) {
    /* Fail only the results bridge inside the actual cycle. Its first three
     * clock reads (entry/source/post-source) succeed. The original completion
     * must remain queued, then reconcile on the next contemporary step. */
    fake_hf_reads=0;fake_hf_unavailable_at=4;
    cycle(&service_work);
    assert(fake_hf_reads==4 && fake_result_count==1 && owner.reserved &&
           !owner.serving && !owner.failed && !resource_fault);
    cycle(&service_work);
    assert(!fake_result_count && !owner.reserved && owner.serving &&
           !owner.failed && !resource_fault && gm7_owner_output(&owner,fake_hf));
    printf("PASS actual results transient unavailable before consume, "
        "original completion reconciles on next cycle without stranded reservation\n");
  } else if(mode==0) {
    /* Actual cycle results samples a newer clock than its caller. Even an
     * irrelevant direct-contact observation must not erase the whole mesh. */
    fake_direct_contact[0]=identity;fake_direct_contact[1]=boot;
    fake_direct_contact[2]=(fake_hf-100)*16;fake_direct_contact[3]=4;
    fake_hf_read_advance=10;
    cycle(&service_work);
    printf("completion/contact failed=%u reason=%u serving=%u birth=%llu\n",
        owner.failed,owner.reason,owner.serving,(unsigned long long)owner.scope.birth);
    assert(!owner.failed && owner.serving && owner.scope.birth);
  } else {
    /* Exercise the actual completion helper followed by the stop API with
     * the service's original processing snapshot. Original RF bounds remain
     * those above; they are never replaced by late processing time. */
    struct gm7_bridge b;assert(bridge(&b));uint64_t stale=b.hf;
    fake_hf_read_advance=10;assert(results(&b));
    assert(owner.serving && owner.now>stale);
    if(mode==1) {
      gm7_owner_stop(&owner,stale);
      printf("completion/stop failed=%u stopped=%u reason=%u\n",owner.failed,owner.stopped,owner.reason);
      assert(!owner.failed && owner.stopped && !owner.serving && owner.scope.birth);
    } else {
      uint64_t birth=owner.scope.birth;
      gm7_owner_contact(&owner,identity,boot,stale-100,stale);
      assert(!owner.failed && owner.scope.birth==birth && owner.serving);
    }
  }
  return 0;
}
