// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "rc7_provisional_path.hpp"
// Included after the production native factory-binding header. Fixture
// bindings intentionally omit opt-in macros, selecting the same zero default.
namespace v12 {
inline constexpr Rc7ProvisionalPath rc7_provisional_config(){
#ifdef PTOB_PROVISIONAL_ID
  return {PTOB_PROVISIONAL_ID,PTOB_PROVISIONAL_PUBLISHER,PTOB_PROVISIONAL_ROOT,
    PTOB_PROVISIONAL_ROOT_BOOT,PTOB_PROVISIONAL_SOURCE_EPOCH,PTOB_PROVISIONAL_OFFSET_NS,PTOB_PROVISIONAL_ALLOW_SOURCE_EPOCH_TRANSFER};
#else
  return {};
#endif
}
#ifdef PTOB_PROVISIONAL_ID
static_assert(rc7_provisional_config().id==PTOB_PROVISIONAL_ID&&
  rc7_provisional_config().publisher==PTOB_PROVISIONAL_PUBLISHER&&
  rc7_provisional_config().root==PTOB_PROVISIONAL_ROOT&&
  rc7_provisional_config().root_boot==PTOB_PROVISIONAL_ROOT_BOOT&&
  rc7_provisional_config().source_epoch==PTOB_PROVISIONAL_SOURCE_EPOCH&&
  rc7_provisional_config().offset_ns==PTOB_PROVISIONAL_OFFSET_NS&&
  rc7_provisional_config().allow_source_epoch_transfer==PTOB_PROVISIONAL_ALLOW_SOURCE_EPOCH_TRANSFER,
  "Generated provisional profile differs from compiled production config");
#else
static_assert(rc7_provisional_config().id==0&&rc7_provisional_config().offset_ns==0,
  "Unconfigured production path must remain zero");
#endif
}
