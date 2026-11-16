// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Watch-only supervision: diagnostics and flash journal, NEVER MCU restart.
#include "fault_watchdog.hpp"
#include "radio_watchdog.hpp"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
namespace {
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
v12::FaultWatchdogStatus view;
watch::RadioJournalStatus journal;
watch::RadioFaultRecord committed[8],first_fault[2];
watch::RadioFaultRecord queue[16];unsigned head=0,count=0;
uint64_t boot_id=0;bool ready=false,paused=false,busy=false;uint32_t sequence=0;
TaskHandle_t worker_task=nullptr;
StaticTask_t storage;StackType_t stack[3072];
uint64_t now(){return uint64_t(esp_timer_get_time());}
void worker(void*){
 bool loaded=false;uint32_t previous_stall=0;
 for(;;){
  portENTER_CRITICAL(&mux);const bool can_load=ready&&!paused;if(can_load&&!loaded)busy=true;portEXIT_CRITICAL(&mux);
  if(can_load&&!loaded){
   nvs_handle_t h;const auto rc=nvs_open("watchfault",NVS_READWRITE,&h);
   if(rc==ESP_OK){
    for(unsigned i=0;i<8;++i){char key[16];std::snprintf(key,sizeof key,"e%u",i);watch::RadioFaultRecord r;size_t size=sizeof r;
     if(nvs_get_blob(h,key,&r,&size)==ESP_OK&&size==sizeof r&&r.version==1&&r.radio<2){
      portENTER_CRITICAL(&mux);committed[i]=r;if(r.sequence>sequence)sequence=r.sequence;
      if(r.event!=4&&r.sequence>journal.latest[r.radio].sequence)journal.latest[r.radio]=r;
      portEXIT_CRITICAL(&mux);
     }
    }
    for(unsigned i=0;i<2;++i){char key[16];std::snprintf(key,sizeof key,"first%u",i);watch::RadioFaultRecord r;size_t size=sizeof r;
     if(nvs_get_blob(h,key,&r,&size)==ESP_OK&&size==sizeof r&&r.version==1&&r.radio==i&&(r.event==1||r.event==5)){
      portENTER_CRITICAL(&mux);first_fault[i]=r;portEXIT_CRITICAL(&mux);
     }
    }nvs_close(h);
   }
   portENTER_CRITICAL(&mux);journal.storage_error=rc;busy=false;portEXIT_CRITICAL(&mux);loaded=rc==ESP_OK;
  }
  watch::RadioFaultRecord r;bool have=false;
  portENTER_CRITICAL(&mux);
  view.sampled_us=now();
  // Stalled owners remain observable. A stalled worker cannot safely be
  // restarted by another task; never reboot or bypass ownership to recover it.
  if(view.sampled_us-view.main_at_us>=5000000)view.mask|=v12::FaultMainStalled;
  if(view.host_expected&&view.sampled_us-view.host_at_us>=5000000)view.mask|=v12::FaultHostStalled;
  const uint32_t stalled=view.mask&(v12::FaultMainStalled|v12::FaultHostStalled);
  if(!paused&&stalled&&stalled!=previous_stall){
   watch::RadioFaultRecord failure;failure.boot=boot_id;failure.at_us=view.sampled_us;
   failure.mask=stalled;failure.event=5;failure.radio=0;
   if(count<16){queue[(head+count)%16]=failure;++count;}else ++journal.dropped;
  }
  previous_stall=stalled;
  if(!paused&&loaded&&count){r=queue[head];head=(head+1)%16;--count;have=true;busy=true;
   r.sequence=sequence==UINT32_MAX?UINT32_MAX:++sequence;if(r.event!=4)journal.latest[r.radio]=r;}
  portEXIT_CRITICAL(&mux);
  if(have){
   const bool save_first=(r.event==1||r.event==5)&&!first_fault[r.radio].sequence;
   nvs_handle_t h;auto rc=nvs_open("watchfault",NVS_READWRITE,&h);
   if(rc==ESP_OK){char key[16];std::snprintf(key,sizeof key,"e%u",unsigned(r.sequence%8));rc=nvs_set_blob(h,key,&r,sizeof r);
    if(rc==ESP_OK&&save_first){std::snprintf(key,sizeof key,"first%u",unsigned(r.radio));rc=nvs_set_blob(h,key,&r,sizeof r);}
    if(rc==ESP_OK)rc=nvs_commit(h);
    nvs_close(h);
   }
   portENTER_CRITICAL(&mux);journal.storage_error=rc;if(rc==ESP_OK){++journal.saved;committed[r.sequence%8]=r;if(save_first)first_fault[r.radio]=r;}else ++journal.dropped;busy=false;portEXIT_CRITICAL(&mux);
  }
  if(!have)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(1000));
 }
}
}
namespace watch {
void radio_journal_ready(){portENTER_CRITICAL(&mux);ready=true;portEXIT_CRITICAL(&mux);if(worker_task)xTaskNotifyGive(worker_task);}
void radio_journal_stop(){portENTER_CRITICAL(&mux);paused=true;journal.dropped+=count;count=0;portEXIT_CRITICAL(&mux);}
bool radio_journal_idle(){portENTER_CRITICAL(&mux);const bool idle=!busy;portEXIT_CRITICAL(&mux);return idle;}
void radio_journal_record(RadioFaultRecord r){
 uint32_t tag=2166136261u;for(const char* p=V12_BUILD_ID;*p;++p)tag=(tag^uint8_t(*p))*16777619u;
 r.build_tag=tag;
 portENTER_CRITICAL(&mux);r.boot=boot_id;
 if(!paused&&r.radio<2&&count<16){queue[(head+count)%16]=r;++count;}else if(!paused)++journal.dropped;
 portEXIT_CRITICAL(&mux);if(worker_task)xTaskNotifyGive(worker_task);
}
RadioJournalStatus radio_journal_status(unsigned slot){
 portENTER_CRITICAL(&mux);auto r=journal;r.slot=slot;
 if(slot){r.latest[0]={};r.latest[1]={};
  if(slot<=8&&committed[slot-1].sequence){const auto& saved=committed[slot-1];r.latest[saved.radio]=saved;r.slot_valid=true;}
  if(slot>=9&&slot<=10&&first_fault[slot-9].sequence){const auto& saved=first_fault[slot-9];r.latest[saved.radio]=saved;r.slot_valid=true;}
 }
 portEXIT_CRITICAL(&mux);return r;
}
}
namespace v12 {
bool fault_watchdog_start(const uint8_t* factory,uint64_t boot){
 if(!factory||!boot||view.enabled)return false;
 boot_id=boot;view.main_at_us=now();
 worker_task=xTaskCreateStaticPinnedToCore(worker,"watch_fault",sizeof stack,nullptr,1,stack,&storage,1);view.enabled=worker_task!=nullptr;return view.enabled;
}
void fault_watchdog_fatal(){portENTER_CRITICAL(&mux);view.mask|=FaultInit;portEXIT_CRITICAL(&mux);}
void fault_watchdog_main_progress(uint32_t mask,uint8_t,int32_t,uint64_t parent,bool relay,bool applied){
 portENTER_CRITICAL(&mux);view.main_at_us=now();view.mask=mask;
 if(mask&&!view.since_us){view.since_us=view.main_at_us;view.first_mask=mask;}if(!mask)view.since_us=0;
 view.elapsed_us=view.since_us?view.main_at_us-view.since_us:0;view.fault_since_us=view.since_us;view.fault_elapsed_us=view.elapsed_us;
 if(applied){view.parent=parent;view.parent_relay=relay;}portEXIT_CRITICAL(&mux);
}
void fault_watchdog_sync_progress(bool,uint64_t valid){portENTER_CRITICAL(&mux);view.sync_until_us=valid;view.sync_expected=false;portEXIT_CRITICAL(&mux);}
void fault_watchdog_host_progress(){portENTER_CRITICAL(&mux);view.host_at_us=now();view.host_expected=true;portEXIT_CRITICAL(&mux);}
void fault_watchdog_host_expected(){fault_watchdog_host_progress();}
void fault_watchdog_host_expected(bool expected){portENTER_CRITICAL(&mux);view.host_expected=expected;if(expected)view.host_at_us=now();portEXIT_CRITICAL(&mux);}
FaultWatchdogStatus fault_watchdog_status(){portENTER_CRITICAL(&mux);auto r=view;portEXIT_CRITICAL(&mux);return r;}
bool fault_watchdog_test(){return false;} // Reset injection is not a watch feature.
void fault_watchdog_parent_restored(){}
}
