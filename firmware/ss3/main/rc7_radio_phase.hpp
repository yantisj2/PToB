#pragma once
#include <cstdint>
extern "C" {
#include "r2/rc7/protocol/rc_dynamic.h"
}
namespace v12 {
// RF placement only. The future TX owner supplies a verified physical TX
// capture; neither UTC/RootEnvelope.frame nor an HCI request is an input.
// counter_epoch identifies the continuous native GPTimer, separately from
// the RX synchronization incarnation. No scheduling or controller writes.
class Rc7RadioPhase {
  rc7_path path_{};
  uint64_t counter_epoch_=0,rx20_=0;
  uint32_t rx_incarnation_=0;
 public:
  static constexpr uint64_t Frame20=1250000,Slot20=312500,MaxAge20=10000000;
  struct TxCapture {
    uint64_t tick20=0,counter_epoch=0;
    bool actual=false,associated=false; // Must come from the reviewed adapter.
  };
  void clear(){path_={};counter_epoch_=rx20_=0;rx_incarnation_=0;}
  bool scope(const rc7_path& path,uint64_t counter_epoch){
    if(!rc7_path_valid(&path)||!counter_epoch){clear();return false;}
    if(counter_epoch_!=counter_epoch||!rc7_path_same(&path_,&path))clear();
    path_=path;counter_epoch_=counter_epoch;return true;
  }
  // Caller authenticates and accepts the original/correction association
  // and its source/map before passing that ORIGINAL's RX, never follow-up RX.
  bool accepted(uint64_t original20,uint32_t incarnation,uint64_t available20,bool map_source_live){
    if(!counter_epoch_||!incarnation||!original20||available20<original20||
       available20-original20>=MaxAge20||!map_source_live)return false;
    if(rx20_&&original20<=rx20_)return false;
    rx20_=original20;rx_incarnation_=incarnation;return true;
  }
  uint64_t original20()const{return rx20_;}
  uint32_t incarnation()const{return rx_incarnation_;}
  bool usable(uint64_t now20,uint64_t counter_epoch,bool map_source_live)const{
    return rx20_&&counter_epoch==counter_epoch_&&now20>=rx20_&&
      now20-rx20_<MaxAge20&&map_source_live;
  }
  bool legal(uint8_t child_slot)const{
    return counter_epoch_&&path_.depth<2&&child_slot>path_.node[path_.depth].slot&&child_slot<=3;
  }
  // A requested placement is diagnostic intent, never evidence of RF phase.
  bool request(uint8_t child_slot,uint64_t now20,uint64_t epoch,bool live,uint64_t& requested20)const{
    requested20=0;
    if(!legal(child_slot)||!usable(now20,epoch,live))return false;
    const auto delta=uint64_t(child_slot-path_.node[path_.depth].slot)*Slot20;
    if(rx20_>UINT64_MAX-delta)return false;
    auto requested=rx20_+delta;
    if(requested<now20){
      const auto elapsed=now20-requested;
      const auto frames=elapsed/Frame20+uint64_t(elapsed%Frame20!=0);
      if(frames>(UINT64_MAX-requested)/Frame20)return false;
      requested+=frames*Frame20;
    }
    requested20=requested;return true;
  }
  bool error(const TxCapture& tx,uint8_t child_slot,uint64_t now20,bool live,int64_t& error_ns)const{
    error_ns=0;
    if(!tx.actual||!tx.associated||!legal(child_slot)||
       !usable(now20,tx.counter_epoch,live)||tx.tick20<rx20_||tx.tick20>now20)return false;
    // Subtract before modulo/multiplication: safe across the full uint64_t
    // counter range, exactly50ns/native tick with no16MHz rounding adapter.
    const auto relative=(tx.tick20-rx20_)%Frame20;
    const auto phase=(uint64_t(path_.node[path_.depth].slot)*Slot20+relative)%Frame20;
    int64_t error=int64_t(child_slot)*15625000-int64_t(phase)*50;
    if(error>31250000)error-=62500000;
    if(error<-31250000)error+=62500000;
    error_ns=error;return true;
  }
};
}
