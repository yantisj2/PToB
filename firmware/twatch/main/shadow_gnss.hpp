// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "shadow_model.hpp"
namespace v12 {struct ConsoleFrame;}
namespace watch {struct Model;}
namespace watchshadow {
void init(uint64_t boot,uint64_t factory,bool (*read20)(uint64_t*));
void visible(bool on); // UI owner: bounded BLDO1 transition and request mailbox.
void selected(const watch::Model&,uint64_t now); // Main owner: actual selected clock and original expiry.
void poll(uint64_t now); // Main owner: UART, capture and statistical processing.
void stop(); // Latches maintenance OFF; cannot be re-enabled in this boot.
bool released();
struct ControlProof {bool requested=false,powered=false,transition=false,owner=false,uart=false,capture=false,off=false,cleanup_fault=false;};
ControlProof control(); // Actual owner/rail state, never the displayed snapshot.
watch::ShadowView snapshot();
void status(v12::ConsoleFrame&); // Compact fields appended to TWATCH_UI.
void detail(v12::ConsoleFrame&);
}
