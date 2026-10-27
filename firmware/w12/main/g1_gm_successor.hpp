// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "g1_client_policy.hpp"
extern "C" {
#include "gm7_codec.h"
}
namespace v12 {
// Advisory receiver routing only. G1's admitted replay clock bounds ANNOUNCE
// age; its paired UTC/monotonic sample then bounds a UTC-less WITHDRAW.
// Neither control records nor the nominee grant timing or takeover authority.
class GmSuccessor {
  gm7_control current_{};
  uint64_t sequence_=0,sent_=0,lo_=0,mapped_at_=0;
  uint64_t hint_until_=0,action_at_=0;
  uint32_t epoch_=0;
  bool terminal_=false,fired_=false;
  static bool add(uint64_t a,uint64_t b,uint64_t& out){if(a>UINT64_MAX-b)return false;out=a+b;return true;}
  static bool utc_bounds(const v12::G1Anchor& a,uint64_t utc,uint64_t now,uint64_t& lo,uint64_t& hi){
    if(!a.valid(now)||!gm7_utc_valid(utc)||a.utc>UINT64_MAX-(now-a.at))return false;
    const auto local=a.utc+now-a.at,guard=a.guard(now);
    uint64_t center=now;
    if(utc>=local){if(!add(now,utc-local,center))return false;}
    else{if(local-utc>now)return false;center=now-(local-utc);}
    if(center<guard||!add(center,guard,hi))return false;
    lo=center-guard;return true;
  }
  bool scope(const gm7_control& c)const{
    return current_.sender&&c.sender==current_.sender&&c.boot==current_.boot&&
      c.birth_us==current_.birth_us&&c.founder==current_.founder&&c.term==current_.term&&
      c.active==current_.active&&c.active_boot==current_.active_boot&&c.timing_epoch==current_.timing_epoch&&c.train_gen==current_.train_gen;
  }
 public:
  static constexpr uint64_t RecoveryUs=3000000;
  void clear(){*this={};}
  bool bound(const v12::G1Anchor& a)const{
    return current_.sender&&a.source&&a.root==current_.active&&a.boot==current_.active_boot&&
      a.source_epoch==epoch_&&a.publication==current_.train_gen;
  }
  // Called after fresh normal admission. A renewal cannot remove a terminal
  // fence or slide an already scheduled yield. A new admitted scope can.
  void admitted(const v12::G1Anchor& a){if(!bound(a)||(fired_&&!terminal_))clear();}
  bool blocks(uint64_t id,uint64_t boot,uint32_t train)const{
    return terminal_&&id==current_.active&&boot==current_.active_boot&&train==current_.train_gen;
  }
  bool expected(uint64_t id,uint64_t boot,uint64_t now)const{
    return !fired_||now>=hint_until_||id!=current_.hot||boot==current_.hot_boot;
  }
  bool receive(const gm7_packet& p,const v12::G1Anchor& a,uint64_t received,uint64_t now){
    if(gm7_validate(&p)||now<received||now-received>=250000||
       (p.kind!=GM7_ANNOUNCE&&p.kind!=GM7_WITHDRAW))return false;
    const auto& c=p.body.control;
    // The named exact incarnation reporting the next term is a switchover
    // hint. It still needs a new G1 challenge exchange before it can be used.
    if(p.kind==GM7_ANNOUNCE&&bound(a)&&!fired_&&now<hint_until_&&current_.hot&&
       c.sender==current_.hot&&c.boot==current_.hot_boot&&c.birth_us==current_.birth_us&&
       c.founder==current_.founder&&current_.term!=UINT64_MAX&&c.term==current_.term+1){
      uint64_t lo=0,hi=0,until=0;
      if(!utc_bounds(a,c.sent_utc_us,now,lo,hi)||lo>received||
         !add(lo,c.source_valid_us,until)||now>=until)return false;
      action_at_=now;terminal_=true;return true;
    }
    if(p.kind==GM7_ANNOUNCE){
      if(c.active!=a.root||c.active_boot!=a.boot||c.timing_epoch!=a.source_epoch||c.train_gen!=a.publication||!a.valid(now))return false;
      if(current_.sender&&(!bound(a)||!scope(c)))return false;
      if(terminal_||c.sequence<=sequence_||(sequence_&&c.sent_mono_us<sent_))return false;
      uint64_t lo=0,hi=0,until=0,source=0,hint=0,action=0;
      if(!utc_bounds(a,c.sent_utc_us,now,lo,hi)||lo>received||
         !add(lo,c.max_age_us,until)||now>=until||
         !add(lo,c.source_valid_us,source)||now>=source||!add(until,RecoveryUs,hint))return false;
      if((p.flags&GM7_YIELD)&&!add(hi,750000,action))return false;
      // An immutable YIELD cannot be moved by a later copy or ordinary ANN.
      if(action_at_&&(c.hot!=current_.hot||c.hot_boot!=current_.hot_boot))return false;
      current_=c;epoch_=a.source_epoch;sequence_=c.sequence;sent_=c.sent_mono_us;
      lo_=lo;mapped_at_=now;hint_until_=hint;
      if(action&&!action_at_)action_at_=action;
      return true;
    }
    // WITHDRAW cannot create a nomination or replace the retained scope.
    if(!bound(a)||!scope(c)||terminal_||c.sequence<=sequence_||c.sent_mono_us<sent_||
       (c.hot&&(c.hot!=current_.hot||c.hot_boot!=current_.hot_boot))||
       now<mapped_at_||now>=hint_until_)return false;
    const auto delta=c.sent_mono_us-current_.sent_mono_us;
    if(delta>10000000)return false;
    const auto drift=(delta+499)/500+100; // Existing coarse 2000ppm allowance.
    uint64_t lo=0,until=0;
    if(!add(lo_,delta,lo)||lo<drift)return false;
    lo-=drift;
    if(lo>received||!add(lo,c.max_age_us,until)||now>=until)return false;
    sequence_=c.sequence;sent_=c.sent_mono_us;terminal_=true;action_at_=now;return true;
  }
  bool due(uint64_t now)const{return !fired_&&action_at_&&now>=action_at_;}
  uint64_t lost(const v12::G1Anchor& a,uint64_t now){
    if(!bound(a)||fired_)return 0;
    if(action_at_&&now>=action_at_)terminal_=true;
    fired_=true;
    return now>=mapped_at_&&now<hint_until_?current_.hot:0;
  }
};

// One short passive management window per UTC quarter frame, between actual
// 62.5ms PA receptions. No fresh capture/model means no additional scanning.
class GmControlScan {
  uint64_t start_=0,until_=0,capture_=0,frame_=0,last_=0;
 public:
  void clear(){*this={};}
  bool allows(uint64_t now)const{return start_&&now>=start_&&now<until_;}
  bool observed(uint64_t at)const{return start_&&at>=start_&&at<until_;}
  bool poll(uint64_t now,bool eligible,uint64_t capture,uint64_t age,uint64_t utc){
    if(now<last_||!eligible){clear();last_=now;return false;}last_=now;
    if(now<until_)return allows(now);
    if(!capture||capture==capture_||age>=25000||!utc||now>UINT64_MAX-250000||utc>UINT64_MAX-250000)return false;
    const auto phase=utc%250000;
    constexpr uint64_t Begin=32000,End=50000;
    if(phase>=Begin||age+Begin-phase<8000||age+End-phase>56000||utc-phase+End<=frame_)return false;
    capture_=capture;frame_=utc-phase+End;start_=now+Begin-phase;until_=now+End-phase;return false;
  }
};
}
