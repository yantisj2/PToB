#pragma once
#include <cstdint>
#include "board.hpp"
#include "../display.hpp"
#include "held_clock.hpp"
#include "td_health_display.hpp"
#include "../../main/ss3_td_model.hpp"
namespace sticks3 {
// Value-owned mailbox: no pointers into the timing engine cross task boundaries.
struct View {
 v12::TdSnapshot td{};TdHealthBanner td_health_banner{};
 r2_tz tz=r2_tz_unknown();uint64_t tz_until_us=0;char zone[4]="UTC";
 char header[24]="SS3",role[24]="ACQUIRE";
 bool native_dashboard=false,relay_online=false,relay_ready=false;
 bool relay_only=false;char mode_banner[24]{};
 char upstream[24]="NONE";uint32_t native_rms_ns=UINT32_MAX;uint64_t lock_seconds=0;
 char reason[24]="WAITING FOR CLOCK";HeldClock::State presentation=HeldClock::Waiting;
 bool presentation_explicit=false;uint32_t session=0;
 // Last causal statistic is historical and never substitutes live precision.
 bool metric_history=false,trace_ui=false;uint64_t snapshot_tick=0,metric_tick=0;
 uint32_t metric_root=0,metric_session=0;
 uint32_t history_ns=UINT32_MAX,history_samples=0,history_age_ms=UINT32_MAX;
 uint8_t page=0,depth=0,quarter=0,phase256=0,td_chart=0;bool phase_explicit=false,solid=false,grandmaster=false;
 bool time_valid=false,relative=false,held=false,ble_enabled=false,scan=false,fit=false;
 uint32_t utc=0,root=0,provider=0,age_s=UINT32_MAX,precision_ns=UINT32_MAX;
 uint32_t p95=UINT32_MAX,p99=UINT32_MAX,maximum=UINT32_MAX,samples=0,hold_ms=0;
 uint32_t accepted=0,duplicates=0,conflicts=0,expired=0,drops=0,scan_max_ms=0,violations=0;
 uint32_t gm_pages=0,gm_records=0,utc_physical=0,utc_maps=0,rx_rejected=0;
 uint32_t radio_error=0,capture_error=0,heap_internal=0;
 int32_t drift_mppb=0,trim_mppb=0,residual_mppb=0;int8_t rssi=0;bool rssi_valid=false;
 uint8_t percentile=95,channel=0;uint32_t hold_trials=0,span_s=0,rate_sigma=UINT32_MAX;
 bool pps_enabled=false,pps_mapped=false;uint32_t pps_edges=0,pps_drops=0,pps_age_ms=UINT32_MAX,pps_bracket_ns=UINT32_MAX;
 // No native SoftPLL state or PPS frequency fit is exported by this W12 UI ABI.
 // They are intentionally unavailable; peer feedback never creates either.
};
View make_view(const DisplayState& state);
struct RenderAudit { uint32_t clipped_pixels=0,text_overflow=0; };
void render(uint8_t frame[FrameBytes],const View& view,RenderAudit* audit=nullptr);
void render_strip(uint8_t strip[StripBytes],unsigned first_row,const View& view);
void render_splash(uint8_t frame[FrameBytes],RenderAudit* audit=nullptr);
void render_splash_strip(uint8_t strip[StripBytes],unsigned first_row);
}
