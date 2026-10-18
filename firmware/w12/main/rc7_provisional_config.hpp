// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "rc7_provisional_path.hpp"
// Included after the production native factory-binding header. Fixture
// bindings intentionally omit opt-in macros, selecting the same zero default.
namespace v12 {
inline Rc7ProvisionalPath rc7_provisional_config(){
#ifdef PTOB_PROVISIONAL_PATH_ID
  return {PTOB_PROVISIONAL_PATH_ID,PTOB_PROVISIONAL_PUBLISHER,PTOB_PROVISIONAL_ROOT,
    PTOB_PROVISIONAL_ROOT_BOOT,PTOB_PROVISIONAL_SOURCE_EPOCH,PTOB_PROVISIONAL_OFFSET_NS,PTOB_PROVISIONAL_ALLOW_SOURCE_EPOCH_TRANSFER};
#else
  return {};
#endif
}
}
