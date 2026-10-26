/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual PT/mailbox/codec/servo/native HCI lifecycle; modeled RF captures.
 * No fitted map/readiness/HOT state is seeded here.
 */
#include "gm7_cohort_radio_fixture.inc"
#include "gm7_cohort_bridge.h"
static bool running, initialized;
static struct gm7_cohort_input in;
static void parse_hex(void *out,size_t size,const char *hex) {
  assert(strlen(hex)>=size*2);
  for(unsigned i=0;i<size;i++) {
    unsigned v;assert(sscanf(hex+i*2,"%2x",&v)==1);
    ((uint8_t*)out)[i]=(uint8_t)v;
  }
}
int main(void) {
  char line[8192];
  setvbuf(stdout,NULL,_IOLBF,0);
  while(fgets(line,sizeof line,stdin)) {
    if(line[0]=='B') {
      parse_hex(&in,sizeof in,line+2);
      fake_time=capture_us=in.kernel;capture_tick=in.hf*16;
      if(!initialized) {
        assert(!r2_radio_service_init(0,&in.path));
        r2_radio_service_continuous(true);fake_adv_success=true;
        initialized=true;
      }
      r2_radio_service_source(&in.source,in.source_ready,in.source_epoch);
      r2_radio_service_gm7_publish(in.scope,&in.anchor,&in.path,in.utc_ns,
                                  in.kernel,in.resume_until);
      if(in.handoff)r2_radio_service_handoff_request(in.handoff_root,in.handoff_boot,in.handoff_epoch);
      if(in.stop)r2_radio_service_stop(in.kernel);
      if(in.activate)r2_radio_service_activate();
      precision_poll(in.kernel,&running);
      r2_radio_service_gm7_tick(in.kernel,NULL);
      struct gm7_cohort_output out={.closed=r2_radio_service_closed(),
          .ready=r2_radio_service_handoff_ready(in.root,in.boot,in.timing_epoch)};
      r2_radio_service_gm7_timing(&out.timing,in.kernel);
      r2_radio_service_gm7_contact(&out.contact[0],&out.contact[1],&out.contact[2],&out.contact[3]);
      static unsigned last_lifecycle=UINT_MAX;unsigned lifecycle=out.closed|(out.ready<<1)|(pt_input.stopped<<2);
      if(lifecycle!=last_lifecycle){printf("L %llu %u %u %u %u\n",in.hf,out.closed,out.ready,pt_input.stopped,handoff.valid);last_lifecycle=lifecycle;}
      printf("Z %llu %u %d %llu %u %llu\n",out.timing.scope.root_identity,
        out.timing.scope.gm,out.timing.tx_phase_us,out.timing.last_tx,
        out.timing.first_tx_observed_complete,out.timing.first_tx_derived_end_hi);
      printf("P ");
      for(unsigned i=0;i<sizeof out;i++)printf("%02x",((uint8_t*)&out)[i]);
      printf("\nQ %u %u %u %u %u %u\n",native_gm_started,!!sync_object,
          precision.accepted,precision.rejected,out.timing.view.locked,error);
      if(native_gm_started && pt_output.length) {
        printf("G ");
        for(unsigned i=0;i<pt_output.length;i++)printf("%02x",pt_output.payload[i]);
        putchar('\n');
      }
      puts("E");
    } else if(line[0]=='M') {
      unsigned long long root,boot,tick,epoch;unsigned timing,train,source,association;
      assert(sscanf(line+2,"%llu %llu %u %u %u %u %llu %llu",&root,&boot,&timing,&train,&source,&association,&tick,&epoch)==8);
      fake_time=capture_us=in.kernel+tick/16-in.hf;capture_tick=tick;
      struct r2_source_clock promoted={0};
      int rc=r2_radio_service_gm7_promote(&promoted,root,boot,timing,train,source,association,
          (struct r2_capture){tick,epoch,R2_CAPTURE_NOW});
      printf("M %d ",rc);
      if(!rc)for(unsigned n=0;n<sizeof promoted;n++)printf("%02x",((uint8_t*)&promoted)[n]);
      putchar('\n');puts("E");
    } else if(line[0]=='A') {
      unsigned long long root;assert(sscanf(line+2,"%llu",&root)==1);
      if(root==in.scope.root_identity && discovering) {
        struct bt_le_scan_recv_info info={.sid=12,.interval=100,.addr=&target};
        scan_received(&info,NULL);
      }
      if(root==in.scope.root_identity && sync_object && !established) {
        struct bt_le_per_adv_sync_synced_info info={.sid=12,.addr=&target};
        synced(sync_object,&info);
      }
    } else if(line[0]=='R' || line[0]=='X') {
      unsigned long long tick,available;unsigned event;
      int chars=0;assert(sscanf(line+2,"%llu %llu %u %n",&tick,&available,&event,&chars)==3);
      assert(available>=tick && available/16>=in.hf);
      fake_time=capture_us=in.kernel+available/16-in.hf;
      capture_tick=available;
      /* A selected control identity is not a PA receiver. The modeled radio
       * only routes G1 captures to an actually established matching PT owner. */
      if(line[0]=='R' && (!sync_object || !established || !active_scope.follower))
        continue;
      if(line[0]=='X' && !active_scope.gm) continue;
      packet=(struct r2_radio_record){.raw=(uint32_t)tick,.epoch=4,
          .event=(uint16_t)event,.channel=3,.length=R2_RELAY_AD_BYTES,
          .tx=line[0]=='X',.pdu_length=202};
      parse_hex(packet.ad,R2_RELAY_AD_BYTES,line+2+chars);
      assert(!queued);queued=true;
      precision_step(fake_time);
      /* Retirement of this capture belongs to this modeled radio owner even
       * if its original permission expired before processing. */
      queued=false;
    } else assert(!"bad PT command");
  }
  return 0;
}
