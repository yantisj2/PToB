#pragma once
#include "public_grants.hpp"
namespace v12 {
struct PublicStatus {uint32_t malformed=0,no_trusted_key=0,invalid_signature=0;
  uint32_t authenticated=0,freshness_unknown=0,missing_parent=0,timing_no_capture=0;};
class PublicReceiver {
public:
  PublicStatus status{};
  // Only reviewed public coordinator trust material may fill this slot; none
  // is provisioned in R1. Public clients never receive an issuer secret.
  struct Trust {uint8_t issuer=255;uint8_t xy[64]{};bool provisioned=false;} trust;
  RootCache parents;
  void receive(const uint8_t* bytes,size_t length);
};
}
