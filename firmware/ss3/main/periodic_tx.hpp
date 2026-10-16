#pragma once
#include <cstdint>
#include "periodic_tx_selected_model.hpp"
#include "rc7_tx_estimate.hpp"
#if defined(V12_R2_RC7_TX_ESTIMATE_LAB) && (!defined(V12_R2_RX_SHADOW) || !defined(V12_R2_ENABLED) || V12_R2_CONFIG != 13 || V12_R2_G1_LISTENING_PERCENT != 100)
#error Untested RC7 TX estimates require the BLE100 G1 lab profile and direct RX clock reference
#endif
namespace v12::periodic_tx {
// Controller observations for the Phase 2 feasibility probe. These rows do
// NOT contain a proved physical TX timestamp or actual PA channel. The narrow
// selected-payload/controller-event binding is diagnostic only.
// In particular, before20/after20 are callback brackets, never RC7 tx_ns.
constexpr unsigned Capacity=4, ContextBytes=84, CsWords=45, TxWords=63;
struct Record {
  uint64_t before20=0,after20=0;
  uint32_t sequence=0,epoch=0,kind=0;
  uint8_t activity=255,status=0,controller_activity=255;
  uint8_t expected_slot=255;
  selected_tx::Selection selected{};
};
static_assert(sizeof(Record)<=256,"Selected TX diagnostic ring exceeds its reviewed allocation");
struct Stats {
  uint32_t calls=0,recorded=0,dropped=0,fault=0,epoch=0,max_cycles=0;
  bool installed=false,armed=false;
};
// A future timestamp provider needs its own exact ABI/physical review. The
// diagnostic shadow cannot promote scheduled CS fields or ISR brackets.
constexpr bool actual_capture_available(){return false;}
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
constexpr bool estimated_capture_available(){return true;}
#else
constexpr bool estimated_capture_available(){return false;}
#endif
struct EstimateRecord {
  Record observed{};
  uint64_t tick20=0;
  tx_estimate::ClockAnchor anchor{}; // Worker diagnostic only; never an RF capture.
  uint32_t uncertainty_ns=tx_estimate::UnknownUncertainty;
  uint8_t channel=255;
  uint8_t stage=0; // 1 verify,2 bracket,3 programmed mapping,4 channel,5 estimate.
  tx_estimate::Provenance provenance=tx_estimate::Provenance::None;
  bool estimated=false;
};
// Worker-only snapshot. Publication zero means no coherent current bank;
// this reports queue ownership, never TX or source authority.
struct EstimateQueue {uint32_t prepared=0,current=0;};
#if defined(V12_R2_TX_SHADOW) || defined(V12_R2_RC7_TX_ESTIMATE_LAB)
bool start(const uint8_t* expected,unsigned size);bool stop();bool pop(Record&);Stats status();
bool payload_verified(const Record&);
#else
inline bool start(const uint8_t*,unsigned){return true;}inline bool stop(){return true;}
inline bool pop(Record&){return false;}inline Stats status(){return {};}
inline bool payload_verified(const Record&){return false;}
#endif
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
bool start_estimated(const uint8_t* initial,unsigned size);
bool prepare_estimated_payload(const uint8_t*,unsigned);
bool pop_estimated(EstimateRecord&);
bool estimate_owner_active();
EstimateQueue estimate_queue();
#else
inline bool start_estimated(const uint8_t*,unsigned){return false;}
inline bool prepare_estimated_payload(const uint8_t*,unsigned){return false;}
inline bool pop_estimated(EstimateRecord&){return false;}
inline bool estimate_owner_active(){return false;}
inline EstimateQueue estimate_queue(){return {};}
#endif
}
