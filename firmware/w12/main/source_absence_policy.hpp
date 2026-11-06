// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace ptob {
constexpr uint64_t NoTimeRestartUs=720000000;
// Absence can cancel only the destructive no-time restart. It never grants
// current time, renews a lease, or suppresses driver/worker fault supervision.
class SourceAbsencePolicy {
 bool learned_=false,healthy_=false,expected_=false;
 public:
 struct View {bool learned,healthy_rx,expected;};
 bool observe(bool requested,bool learned,bool healthy_rx){
  learned_=learned_||learned;healthy_=healthy_rx;
  expected_=requested&&!learned_&&!healthy_rx;return expected_;
 }
 View view()const{return {learned_,healthy_,expected_};}
};
}
