// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
extern "C" {
#include "../r2/r2-g1/group_auth.h"
}
namespace v12::rc9 {
constexpr size_t TimeBytes=190,ControlBytes=212,CapabilityBytes=144;
constexpr uint64_t HoldUs=12000000,ExchangeUs=2000000;
constexpr uint32_t LeaseUs=360000000,RenewUs=300000000;
enum class Type:uint8_t {Capability=1,Request=2,Response=3,Time=4};
struct Control {
 Type type=Type::Capability;
 uint64_t sender=0,sender_boot=0,target=0,target_boot=0,challenge=0,nonce=0,next_challenge=0,server_us=0;
 uint32_t sequence=0,lease_us=0,publication_floor=0;
 bool ready=false;
};
// Functional UTC snapshot, NOT a fine-clock correction. edge/until/sample are
// publisher monotonic microseconds; event/created/expires are original RC8 ns.
// Missing companion boots/stream and UNKNOWN assurance/authority stay unknown.
struct Time {
 uint64_t sender=0,sender_boot=0,sampled_us=0,edge_us=0,until_us=0,event_ns=0;
 uint64_t root=0,root_boot=0,emitter=0,emitter_boot=0,stream=0,created_ns=0,expires_ns=0;
 uint32_t publication=0,source_epoch=0,generation=0,corrected_sequence=0,link_rms_ns=UINT32_MAX;
 uint8_t context[16]{},assurance=0,authority=0,kind=0,link_samples=0;
 bool valid=false;
};
bool valid_control(const Control&);
bool valid_time(const Time&);
bool encode_control(uint8_t*,const Control&,const r2_group_config&);
bool decode_control(Control&,const uint8_t*,size_t,const r2_group_config&);
bool encode_capability(uint8_t*,const Control&,const r2_group_config&);
bool decode_capability(Control&,const uint8_t*,size_t,const r2_group_config&);
bool encode_time(uint8_t*,const Time&,const r2_group_config&);
bool decode_time(Time&,const uint8_t*,size_t,const r2_group_config&);
}
