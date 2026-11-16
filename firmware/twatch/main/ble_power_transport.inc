// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Included only in the watch's serialized transport implementation.
#if defined(ESP_PLATFORM) && (!CONFIG_BT_NIMBLE_USE_ESP_TIMER || !CONFIG_ESP_TIMER_TASK_AFFINITY_CPU0 || CONFIG_BT_NIMBLE_PINNED_TO_CORE != 0)
#error Watch BLE lifecycle requires pinned CPU0 ESP timers and host
#endif
bool watch_retired_resources_locked(){
 return !watch_sdk_operation&&!watch_port_init_attempted&&!watch_port_initialized&&!watch_events_initialized&&
  !watch_npl_live&&!watch_host_task&&watch_host_exited&&!watch_power.host_running&&
  !watch_posters&&!watch_clock_readers&&!watch_admitters&&!host_depth&&watch_off_owners_retired&&
  esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_IDLE;
}
bool watch_aux_physical_quiet(){
 const auto tx=diagnostic_tx_status();const auto direct=direct_status();
 return r2_watch_unstarted_quiet()&&!tx.requested&&!tx.periodic&&!tx.discovery&&!tx.owner_busy&&
  direct.handle==UINT16_MAX&&!direct.encrypted&&!direct.confirmed&&
  (direct.stage==DirectStage::Off||(direct.stage==DirectStage::Fault&&!direct.incarnation));
}
bool watch_rearm_after_off(){
 if(!r2_watch_host_rearm()||!diagnostic_tx_watch_retire()||!direct_watch_retire_unused())return false;
 portENTER_CRITICAL(&mux);
 // The last physical-OFF receipt quarantines old queue intents. Keep failure
 // counts and lease deadlines; never replay a stopped generation after reinit.
 client.requested=client.scan=client.pending=client.synced=false;client.state=Lifecycle::Off;
 command_pending=command_stop=false;stop_processed=stop_generation;lease_end=0;lease.stopped(micros());
 portEXIT_CRITICAL(&mux);publish();return true;
}
void watch_power_prepare(ble_npl_event*){
 portENTER_CRITICAL(&mux);watch_power.observe(micros());const bool preparing=watch_power.stage==watch::BlePower::Preparing;portEXIT_CRITICAL(&mux);
 if(!preparing)return;
 if(!r2_watch_host_suspend()){
  portENTER_CRITICAL(&mux);if(watch_power.stage==watch::BlePower::Preparing)watch_power.stage=watch::BlePower::Draining;portEXIT_CRITICAL(&mux);return;
 }
 // This callback is after all prior host work. Close every external event
 // producer before stopping the callout; late queued callbacks are fenced.
 portENTER_CRITICAL(&mux);
 // eventq_put may wake this higher-priority host before its posting worker
 // retires its own poster reservation. Fence now; only the worker may wait
 // for poster retirement and then stop/free the SDK objects.
 if(command_pending||watch_admitters){watch_power.stage=watch::BlePower::Draining;portEXIT_CRITICAL(&mux);return;}
 ready=false;watch_event_gate=false;portEXIT_CRITICAL(&mux);
 r2_watch_host_fenced();
 ble_npl_callout_stop(&poll_callout);
 ble_npl_eventq_remove(nimble_port_get_dflt_eventq(),&command_event);
 ble_npl_eventq_remove(nimble_port_get_dflt_eventq(),&g1_cleanup_event);
 fault_watchdog_host_expected(false);
 portENTER_CRITICAL(&mux);watch_off_owners_retired=true;portEXIT_CRITICAL(&mux);
 portENTER_CRITICAL(&mux);watch_power.observe(micros());if(watch_power.stage!=watch::BlePower::Fault)watch_power.stage=watch::BlePower::HostStop;portEXIT_CRITICAL(&mux);
 publish();watch_power_notify();
}
bool watch_start_stack(){
 portENTER_CRITICAL(&mux);const bool retired=watch_retired_resources_locked();portEXIT_CRITICAL(&mux);
 if(watch_host_epoch&&(!retired||!r2_watch_capture_retired()||!watch_rearm_after_off())){portENTER_CRITICAL(&mux);watch_power.fail(-16,micros());portEXIT_CRITICAL(&mux);return false;}
 if(watch_host_epoch==UINT32_MAX){portENTER_CRITICAL(&mux);watch_power.fail(-75,micros());portEXIT_CRITICAL(&mux);return false;}
 ++watch_host_epoch;
 // Reserve all8192 host stack bytes before the controller/host pools split
 // the remaining internal heap. The task blocks before every NPL operation.
 portENTER_CRITICAL(&mux);watch_host_start_allowed=false;watch_host_exited=true;watch_host_run_entered=false;watch_sync_callback_seen=false;watch_off_owners_retired=false;portEXIT_CRITICAL(&mux);
 TaskHandle_t reserved=nullptr;
 if(xTaskCreatePinnedToCore(host,"nimble_host",CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,reinterpret_cast<void*>(uintptr_t(watch_host_epoch)),configMAX_PRIORITIES-4,&reserved,NIMBLE_CORE)!=pdPASS){
  portENTER_CRITICAL(&mux);watch_power.fail(-12,micros());portEXIT_CRITICAL(&mux);return false;
 }
 portENTER_CRITICAL(&mux);watch_host_task=reserved;watch_host_exited=false;watch_power.host_running=true;
 watch_sdk_operation=1;watch_port_init_attempted=true;portEXIT_CRITICAL(&mux);
 const int rc=nimble_port_init();
 portENTER_CRITICAL(&mux);watch_sdk_operation=0;watch_port_initialized=!rc;portEXIT_CRITICAL(&mux);
 if(rc){portENTER_CRITICAL(&mux);watch_power.fail(rc,micros());portEXIT_CRITICAL(&mux);watch_host_release(false);return false;}
 if(!r2_host_init()){portENTER_CRITICAL(&mux);watch_power.fail(-5,micros());portEXIT_CRITICAL(&mux);watch_host_release(false);return false;}
 ble_hs_cfg.reset_cb=reset;ble_hs_cfg.sync_cb=sync;
 ble_hs_cfg.sm_io_cap=BLE_HS_IO_NO_INPUT_OUTPUT;ble_hs_cfg.sm_sc=1;
 ble_hs_cfg.sm_mitm=ble_hs_cfg.sm_bonding=ble_hs_cfg.sm_our_key_dist=ble_hs_cfg.sm_their_key_dist=0;
 diagnostic_tx_init();
 ble_npl_event_init(&command_event,command,nullptr);
 ble_npl_event_init(&g1_cleanup_event,g1_cleanup,nullptr);
 ble_npl_event_init(&watch_power_event,watch_power_prepare,nullptr);
 ble_npl_callout_init(&poll_callout,nimble_port_get_dflt_eventq(),poll,nullptr);
 portENTER_CRITICAL(&mux);watch_power.observe(micros());const bool timely=watch_power.stage!=watch::BlePower::Fault;
 watch_events_initialized=watch_npl_live=true;watch_event_gate=timely;watch_host_exited=false;watch_hs_synced=false;watch_power.controller=uint8_t(esp_bt_controller_get_status());
 if(timely){if(watch_power.stage==watch::BlePower::Init)watch_power.stage=watch::BlePower::Sync;watch_power.host_running=true;}portEXIT_CRITICAL(&mux);
 if(!timely){watch_host_release(false);return false;}
 fault_watchdog_host_expected(true);
 watch_host_release(true);
 return true;
}
void watch_power_step(){
 portENTER_CRITICAL(&mux);watch_power.observe(micros());const auto stage=watch_power.stage;portEXIT_CRITICAL(&mux);
 if(stage==watch::BlePower::Init){(void)watch_start_stack();return;}
 if(stage==watch::BlePower::Draining){
  portENTER_CRITICAL(&mux);const bool never_ran=watch_power.cleanup_active&&!watch_host_run_entered;
  const bool busy=watch_sdk_operation!=0;const bool unknown=watch_port_init_attempted&&!watch_port_initialized;
  const bool reserved=watch_host_task!=nullptr;const bool exited=watch_host_exited;portEXIT_CRITICAL(&mux);
  if(never_ran){
   if(busy)return;
   if(reserved){watch_host_release(false);return;}
   if(!exited)return;
   if(unknown){portENTER_CRITICAL(&mux);watch_power.fail(-95,micros());portEXIT_CRITICAL(&mux);return;}
   if(!watch_aux_physical_quiet())return;
   portENTER_CRITICAL(&mux);ready=false;watch_event_gate=false;
   const bool quiet=!watch_posters&&!watch_clock_readers&&!watch_admitters&&!host_depth;
   if(quiet){watch_off_owners_retired=true;
    if(watch_port_initialized)watch_power.stage=watch::BlePower::HostExit;
    else if(esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_IDLE)watch_power.stopped(micros());
    else watch_power.fail(-95,micros());
   }
   portEXIT_CRITICAL(&mux);if(quiet)fault_watchdog_host_expected(false);return;
  }
  portENTER_CRITICAL(&mux);const bool never_started=!watch_npl_live&&!watch_power.host_running&&esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_IDLE;
  const bool synchronized=watch_hs_synced||(watch_power.cleanup_active&&watch_sync_callback_seen);
  if(never_started&&!watch_port_init_attempted){watch_off_owners_retired=true;watch_host_exited=true;watch_power.stopped(micros());}portEXIT_CRITICAL(&mux);
  if(never_started){publish();return;}if(!synchronized)return;
  portENTER_CRITICAL(&mux);if(watch_power.stage==stage)watch_power.stage=watch::BlePower::Preparing;portEXIT_CRITICAL(&mux);
  if(!watch_post(&watch_power_event)){portENTER_CRITICAL(&mux);watch_power.fail(-19,micros());portEXIT_CRITICAL(&mux);}return;
 }
 if(stage==watch::BlePower::HostStop){
  portENTER_CRITICAL(&mux);const bool active=watch_clock_readers||watch_posters||watch_admitters||host_depth;portEXIT_CRITICAL(&mux);
  if(active||!ble_npl_eventq_is_empty(nimble_port_get_dflt_eventq()))return; // Retire all producers and queue-full timer senders first.
  // SDK may wait indefinitely. Only this worker blocks; the main observer
  // retains the original2s timeout and never reports a successful OFF.
  portENTER_CRITICAL(&mux);watch_sdk_operation=2;portEXIT_CRITICAL(&mux);
  const int rc=nimble_port_stop();
  portENTER_CRITICAL(&mux);watch_sdk_operation=0;watch_power.observe(micros());if(rc)watch_power.fail(rc,micros());
  else if(watch_power.stage!=watch::BlePower::Fault)watch_power.stage=watch::BlePower::HostExit;
  portEXIT_CRITICAL(&mux);return;
 }
 if(stage==watch::BlePower::HostExit){
  portENTER_CRITICAL(&mux);const bool retired=watch_host_exited&&!watch_host_task&&!watch_posters&&!watch_clock_readers&&!watch_admitters&&!watch_sdk_operation;
  if(retired)watch_power.stage=watch::BlePower::Deinit;
  portEXIT_CRITICAL(&mux);
  if(!retired)return;
  // NPL hooks must be retired while their allocator still exists. No old
  // task/event/callout may survive into the recreated host generation.
  if(watch_events_initialized){
   ble_npl_callout_stop(&poll_callout);ble_npl_callout_deinit(&poll_callout);
   ble_npl_event_deinit(&command_event);ble_npl_event_deinit(&g1_cleanup_event);ble_npl_event_deinit(&watch_power_event);
  }
  portENTER_CRITICAL(&mux);watch_events_initialized=watch_npl_live=false;watch_sdk_operation=3;portEXIT_CRITICAL(&mux);
  const int rc=nimble_port_deinit();
  portENTER_CRITICAL(&mux);watch_sdk_operation=0;watch_power.observe(micros());watch_power.controller=uint8_t(esp_bt_controller_get_status());
  if(rc||watch_power.controller)watch_power.fail(rc?rc:-5,micros());
  else {watch_port_initialized=watch_port_init_attempted=false;
   if(watch_retired_resources_locked()){watch_host_run_entered=watch_sync_callback_seen=watch_host_start_allowed=false;watch_power.stopped(micros());}
   else watch_power.fail(-16,micros());}
  portEXIT_CRITICAL(&mux);
  publish();return;
 }
}
void watch_power_worker(void*);
bool watch_power_service(){
 portENTER_CRITICAL(&mux);const auto now=micros();watch_power.observe(now);
 const bool cleanup_started=watch_power.stage==watch::BlePower::Fault&&watch_power.operating_fault&&!watch_power.cleanup_attempted&&!watch_sdk_operation&&watch_power.begin_cleanup(now);
 portEXIT_CRITICAL(&mux);
 if(cleanup_started){r2_watch_host_resume(false);r2_stop();transport_watch_ble_stop_aux();}
 portENTER_CRITICAL(&mux);
 if(watch_power_task){xTaskNotifyGive(watch_power_task);portEXIT_CRITICAL(&mux);return true;}
 if(!watch_power.transitioning()||watch_power.stage==watch::BlePower::Sync||watch_power_creating||now<watch_power_retry_us){portEXIT_CRITICAL(&mux);return true;}
 watch_power_creating=true;portEXIT_CRITICAL(&mux);
 // The new task waits for this publication before touching lifecycle state.
 // A retiring task's stack may still await idle cleanup; retry allocation
 // from the bounded main observer without restarting the original deadline.
 TaskHandle_t created=nullptr;
 const auto rc=xTaskCreatePinnedToCore(watch_power_worker,"watch_ble_power",4096,nullptr,2,&created,0);
 portENTER_CRITICAL(&mux);watch_power_creating=false;
 if(rc==pdPASS){watch_power_task=created;watch_power.worker_running=true;xTaskNotifyGive(created);}
 else {if(watch_power.worker_alloc_failures!=UINT32_MAX)++watch_power.worker_alloc_failures;watch_power_retry_us=now<=UINT64_MAX-10000?now+10000:UINT64_MAX;}
 portEXIT_CRITICAL(&mux);return rc==pdPASS;
}
bool watch_power_iteration(){
 watch_power_step();const auto available=uint32_t(uxTaskGetStackHighWaterMark(nullptr));
 portENTER_CRITICAL(&mux);if(available<watch_power.stack_min_bytes)watch_power.stack_min_bytes=available;
 // Sync needs only the existing host callback and main deadline observer.
 // Retire here too, so address setup and first admission regain this stack.
 watch_power.observe(micros());const bool busy=watch_power.transitioning()&&watch_power.stage!=watch::BlePower::Sync;
 if(!busy){watch_power_task=nullptr;watch_power.worker_running=false;}
 portEXIT_CRITICAL(&mux);return busy;
}
void watch_power_worker(void*){
 // Creation can preempt the caller before it publishes the handle.
 ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
 while(watch_power_iteration())ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(10));
 // Every notifier holds mux through notification; none can retain this handle
 // after retirement. No SDK operation or clock domain is reset here.
 vTaskDelete(nullptr);
}
