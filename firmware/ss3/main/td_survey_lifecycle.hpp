// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace v12 {
struct TdSurveyLifecycle {
  static constexpr uint64_t DrainUs=2000000;
  uint64_t closing_us=0;
  uint32_t generation=0;
  bool finishing=false;
  bool begin_close(uint64_t now,bool due){
    if(finishing||!due)return false;
    finishing=true;closing_us=now;return true;
  }
  bool expired(uint64_t now)const{return finishing&&now>=closing_us&&now-closing_us>=DrainUs;}
  void advance(){finishing=false;closing_us=0;if(generation!=UINT32_MAX)++generation;}
};
}
