#pragma once
#include "g1_clock_prediction.hpp"
namespace v12 {
// Explicit donor experiment: apply to a COPY of the delivered clock only.
// Never change raw oscillator points, predictions, signed source offsets,
// permission, qualified precision, source candidacy or relay readiness.
inline G1ClockPrediction g1_output_correction(G1ClockPrediction raw,int32_t correction,
    uint64_t root,uint64_t boot,uint32_t source,unsigned percent,bool live,
    bool reference_valid,unsigned phy,unsigned allowance,
    uint64_t expected_root,uint64_t expected_boot,uint32_t expected_source){
  raw.output_correction_ns=0;
  if(correction==3056&&raw.tick&&live&&percent==100&&reference_valid&&phy==0&&allowance==4&&
      root&&root==expected_root&&boot&&boot==expected_boot&&source&&source==expected_source)
    raw.output_correction_ns=correction;
  return raw;
}
}
