// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace corese {
// Diagnostic scheduling only. Current admitted UTC places bounded passive
// windows between physical PA receptions; it cannot grant/renew clock authority.
class StatusScan {
 uint64_t last_=0,start_=0,until_=0,used_capture_=0,last_plan_utc_=0;
 uint32_t windows_=0,primary_=0,status_=0,guard_refused_=0;
 uint64_t pa_phase_=0;bool active_=false,opened_=false,primary_plan_=false;
 public:
 static constexpr uint64_t CaptureAgeUs=25000,StartGuardUs=8000,LastEndUs=56000;
 static constexpr uint64_t FrameUs=250000,PrimaryStartUs=41000,PrimaryEndUs=55000;
 static constexpr uint64_t StatusStartUs=85000,StatusEndUs=113000,MaxWindowUs=30000;
 static_assert(PrimaryEndUs-PrimaryStartUs<=MaxWindowUs&&StatusEndUs-StatusStartUs<=MaxWindowUs);
 bool poll(uint64_t now,bool eligible,uint64_t capture,uint64_t age,uint64_t utc){
  if(now<last_){active_=false;return false;}last_=now;
  if(!eligible){active_=false;return false;}
  if(active_&&now<until_){
   if(now<start_)return false;
   if(!opened_){opened_=true;if(windows_!=UINT32_MAX)++windows_;auto& count=primary_plan_?primary_:status_;if(count!=UINT32_MAX)++count;}
   return true;
  }
  active_=false;
  if(!capture||capture==used_capture_||age>=CaptureAgeUs||!utc||now>UINT64_MAX-FrameUs||utc>UINT64_MAX-FrameUs)return false;
  const auto phase=utc%FrameUs;
  pa_phase_=(phase+FrameUs-age%FrameUs)%FrameUs;
  uint64_t begin=0,end=0;
  if(phase<PrimaryStartUs){begin=PrimaryStartUs;end=PrimaryEndUs;}
  else if(phase>=62500&&phase<StatusStartUs){begin=StatusStartUs;end=StatusEndUs;}
  else return false;
  const auto wait=begin-phase,finish=end-phase,plan=utc-phase+end;
  // Preserve an actual guard around the associated physical62.5ms PA.
  // A UTC model whose phase cannot satisfy that guard grants no scan window.
  if(age+wait<StartGuardUs||age+finish>LastEndUs){if(guard_refused_!=UINT32_MAX)++guard_refused_;return false;}
  if(plan<=last_plan_utc_)return false;
  used_capture_=capture;last_plan_utc_=plan;
  start_=now+wait;until_=now+finish;active_=true;opened_=false;primary_plan_=begin==PrimaryStartUs;
  return false;
 }
 void reset(){last_=start_=until_=used_capture_=last_plan_utc_=0;active_=opened_=false;}
 bool allows(uint64_t now)const{return active_&&now>=start_&&now<until_;}
 bool observed(uint64_t at)const{return start_&&at>=start_&&at<until_;}
 uint64_t deadline()const{return until_;}
 uint32_t windows()const{return windows_;}
 uint32_t primary_windows()const{return primary_;}
 uint32_t status_windows()const{return status_;}
 uint32_t guard_refused()const{return guard_refused_;}
 uint64_t pa_phase()const{return pa_phase_;}
};
}
