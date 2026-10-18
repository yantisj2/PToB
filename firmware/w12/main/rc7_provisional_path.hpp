// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "g1_clock_prediction.hpp"
namespace v12 {
// Operator-selected aggregate-path offset. It does not identify RX or TX bias,
// alter the fitted clock or supply an RF/UTC uncertainty bound. Default zero.
struct Rc7ProvisionalPath {
  uint64_t id=0,publisher=0,root=0,root_boot=0;
  uint32_t source_epoch=0; // Training epoch remains provenance even on explicit transfer.
  int32_t offset_ns=0;bool allow_source_epoch_transfer=false;
  bool enabled()const{return id&&publisher&&root&&root_boot&&source_epoch&&
    offset_ns>=-100000&&offset_ns<=100000&&offset_ns!=0;}
  int32_t select(uint64_t self,unsigned depth,uint64_t origin_root,
                 uint64_t origin_boot,uint32_t epoch,bool reference_valid)const{
    return enabled()&&self==publisher&&depth==0&&origin_root==root&&
      origin_boot==root_boot&&epoch&&(epoch==source_epoch||allow_source_epoch_transfer)&&reference_valid?offset_ns:0;
  }
  static bool apply(int64_t raw,int32_t offset,int64_t& delivered){
    if(raw<=0||(offset>0&&raw>INT64_MAX-offset)||(offset<0&&raw<=-int64_t(offset)))return false;
    delivered=raw+offset;return true;
  }
};
}
