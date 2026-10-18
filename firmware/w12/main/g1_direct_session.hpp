// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "g1_client_policy.hpp"
namespace v12 {
// A direct-root challenge grants a finite session, independently of the
// short signed source permission and the coarse replay clock. Only a newly
// matched physical correction can maintain that clock within this session.
class G1DirectSession {
  uint64_t root_=0,boot_=0,renew_at_=0,until_=0,last_original20_=0;
  uint32_t source_=0,train_=0,uncertainty_=0;
public:
  static constexpr uint64_t RenewUs=240000000,LeaseUs=360000000,JitterUs=15000000;
  void clear(){*this={};}
  bool matches(const G1Anchor& a)const {
    return root_&&a.source&&a.root==root_&&a.boot==boot_&&
      a.source_epoch==source_&&a.publication==train_&&a.context_until==until_;
  }
  // Called only after a fresh direct-root JOIN has passed MAC, nonce, causal
  // time and sender/root checks. Jitter advances renewal, never the expiry.
  bool begin(G1Anchor& a,uint64_t now,uint32_t random){
    clear();
    if(!a.root||!a.boot||!a.source_epoch||!a.publication||!a.live(now)||
       now>UINT64_MAX-LeaseUs)return false;
    root_=a.root;boot_=a.boot;source_=a.source_epoch;train_=a.publication;
    uncertainty_=a.uncertainty;until_=now+LeaseUs;
    renew_at_=now+RenewUs-random%(JitterUs+1);
    a.context_until=until_;return true;
  }
  bool due(const G1Anchor& a,uint64_t now)const {
    if(!matches(a))return a.refresh(now);
    // Absent corrected progress retains the original coarse refresh policy.
    // Neither an authenticated packet nor a held prediction is progress.
    return now>=renew_at_||a.rebootstrap(now)||
      (now>=a.at&&now-a.at>=1500000&&a.refresh(now));
  }
  bool reanchor(G1Anchor& a,uint64_t original20,uint64_t available20,
                uint64_t now,uint64_t utc,uint64_t permission_until,
                uint32_t enclosure_ns){
    if(!matches(a)||!a.valid(now)||now>=until_||!original20||
       original20<=last_original20_||available20<original20||
       available20-original20>5000000||enclosure_ns==UINT32_MAX||
       permission_until!=a.until||permission_until<=now||
       permission_until-now>1500000||a.utc>UINT64_MAX-(now-a.at))return false;
    const auto expected=a.utc+now-a.at;
    const auto distance=utc>=expected?utc-expected:expected-utc;
    // The replacement must lie inside the still-live prior causal interval.
    // Keep the original JOIN uncertainty plus physical enclosure, rather than
    // substituting fit RMS or treating the estimate as qualified UTC.
    const uint64_t uncertainty=uint64_t(uncertainty_)+(uint64_t(enclosure_ns)+999)/1000;
    if(distance>a.guard(now)||uncertainty>=249800)return false;
    a.utc=utc;a.at=now;a.uncertainty=uint32_t(uncertainty);
    last_original20_=original20;return true;
  }
};
}
