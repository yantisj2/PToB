/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Deterministic normal-BLE first-three-frame input for independent comparison
 * with the pre-LoRa-UI renderer. LoRa is operator OFF in this control. */
#include "ui.h"
#include <assert.h>
#include <stdio.h>
int main(int argc,char **argv){assert(argc==2);FILE *f=fopen(argv[1],"wb");assert(f);
 for(unsigned kind=0;kind<8;kind++){
  struct r2_ui_model m={.sampled_ms=1000,.platform_ready=true,.profile_supported=true,.profile_rc=true,.ble_rx_supported=true,.ble_rx_requested=true,.ble_active=true,.unit="RX-A",.root_name="GM-A",.upstream_name="BLE-A",
   .clock={.valid=true,.utc_us=36642000000LL,.pps_present=true,.pps_age_us=50000,.gps_age_us=-1,.rx_age_us=-1},
   .role=R2_UI_FOLLOWER,.scope_active=true,.authenticated_gm=true,.ble_time_selected=true,.ble_valid_until_us=2000000,.follower_pps_valid=true,.face_lock_until_us=2000000,.face_rms_ns=100,
   .precision={.authorized=true,.map_valid=true,.locked=true,.rms_valid=true,.rms_ns=100},.precision_beat_hz=16,.ble_reception_valid=true,.ble_rx_age_ms=20,.ble_delivery_per_mille=950};
  if(kind==1){m.role=R2_UI_GM;m.profile_rc=false;m.clock.source_available=m.gm_source_fresh=true;m.ble_time_selected=false;}
  if(kind==2)m.local_hot_standby=true;
  if(kind==3){m.boundary_clock=true;m.time_relay_hops=1;}
  if(kind==4)m.clock.pps_present=false;
  if(kind==5){m.face_lock_until_us=0;m.precision.locked=false;m.face_rms_ns=99999;}
  if(kind==6){m.scope_active=false;m.clock.valid=false;m.ble_time_selected=false;m.face_lock_until_us=0;}
  if(kind==7){m.temperature_valid=true;m.temperature_qc=140;m.rc_source_held=true;}
  for(unsigned page=0;page<3;page++){struct r2_ui_status s={.page=page};uint8_t fb[R2_UI_FRAME_BYTES];r2_ui_render(fb,&m,&s,1000);assert(fwrite(fb,1,sizeof fb,f)==sizeof fb);}
 }assert(!fclose(f));
}
