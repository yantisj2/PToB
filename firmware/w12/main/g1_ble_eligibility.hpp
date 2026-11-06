// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "native_r2.hpp"
namespace ptob_ble {
constexpr uint64_t SnapshotMaxUs=250000;
enum BleLockRefusal:uint32_t {
 BleReady=0,BleDisabled=1,BleFault=2,BleMode=4,BleNotFollowing=8,BleContext=16,
 BleSnapshot=32,BleCoarseExpiry=64,BleCorrectedExpiry=128,BleReplayExpiry=256,
 BleQuality=512,BleForecast=1024,BleRetained=2048
};
inline uint32_t ble_lock_refusal(const v12::R2Status& s,uint64_t now){
 uint32_t mask=0;
 if(!s.requested)mask|=BleDisabled;
 if(s.fault||s.rx_capture.fault)mask|=BleFault;
 if(s.rc7_upstream_mode_pending||s.rc_parent.pending()||s.rc_parent.state==4)mask|=BleMode;
 if(!s.following||!s.clock_valid)mask|=BleNotFollowing;
 if(s.g1_retained_estimate)mask|=BleRetained;
 if(!s.root_identity||!s.root_boot||!s.source_epoch||!s.publication)mask|=BleContext;
 if(now<s.follower_snapshot_us||now-s.follower_snapshot_us>=SnapshotMaxUs)mask|=BleSnapshot;
 if(s.service_until_us<=now)mask|=BleCoarseExpiry;
 if(s.g1_corrected_until_us<=now)mask|=BleCorrectedExpiry;
 const auto until=s.g1_anchor_deadline_us;
 if(until<=now)mask|=BleReplayExpiry;
 if(mask&(BleDisabled|BleFault|BleMode|BleContext|BleSnapshot|BleRetained|BleReplayExpiry))return mask;
 const auto tick=s.g1_relative_quality.evaluated20;
 if(!s.g1_relative_quality.locked(tick,s.g1_clock_model))mask|=BleQuality;
 else if(s.g1_relative_quality.hold_ms(tick,s.g1_clock_model,until-now)<1000)mask|=BleForecast;
 return mask;
}
inline const char* ble_lock_reason(uint32_t refusal){
 if(!refusal)return "READY";
 if(refusal&BleDisabled)return "DISABLED";
 if(refusal&BleFault)return "FAULT";
 if(refusal&BleMode)return "MODE_DRAIN";
 if(refusal&BleSnapshot)return "STALE_SNAPSHOT";
 if(refusal&BleRetained)return "RETAINED_HOLD";
 if(refusal&BleCoarseExpiry)return "COARSE_PERMISSION";
 if(refusal&BleCorrectedExpiry)return "CORRECTED_PERMISSION";
 if(refusal&BleReplayExpiry)return "REPLAY_CONTEXT";
 if(refusal&BleContext)return "NO_CONTEXT";
 if(refusal&BleNotFollowing)return "NO_CURRENT_FOLLOW";
 if(refusal&BleQuality)return "RAW_QUALITY";
 if(refusal&BleForecast)return "QUALITY_FORECAST";
 return "UNKNOWN";
}
}
