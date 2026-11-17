// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace watch {
// The software-maintenance wire has a fixed field set. No maps, parent lists,
// historical event populations, battery history or satellite arrays enter it.
template<class Frame,class Mesh> void control_mesh(Frame& f,const Mesh& m,const char* profile,const char* enrollment){
 f.format("W12_R2_STATUS profile=%s enrollment_sha=%s self=3 control=1 mode=%u requested=%u following=%u serving=%u owner_busy=%u off_ack=%u fault=%u group_id=%016llx key_epoch=%u client_identity=%016llx precision=UNAVAILABLE WiFi=OFF PTS=OFF\n",
  profile,enrollment,unsigned(m.mode),unsigned(m.requested),unsigned(m.following),unsigned(m.serving),unsigned(m.owner_busy),unsigned(m.off_ack),unsigned(m.fault),(unsigned long long)m.group_id,unsigned(m.key_epoch),(unsigned long long)m.client_identity);
}
struct ControlUi {uint32_t frames=0,errors=0;uint8_t face=0;bool ready=false,asleep=false,display_off=false;};
template<class Frame,class Power,class Gnss> void control_ui(Frame& f,const ControlUi& u,const Power& p,const Gnss& g,bool fine_off,bool fine_fault){
 const bool cleanup_off=p.cleanup_completed_us&&p.cleanup_completed_us==p.off_proven_us;
 f.format("TWATCH_UI version=1 width=410 height=502 ready=%u errors=%u frames=%u face=%u asleep=%u control_version=1 GNSS=FACE_AUTO precision=UNQUALIFIED display_off_proof=%u fine_off_proof=%u fine_cleanup_fault=%u",
  unsigned(u.ready),unsigned(u.errors),unsigned(u.frames),unsigned(u.face),unsigned(u.asleep),unsigned(u.display_off),unsigned(fine_off),unsigned(fine_fault));
 f.format(" ble_power_stage=%u ble_power_requested=%u ble_controller=%u ble_host_running=%u ble_power_worker=%u ble_power_off_ack=%u ble_power_generation=%u ble_power_rc=%ld ble_off_proof=%u ble_off_gen=%u ble_off_start_us=%llu ble_off_deadline_us=%llu ble_off_at_us=%llu ble_operating_fault=%u ble_cleanup_rc=%ld",
  unsigned(p.stage),unsigned(p.requested),unsigned(p.controller),unsigned(p.host_running),unsigned(p.worker_running),unsigned(p.off_ack()),unsigned(p.generation),long(p.error),unsigned(p.off_proven),unsigned(p.off_generation),
  (unsigned long long)(cleanup_off?p.cleanup_started_us:p.started_us),(unsigned long long)(cleanup_off?p.cleanup_deadline_us:p.deadline_us),(unsigned long long)p.off_proven_us,unsigned(p.operating_fault),long(p.cleanup_error));
 f.format(" shadow_version=1 shadow_requested=%u shadow_powered=%u shadow_transition=%u shadow_owner=%u shadow_uart=%u shadow_capture=%u shadow_off_proof=%u shadow_cleanup_fault=%u\n",
  unsigned(g.requested),unsigned(g.powered),unsigned(g.transition),unsigned(g.owner),unsigned(g.uart),unsigned(g.capture),unsigned(g.off),unsigned(g.cleanup_fault));
}
}
