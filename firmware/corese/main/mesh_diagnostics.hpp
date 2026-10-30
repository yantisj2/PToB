// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
// Keep the wire codec out of presentation/native receiver headers. W12 and
// T096 carry separate public copies of group_auth.h; including both relies on
// incidental compiler deduplication of identical #pragma-once files.
struct r2_group_config;
namespace corese::mesh {
enum class Page:uint8_t {Mesh,Path,Quality,Reference,Wire,Shadow,Count};
enum class Result:uint8_t {Waiting,Accepted,Auth,Scope,Expired,Replay};
struct Scope {uint64_t root=0,boot=0;uint32_t epoch=0;};
struct Roles {
 uint64_t root=0,root_boot=0,hot=0,hot_boot=0,coordinator=0;
 uint64_t received_us=0,until_us=0,primary_since_us=0,hot_since_us=0;
 uint64_t created_us=0,epoch_since_us=0;
 uint64_t sequence=0,birth_utc_us=0,founder=0,term=0;
 uint64_t utc_sample_us=0,utc_received_us=0,hot_until_us=0;
 uint64_t hot_received_us=0,hot_remote_sampled_us=0;
 uint8_t hot_report_kind=0,hot_role=0;bool hot_report_known=false;
 uint32_t epoch=0,train_gen=0,accepted=0,rejected_auth=0,rejected_scope=0,rejected_age=0,replayed=0,changes=0;
 uint32_t announcements=0,statuses=0,role_reports=0,withdrawals=0;
 uint64_t last_peer=0,last_boot=0,last_sequence=0;
 uint16_t last_blockers=0;uint8_t last_kind=0,last_flags=0,last_role=0;
 uint16_t blockers=0;uint8_t wire_version=1,kind=0,flags=0,reported_role=0;
 bool hot_confirmed=false,withdrawn=false;
 uint8_t members=0;Result result=Result::Waiting;
 // Cached descriptive roles persist until replaced. These timers measure
 // unchanged identities during this observer boot, independent of GS TTL.
 bool known=false;
 char primary_name[25]="UNKNOWN",hot_name[25]="UNKNOWN",coordinator_name[25]="UNKNOWN";
};
bool live(const Roles&,uint64_t now);
// Current ACTIVE nomination is descriptive and independent of HOT self-report
// freshness or precision qualification. It grants no takeover permission.
bool hot_nomination_current(const Roles&,uint64_t now);
// Timing owner's authenticated current path, projected for display only.
struct Node {uint64_t id=0,boot=0;uint32_t train=0,epoch=0;char name[25]="UNKNOWN";};
struct Path {
 Node nodes[3]{};uint64_t sampled_us=0,until_us=0;uint32_t source_epoch=0;
 uint8_t count=0;bool known=false,authenticated=false;
};
struct Link {
 Scope scope{};uint64_t sampled_us=0,until_us=0;
 uint32_t originals=0,missed=0,duplicates=0,matched=0,rejected=0;
 uint32_t source_age_us=UINT32_MAX,source_ttl_us=UINT32_MAX;
 uint32_t upstream_rms_ns=UINT32_MAX;uint8_t upstream_samples=0;
 bool known=false,upstream_rms_known=false;
};
// The actual JOINv4 capability claim describes this exact peer, not its root.
// No satellite count, DOP, lock, antenna, PTP dataset or timescale is on GS.
struct Capabilities {
 uint64_t peer=0,boot=0,sampled_us=0,until_us=0;uint8_t flags=0;
 bool known=false;
};
// GD1/GD2 are reported snapshots, never a source/precision permission. Boot means
// the root's timing/profile incarnation, as in GS/G1, not its USB boot token.
enum ReferenceField:uint8_t {Fix=1,Pps=2,Qualified=4,Satellites=8,Ptp=16,Hdop=32};
struct Reference {
 Scope scope{};uint64_t received_us=0,until_us=0,remote_sampled_us=0;
 Scope seen_scope{};uint64_t seen_received_us=0,seen_advertiser=0;
 uint64_t sequence=0;uint32_t accepted=0,rejected_auth=0,rejected_scope=0,rejected_age=0,replayed=0;
 uint32_t utc_error_us=UINT32_MAX;
 uint16_t rmc_age_ms=UINT16_MAX,pps_age_ms=UINT16_MAX,gga_age_ms=UINT16_MAX,variance=UINT16_MAX,hdop_centi=UINT16_MAX;
 uint8_t fields=0,values=0,sats=UINT8_MAX,source_kind=0,wire_version=0;
 uint8_t priority1=128,clock_class=248,accuracy=254,priority2=128;
 char root_name[25]="UNKNOWN";Result result=Result::Waiting;bool reported=false;
};
struct Snapshot {Roles roles{};Path path{};Link link{};Capabilities capabilities{};Reference reference{};};
bool current(const Path&,uint64_t now);
bool current(const Link&,const Scope&,uint64_t now);
bool current(const Capabilities&,uint64_t peer,uint64_t boot,uint64_t now);
bool current(const Reference&,const Scope&,uint64_t now);
// Actual GM7 epoch birth is UTC microseconds. Local first observation and GM
// monotonic uptime are separate coordinates and cannot supply this value.
void epoch_start_text(char*,size_t,uint64_t birth_utc_us);
bool epoch_elapsed(const Roles&,uint64_t utc_us,uint64_t& elapsed_us);
// Format only values reported by the GM. HDOP is dimensionless, not timing
// accuracy. Console sample ages remain fixed at their original remote sample.
void reference_hdop(char*,size_t,uint16_t hdop_centi,bool known);
void reference_ptp(char*,size_t,const Reference&,bool visible);
// Caller passes the complete116-byte GS AD, the exact advertising identity,
// and selected root/boot/epoch from the timing owner. This class owns no radio,
// election, JOIN, source permission, clock model, key storage or output.
class RoleMonitor {
 Roles state_{};uint64_t remote_created_=0;
 struct ReplayFence {uint64_t root=0,boot=0,created=0,seen=0;uint32_t sequence=0;};
 ReplayFence fences_[8]{};
 public:
 bool receive(const uint8_t*,size_t,const r2_group_config&,uint64_t advertiser,
              Scope selected,uint64_t received_us,uint64_t now_us);
 Roles snapshot(uint64_t now)const;
};
class ReferenceMonitor {
 Reference state_{};
 struct ReplayFence {uint64_t root=0,boot=0,sampled=0,seen=0;uint32_t sequence=0;};
 ReplayFence fences_[8]{};
 public:
 bool receive(const uint8_t*,size_t,const r2_group_config&,uint64_t advertiser,
              Scope selected,uint64_t received_us,uint64_t now_us);
 Reference snapshot(uint64_t now)const;
};
// Receive-only implementation3 observer. Authentication, canonical advertiser,
// independent control/ROLE/GNSS replay fences and original receipt deadlines
// protect descriptive reports. This class owns no election or time authority.
class Gm7Monitor {
 Roles roles_{};Reference reference_{};
 struct Peer {uint64_t id=0,boot=0,seen=0,sequence[3]{},sampled[3]{};};
 Peer peers_[8]{};
 public:
 bool receive(const uint8_t*,size_t,const r2_group_config&,uint64_t advertiser,
              Scope selected,uint64_t received_us,uint64_t now_us);
 Roles roles(uint64_t now)const;
 Reference reference(uint64_t now)const;
};
}
