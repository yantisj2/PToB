// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "mesh_diagnostics.hpp"
namespace v12::n30_probe {struct Pair;}
namespace corese::owner {
bool init();
void source_lost(uint64_t now);
void advertisement(const uint8_t*,unsigned,uint8_t,const uint8_t address_le[6],mesh::Scope,const r2_group_config&,uint64_t at,uint64_t now);
void capabilities(uint64_t peer,uint64_t boot,uint8_t flags,uint64_t at,uint64_t until);
void residual(const v12::n30_probe::Pair&,uint64_t now);
void scan_state(bool allowed,uint64_t deadline,uint32_t windows,uint64_t now);
struct Observation {mesh::Roles roles{};mesh::Reference reference{};mesh::Capabilities cap{};mesh::Scope offset_scope{};int64_t offset=0;uint64_t offset_at=0,scan_deadline=0;uint32_t scan_windows=0;bool offset_valid=false,scan_allowed=false;};
bool snapshot(Observation&,uint64_t now);
}
