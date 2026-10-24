// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
namespace corese {
enum class SleepStage:unsigned {Awake,Requested,Draining,Ready,Failed,Sleeping};
// CPU1 owns the button/PMIC; CPU0 owns bounded radio/counter shutdown.
class SleepGate {
 std::atomic<SleepStage> stage_{SleepStage::Awake};
 bool move(SleepStage from,SleepStage to){return stage_.compare_exchange_strong(from,to);}
 public:
 bool request(){return move(SleepStage::Awake,SleepStage::Requested);}
 bool take_request(){return move(SleepStage::Requested,SleepStage::Draining);}
 void drained(bool ready,bool fault){if(fault)move(SleepStage::Draining,SleepStage::Failed);else if(ready)move(SleepStage::Draining,SleepStage::Ready);}
 bool take_shutdown(){return move(SleepStage::Ready,SleepStage::Sleeping);}
 SleepStage stage()const{return stage_.load();}
};
}
