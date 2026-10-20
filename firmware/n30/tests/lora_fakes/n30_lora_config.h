// Synthetic host API boundary; no hardware access. MIT, Jonathan Yantis.
#pragma once
#include <cstdint>
#define PTOB_N30_FACTORY_ID UINT64_C(2)
#define PTOB_N30_LORA_TX_FACTORY UINT64_C(1)
#define PTOB_N30_GROUP_ID UINT64_C(11)
#define PTOB_N30_KEY_EPOCH 1
static constexpr uint8_t PTOB_N30_GROUP_KEY[32]={1,2,3};
