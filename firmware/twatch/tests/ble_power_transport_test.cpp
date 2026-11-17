// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual watch native_transport lifecycle, with synthetic pinned-SDK calls.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <algorithm>
uint64_t fake_now=1000;
namespace watch::energy {bool ble(bool required);}
#include "../../w12/main/native_transport.cpp"
FakeHsCfg ble_hs_cfg;
int fixture_ensure_rc=0,r2_sync_rc=0;uint8_t r2_sync_site=2;
int controller=0,init_rc=0,stop_rc=0,deinit_rc=0;
bool quiet=true,stop_late=false,host_expected=false,post_race=false,toggle_during_init=false;
unsigned init_calls=0,stop_calls=0,deinit_calls=0,service_calls=0,r2_stops=0,aux_stops=0,deleted=0;
std::vector<int> order;
std::vector<ble_npl_event*> events;
std::vector<TaskHandle_t> tasks;TaskHandle_t current_task=nullptr;
unsigned task_sequence=1,worker_creations=0,allocation_failures=0;
bool create_race=false,create_toggle=false;
bool reserving_host=false,host_allocation_fail=false,service_fail=false;
bool capture_retired=true,rearm_ok=true,queue_blocked=false;
bool cpu_ble=true,cpu_fail=false;
bool preempt_prepare=false;
unsigned host_run_calls=0;
namespace watch::energy {bool ble(bool required){if(cpu_fail)return false;cpu_ble=required;return true;}}
struct CreationBlocked{};
namespace v12 {
DirectStatus direct_status(){return {};}
bool direct_watch_retire_unused(){return true;}
void direct_host_poll(){}void direct_host_reset(){}void direct_stop(){++aux_stops;}
void diagnostic_tx_poll(){}void diagnostic_tx_reset(){}void diagnostic_tx_init(){}void diagnostic_tx_stop(){++aux_stops;}
DiagnosticTxStatus diagnostic_tx_status(){return {};}
bool diagnostic_tx_watch_retire(){return true;}
bool r2_host_init(){++service_calls;return !service_fail;}
bool r2_host_sync(){assert(transport_watch_ble_admit_allowed(true)||!watch_power.requested||watch_power.stage==watch::BlePower::Draining);return !r2_sync_rc;}
int r2_watch_host_sync_error(uint8_t* site){*site=r2_sync_site;return r2_sync_rc;}
void r2_host_poll(){}void r2_host_cleanup(){}
void r2_host_reset(int){}void r2_watch_host_resume(bool){}
bool r2_watch_host_suspend(){return quiet;}
void r2_watch_host_fenced(){}
bool r2_watch_capture_retired(){return capture_retired;}
bool r2_watch_unstarted_quiet(){return quiet&&capture_retired;}
bool r2_watch_host_rearm(){return rearm_ok&&quiet&&capture_retired;}
void r2_stop(){++r2_stops;}
void fault_watchdog_host_expected(bool value){host_expected=value;}
}
void vTaskDelete(void*){++deleted;if(current_task){auto i=std::find(tasks.begin(),tasks.end(),current_task);assert(i!=tasks.end());tasks.erase(i);}}
void vTaskDelay(unsigned){}
void xTaskNotifyGive(TaskHandle_t h){assert(h&&std::find(tasks.begin(),tasks.end(),h)!=tasks.end());}
unsigned ulTaskNotifyTake(int,unsigned){if(reserving_host||(v12::watch_power_creating&&!v12::watch_power_task))throw CreationBlocked{};return 1;}
int xTaskCreatePinnedToCore(void(*fn)(void*),const char*,unsigned stack,void* arg,unsigned,TaskHandle_t* h,unsigned core){
 if(fn==v12::watch_power_worker){assert(stack==4096&&core==0);if(allocation_failures){--allocation_failures;return 0;}++worker_creations;
  if(create_race){create_race=false;assert(v12::watch_power_creating&&!v12::watch_power_task);v12::transport_watch_ble_poll();
   const auto sdk_calls=init_calls;try{fn(nullptr);assert(false);}catch(const CreationBlocked&){}assert(init_calls==sdk_calls);
  }
  if(create_toggle){create_toggle=false;assert(v12::transport_watch_ble_request(true,fake_now));assert(v12::transport_watch_ble_request(false,fake_now));}
 }
 if(fn==v12::host){
  assert(stack==8192&&core==0);if(host_allocation_fail||controller)return 0; // SDK init fragments the large block.
  order.push_back(4);reserving_host=true;const auto runs=host_run_calls;
  try{fn(arg);assert(false);}catch(const CreationBlocked&){}reserving_host=false;assert(host_run_calls==runs);
 }
 *h=reinterpret_cast<void*>(uintptr_t(++task_sequence));tasks.push_back(*h);return 1;
}
void retire_worker(){assert(v12::watch_power_task);current_task=v12::watch_power_task;v12::watch_power_worker(nullptr);current_task=nullptr;assert(!v12::watch_power_task&&!v12::watch_power.worker_running);}
int esp_bt_controller_get_status(){return controller;}
int nimble_port_init(){++init_calls;order.push_back(1);assert(cpu_ble&&!controller&&v12::watch_host_task&&v12::watch_power.host_running);if(!init_rc)controller=2;
 if(toggle_during_init){toggle_during_init=false;assert(v12::transport_watch_ble_request(false,fake_now));assert(v12::transport_watch_ble_request(true,fake_now+1));}
 return init_rc;}
void nimble_port_run(){++host_run_calls;}
int nimble_port_stop(){
 ++stop_calls;order.push_back(2);assert(!v12::watch_event_gate&&!v12::watch_clock_readers);
 if(stop_late){fake_now=v12::watch_power.deadline_us;assert(v12::transport_watch_ble_status().stage==watch::BlePower::Fault);}
 if(!stop_rc)v12::host(reinterpret_cast<void*>(uintptr_t(v12::watch_host_epoch)));return stop_rc;
}
int nimble_port_deinit(){
 ++deinit_calls;order.push_back(3);assert(v12::watch_host_exited&&!v12::watch_posters&&!v12::watch_clock_readers);
 assert(!v12::poll_callout.live&&!v12::command_event.live&&!v12::g1_cleanup_event.live&&!v12::watch_power_event.live);
 if(!deinit_rc)controller=0;return deinit_rc;
}
void ble_npl_eventq_put(void*,ble_npl_event* e){assert(e->live);if(!e->queued){e->queued=true;events.push_back(e);
 if(preempt_prepare&&e==&v12::watch_power_event){preempt_prepare=false;e->queued=false;e->fn(e);}
}}
void ble_npl_eventq_remove(void*,ble_npl_event* e){e->queued=false;}
bool ble_npl_eventq_is_empty(void*){return !queue_blocked;}
void pump(){auto pending=events;events.clear();for(auto* e:pending)if(e->queued){e->queued=false;e->fn(e);}}
int ble_gap_disc_cancel(){return 0;}
int ble_gap_periodic_adv_sync_create_cancel(){return 0;}
int ble_gap_periodic_adv_sync_terminate(uint16_t){return 0;}
int ble_gap_periodic_adv_sync_create(const ble_addr_t*,uint8_t,const ble_gap_periodic_sync_params*,int(*)(ble_gap_event*,void*),void*){return 0;}
int ble_gap_ext_disc(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,const ble_gap_ext_disc_params*,void*,int(*)(ble_gap_event*,void*),void*){return 0;}
void fresh(){
 using namespace v12;fake_now=1000;controller=init_rc=stop_rc=deinit_rc=0;quiet=true;stop_late=false;events.clear();order.clear();
 init_calls=stop_calls=deinit_calls=service_calls=r2_stops=aux_stops=deleted=0;
 tasks.clear();current_task=nullptr;worker_creations=allocation_failures=0;fixture_ensure_rc=r2_sync_rc=0;
 host_allocation_fail=service_fail=reserving_host=false;host_run_calls=0;
 capture_retired=rearm_ok=true;queue_blocked=false;
 cpu_ble=true;cpu_fail=false;
 preempt_prepare=false;
 watch_port_initialized=watch_port_init_attempted=watch_events_initialized=watch_host_run_entered=watch_sync_callback_seen=watch_off_owners_retired=false;watch_sdk_operation=0;watch_host_task=nullptr;
 watch_power_task=nullptr;watch_power_creating=false;watch_power_retry_us=0;
 watch_power={};watch_host_epoch=0;watch_event_gate=watch_npl_live=watch_host_exited=watch_hs_synced=false;
 watch_posters=watch_clock_readers=watch_admitters=0;ready=false;host_depth=0;client={};lease={};snapshot={};
 command_pending=command_stop=false;stop_generation=stop_processed=0;stop_exhausted=false;lease_end=0;
 assert(transport_init());watch_power_step();assert(watch_power.stage==watch::BlePower::Sync&&controller==2&&watch_power.host_running);
 assert(order.size()>=2&&order[0]==4&&order[1]==1); // Full host allocation BEFORE SDK pools.
 retire_worker();transport_watch_ble_poll();assert(!watch_power_task); // Sync owns no4KB worker.
 sync();assert(watch_power.stage==watch::BlePower::Active&&ready&&host_expected);
 assert(worker_creations==1);
}
void off(){
 using namespace v12;assert(transport_watch_ble_request(false,fake_now));pump();watch_power_step();pump();
 assert(watch_power.stage==watch::BlePower::HostStop&&!ready&&!host_expected);
 watch_power_step();assert(watch_power.stage==watch::BlePower::HostExit);watch_power_step();
 assert(watch_power.off_ack()&&controller==0&&!watch_power.host_running&&!watch_npl_live);
 retire_worker();
}
int main(){
 using namespace v12;
 fresh();assert(transport_watch_ble_clock_begin());assert(watch_clock_readers==1);
 assert(transport_watch_ble_request(false,fake_now));pump();watch_power_step();pump();
 watch_power_step();assert(!stop_calls&&watch_power.stage==watch::BlePower::HostStop);
 assert(!transport_watch_ble_clock_begin());transport_watch_ble_clock_end();watch_power_step();watch_power_step();
 assert(watch_power.off_ack()&&init_calls==1&&stop_calls==1&&deinit_calls==1);
 assert(!cpu_ble);
 const auto stopped_aux=aux_stops;transport_stop();transport_watch_ble_stop_aux();transport_g1_cleanup_schedule();
 assert(aux_stops==stopped_aux&&!command_pending); // Maintenance on OFF never posts to freed NPL.
 const auto old_epoch=watch_host_epoch;assert(transport_watch_ble_request(true,fake_now+1));watch_power_step();sync();
 assert(init_calls==2&&service_calls==2&&watch_power.stage==watch::BlePower::Active&&watch_host_epoch!=old_epoch);
 assert(cpu_ble);
 ble_gap_event stale{};stale.type=BLE_GAP_EVENT_PERIODIC_SYNC;stale.periodic_sync.status=0;stale.periodic_sync.sync_handle=9;
 const auto old_late=late;gap(&stale,reinterpret_cast<void*>(uintptr_t(old_epoch)));assert(late==old_late+1&&!client.synced);
 fresh();quiet=false;assert(transport_watch_ble_request(false,fake_now));pump();const auto original=watch_power.deadline_us;
 watch_power_step();pump();assert(watch_power.stage==watch::BlePower::Draining&&!stop_calls);
 assert(transport_watch_ble_request(true,fake_now+100));assert(watch_power.deadline_us==original);
 fake_now+=200;quiet=true;watch_power_step();pump();watch_power_step();watch_power_step();
 assert(watch_power.stage==watch::BlePower::Init&&controller==0);watch_power_step();sync();assert(watch_power.stage==watch::BlePower::Active);
 fresh();quiet=false;assert(transport_watch_ble_request(false,fake_now));pump();watch_power_step();pump();
 assert(!transport_request(Mode::Full,1000,7)&&!transport_watch_ble_admit_begin());
 fake_now=watch_power.deadline_us;watch_power_step();assert(watch_power.stage==watch::BlePower::Fault&&!stop_calls&&!watch_power.off_ack());
 fresh();assert(transport_watch_ble_admit_begin());assert(transport_watch_ble_request(false,fake_now));pump();watch_power_step();pump();
 assert(watch_power.stage==watch::BlePower::Draining&&!stop_calls);transport_watch_ble_admit_end();off();
 fresh();stop_rc=7;assert(transport_watch_ble_request(false,fake_now));pump();watch_power_step();pump();watch_power_step();
 assert(watch_power.stage==watch::BlePower::Fault&&watch_power.error==7&&!deinit_calls&&!watch_power.off_ack());
 fresh();stop_late=true;assert(transport_watch_ble_request(false,fake_now));pump();watch_power_step();pump();watch_power_step();
 assert(watch_power.stage==watch::BlePower::Fault&&!deinit_calls&&!watch_power.off_ack());
 fresh();deinit_rc=9;assert(transport_watch_ble_request(false,fake_now));pump();watch_power_step();pump();watch_power_step();watch_power_step();
 assert(watch_power.stage==watch::BlePower::Fault&&watch_power.error==9&&!watch_power.off_ack());
 fresh();off();init_rc=11;assert(transport_watch_ble_request(true,fake_now));watch_power_step();assert(watch_power.stage==watch::BlePower::Fault&&watch_power.error==11);
 // OFF->ON inside SDK initialization must finish the original OFF before ON.
 fresh();off();assert(transport_watch_ble_request(true,fake_now));toggle_during_init=true;watch_power_step();
 assert(watch_power.stage==watch::BlePower::Draining&&watch_power.requested);const auto init_stop_deadline=watch_power.deadline_us;
 sync();assert(watch_hs_synced&&watch_power.stage==watch::BlePower::Draining&&watch_power.deadline_us==init_stop_deadline);
 pump();watch_power_step();pump();watch_power_step();watch_power_step();assert(watch_power.stage==watch::BlePower::Init&&controller==0);
 watch_power_step();sync();assert(watch_power.stage==watch::BlePower::Active&&controller==2);
 // OFF before first SDK initialization needs no controller task or queue.
 fresh();off();watch_power.stage=watch::BlePower::Init;watch_power.requested=true;watch_npl_live=false;watch_power.host_running=false;
 assert(transport_watch_ble_request(false,fake_now));watch_power_step();assert(watch_power.off_ack());
 // Settled Active and Off retain no4KB worker. A toggle during delayed idle
 // cleanup retries allocation under the original stop deadline, once only.
 fresh();allocation_failures=1;assert(transport_watch_ble_request(false,fake_now));
 const auto allocation_deadline=watch_power.deadline_us;assert(!watch_power_task&&watch_power.worker_alloc_failures==1);
 transport_watch_ble_poll();assert(!watch_power_task&&watch_power.deadline_us==allocation_deadline);
 fake_now+=10000;create_race=create_toggle=true;transport_watch_ble_poll();assert(watch_power_task&&worker_creations==2&&!watch_power.requested&&watch_power.deadline_us==allocation_deadline);
 pump();watch_power_step();pump();watch_power_step();watch_power_step();assert(watch_power.off_ack());retire_worker();
 const auto retired_count=worker_creations;watch_power_notify();transport_watch_ble_poll();assert(worker_creations==retired_count);
 fresh();allocation_failures=100;assert(transport_watch_ble_request(false,fake_now));const auto no_memory_deadline=watch_power.deadline_us;
 fake_now=no_memory_deadline;transport_watch_ble_poll();assert(watch_power.stage==watch::BlePower::Fault&&!watch_power_task&&!watch_power.off_ack());
 // Exact Sync source and SDK error remain observable rather than aggregate-5.
 fresh();off();assert(transport_watch_ble_request(true,fake_now));watch_power_step();fixture_ensure_rc=77;sync();
 assert(watch_power.stage==watch::BlePower::Fault&&watch_power.error==77&&watch_power.sync_site==1&&watch_power.sync_error==77);retire_worker();
 fresh();off();assert(transport_watch_ble_request(true,fake_now));watch_power_step();r2_sync_rc=88;sync();
 assert(watch_power.stage==watch::BlePower::Fault&&watch_power.error==88&&watch_power.sync_site==2&&watch_power.sync_error==88);retire_worker();
 fresh();off();assert(transport_watch_ble_request(true,fake_now));watch_power_step();retire_worker();
 assert(watch_power.stage==watch::BlePower::Sync&&!watch_power_task);const auto sync_deadline=watch_power.deadline_us;
 transport_watch_ble_poll();assert(!watch_power_task&&watch_power.deadline_us==sync_deadline);
 assert(transport_watch_ble_request(false,fake_now));const auto sync_stop_deadline=watch_power.deadline_us;
 assert(transport_watch_ble_request(true,fake_now+1)&&watch_power.deadline_us==sync_stop_deadline);
 sync();pump();watch_power_step();pump();watch_power_step();watch_power_step();
 assert(watch_power.stage==watch::BlePower::Init&&controller==0);watch_power_step();retire_worker();sync();assert(watch_power.stage==watch::BlePower::Active);
 fresh();off();host_allocation_fail=true;const auto before_sdk=init_calls;
 assert(transport_watch_ble_request(true,fake_now));watch_power_step();assert(init_calls==before_sdk&&watch_power.stage==watch::BlePower::Fault&&watch_power.error==-12&&!watch_power.host_running);
 fresh();off();init_rc=11;assert(transport_watch_ble_request(true,fake_now));watch_power_step();
 const auto runs_before_cancel=host_run_calls;assert(watch_power.host_running&&!watch_host_start_allowed);
 host(reinterpret_cast<void*>(uintptr_t(watch_host_epoch)));assert(!watch_power.host_running&&watch_host_exited&&!watch_host_task&&host_run_calls==runs_before_cancel);
 fresh();off();service_fail=true;assert(transport_watch_ble_request(true,fake_now));watch_power_step();
 const auto runs_before_services=host_run_calls;host(reinterpret_cast<void*>(uintptr_t(watch_host_epoch)));
 assert(watch_power.stage==watch::BlePower::Fault&&!watch_power.host_running&&host_run_calls==runs_before_services);
 // Failure before any SDK init can close safely, retain its immutable first
 // capsule, then accept a later fresh generation after proven OFF.
 fresh();off();host_allocation_fail=true;assert(transport_watch_ble_request(true,fake_now));watch_power_step();
 const auto first_error=watch_power.error;const auto first_generation=watch_power.fault_generation;const auto first_at=watch_power.error_at_us;
 assert(first_error==-12&&watch_power.operating_fault);retire_worker();transport_watch_ble_poll();
 const auto cleanup_deadline=watch_power.cleanup_deadline_us;assert(watch_power.cleanup_active&&cleanup_deadline-fake_now==2000000);
 watch_power_step();assert(transport_watch_ble_off_proven()&&watch_power.error==first_error&&watch_power.operating_fault);retire_worker();
 const auto off_generation=watch_power.generation;assert(transport_watch_ble_request(false,fake_now+1));
 assert(watch_power.generation==off_generation&&watch_power.off_generation==off_generation&&transport_watch_ble_off_proven());
 capture_retired=false;assert(!transport_watch_ble_off_proven()&&!transport_watch_ble_recover());capture_retired=true;
 host_allocation_fail=false;assert(transport_watch_ble_request(true,fake_now+2));watch_power_step();retire_worker();sync();
 assert(watch_power.stage==watch::BlePower::Active&&!watch_power.operating_fault&&watch_power.error==first_error&&watch_power.fault_generation==first_generation&&watch_power.error_at_us==first_at);
 // A fully initialized port with a never-entered reserved host skips the
 // host-dependent stop API. No app timer was ever armed before cancellation.
 fresh();off();service_fail=true;assert(transport_watch_ble_request(true,fake_now));watch_power_step();assert(!poll_callout.armed&&!watch_host_run_entered);
 host(reinterpret_cast<void*>(uintptr_t(watch_host_epoch)));retire_worker();const auto stops_before_direct=stop_calls;
 transport_watch_ble_poll();watch_power_step();assert(watch_power.stage==watch::BlePower::HostExit);
 watch_power_step();assert(transport_watch_ble_off_proven()&&stop_calls==stops_before_direct&&!watch_power.cleanup_error);retire_worker();
 // Unknown partial initialization is not converted into a fake OFF receipt.
 fresh();off();init_rc=11;assert(transport_watch_ble_request(true,fake_now));watch_power_step();host(reinterpret_cast<void*>(uintptr_t(watch_host_epoch)));retire_worker();
 transport_watch_ble_poll();watch_power_step();assert(watch_power.error==11&&watch_power.cleanup_error==-95&&!transport_watch_ble_off_proven());
 const auto failed_cleanup_deadline=watch_power.cleanup_deadline_us;transport_watch_ble_request(false,fake_now+10);transport_watch_ble_poll();
 assert(watch_power.cleanup_deadline_us==failed_cleanup_deadline&&!transport_watch_ble_recover());retire_worker();
 // App address failure after the actual host has entered uses host-tail
 // drainage and an empty-queue barrier; it never skips a running host.
 fresh();off();assert(transport_watch_ble_request(true,fake_now));watch_power_step();retire_worker();watch_host_run_entered=true;fixture_ensure_rc=77;sync();
 transport_watch_ble_poll();pump();watch_power_step();pump();assert(watch_power.stage==watch::BlePower::HostStop);
 queue_blocked=true;const auto stops_before_queue=stop_calls;watch_power_step();assert(stop_calls==stops_before_queue&&!transport_watch_ble_off_proven());
 queue_blocked=false;watch_power_step();watch_power_step();assert(transport_watch_ble_off_proven()&&watch_power.error==77&&!watch_power.cleanup_error);retire_worker();
 // Stop failure remains terminal; no new cleanup lease retries that API.
 fresh();stop_rc=9;assert(transport_watch_ble_request(false,fake_now));pump();watch_power_step();pump();watch_power_step();
 const auto stop_failure_deadline=watch_power.deadline_us;transport_watch_ble_poll();assert(!watch_power.cleanup_active&&watch_power.cleanup_error==9&&watch_power.deadline_us==stop_failure_deadline&&!transport_watch_ble_off_proven());
 // FreeRTOS CPU0 host21 preempts worker2 inside eventq_put before the posting
 // reservation retires. This is the exact observed62 Draining timeout.
 fresh();assert(transport_watch_ble_request(false,fake_now));pump();
 preempt_prepare=true;watch_power_step();assert(watch_power.stage==watch::BlePower::HostStop&&!watch_event_gate&&!watch_posters);
 watch_power_step();watch_power_step();assert(transport_watch_ble_off_proven());retire_worker();
 // Other already-reserved producers may finish enqueueing after gate close,
 // but no SDK stop/free occurs until every one has actually returned.
 fresh();assert(transport_watch_ble_request(false,fake_now));pump();++watch_posters;
 preempt_prepare=true;watch_power_step();assert(watch_power.stage==watch::BlePower::HostStop&&!watch_event_gate&&watch_posters==1);
 watch_power_step();assert(!stop_calls);--watch_posters;watch_power_step();watch_power_step();assert(transport_watch_ble_off_proven());retire_worker();
 std::puts("PASS actual native transport: SDK stop/exit/NPLdeinit/controllerdeinit/reinit+services, original deadlines, clock-reader/admission/post fences, stale epoch, rapid toggle, OFF maintenance, drain/stop/deinit/init/late failures; no hardware");
}
