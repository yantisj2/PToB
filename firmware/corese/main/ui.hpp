// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include "mesh_diagnostics.hpp"
#include "holdover_view.hpp"
#include "../../w12/main/coarse_time_zone.hpp"

namespace corese {
// Compact monotonic intervals; never a UTC or precision claim.
void duration_text(char*,size_t,uint64_t seconds);
constexpr int Width=320,Height=240;
constexpr size_t Pixels=Width*Height,FrameBytes=Pixels*sizeof(uint16_t);
constexpr uint64_t SnapshotMaxUs=250000;
constexpr uint64_t SourcePulseUs=125000;
constexpr uint64_t RetainedHoldUs=500000;
constexpr uint64_t ReadoutGraceUs=500000;
enum class Screen:uint8_t {Launcher,Clock,Diagnostics};
enum class Face:uint8_t {Instrument,UtcRegister};
enum class Source:uint8_t {None,Grandmaster,Relay1,Relay2,Rc9,Retained};
enum class State:uint8_t {Search,Acquire,Locked,Hold,Fault};
enum class Action:uint8_t {None,OpenClock,OpenDiagnostics,Home,NextFace,PreviousDiagnostic,NextDiagnostic};
struct Model {
 struct Observed {uint64_t rms_at=0,offset_at=0;uint32_t rms_ns=UINT32_MAX;int64_t offset_ns=0;} observed;
 uint64_t sampled_us=0,utc_at_us=0,utc_us=0,until_us=0,delivery_observed_us=0;
 uint64_t precision_until_us=0,root=0,boot=0;
 uint32_t source_epoch=0,counter_hz=0,xtal_hz=0,cpu_hz=0;
 uint32_t rms_ns=UINT32_MAX,received=0,rejected=0,psram_total=0,psram_free=0;
 int64_t offset_ns=0;uint64_t offset_sampled_us=0,offset_until_us=0;
 uint32_t offset_epoch=0;uint64_t offset_root=0,offset_boot=0;
 char parent[25]="NONE",root_name[25]="NONE";
 int16_t rssi_dbm=0;uint64_t rssi_sampled_us=0;
 Source source=Source::None;State state=State::Search;
 bool utc_valid=false,precision_valid=false,offset_valid=false,rssi_valid=false;
 bool counter_healthy=false,synthetic=false;
 r2_tz tz=r2_tz_unknown();uint64_t tz_until_us=0;uint16_t tz_received=0,tz_conflicts=0;
 HoldoverView holdover{};
 struct Readouts {
  uint64_t root=0,boot=0,shadow_at=0,clock_at=0,clock_utc=0,clock_observed=0,tz_until=0;uint32_t epoch=0,generation=0;
  r2_tz tz=r2_tz_unknown();
  HoldoverView shadow{};
 } readouts;
 mesh::Snapshot mesh{};
};
// Presentation history never copies time validity or extends packet deadlines.
class DisplayMemory {
 mesh::Path path_{};mesh::Link link_{};mesh::Capabilities capabilities_{};mesh::Reference reference_{};Model::Observed observed_{};
 Model::Readouts readouts_{};
 public:void retain(Model&);
};
struct View {Screen screen=Screen::Launcher;Face face=Face::Instrument;Action pressed=Action::None;mesh::Page diagnostic=mesh::Page::Mesh;};
bool fresh(const Model&,uint64_t now);
bool time(const Model&,uint64_t now,uint64_t& utc);
// Presentation only. The last physical delivery fixes the unextended deadline.
// It carries no current source/path, precision, offset or PPS indication.
bool retained_time(Model&,uint64_t observed_us,uint64_t snapshot_us,uint64_t utc_us,uint64_t now);
bool precision(const Model&,uint64_t now);
bool offset(const Model&,uint64_t now);
bool shadow_current(const Model&,uint64_t now);
// Briefly retain numeric readouts only; never a live clock/source/lock claim.
bool readout_recent(const Model&,uint64_t now,uint64_t observed_us);
bool readout_time(const Model&,uint64_t now,uint64_t& utc);
const HoldoverView& shadow_readout(const Model&,uint64_t now,bool& visible);
const char* clock_zone(const Model&,uint64_t now,int64_t& shown);
void hold_duration(char*,size_t,const HoldoverView&,bool current);
const char* hold_error(char*,size_t,uint64_t ns,bool known);
// Modeled UTC phase only; never a physical PPS output or measured display edge.
bool source_pulse(const Model&,uint64_t now);
const char* status(const Model&,uint64_t now);
const char* source_name(const Model&,uint64_t now);
void render(uint16_t* frame,const Model&,const View&,uint64_t now);
Action hit(const View&,int x,int y);
void apply(View&,Action);
// A drag off the key cancels it. A held touch cannot activate another screen.
class Touch {
 Action armed_=Action::None;bool down_=false,cancelled_=false;
 public:
 Action update(View&,bool down,int x,int y);
};
}
