// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "relative_rate.hpp"
#include "n30_timing_probe.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
namespace {
n30::RelativeRateEstimator estimator; // Sole writer: existing BLE host observer.
portMUX_TYPE cache_mux=portMUX_INITIALIZER_UNLOCKED;
N30RelativeRateView cached;
}
void n30_relative_rate_pair(const v12::n30_probe::Pair& p,uint64_t now){
 const auto next=estimator.observe({p.root,p.boot,p.local20,p.available20,p.remote_ns,p.predicted_ns,p.source,p.causal},now);
 portENTER_CRITICAL(&cache_mux);cached=next;portEXIT_CRITICAL(&cache_mux);
}
N30RelativeRateView n30_relative_rate_snapshot(uint64_t now){
 portENTER_CRITICAL(&cache_mux);auto result=cached;portEXIT_CRITICAL(&cache_mux);
 if(now<result.sampled_us||now>=result.until_us)result.valid=false;
 return result;
}
