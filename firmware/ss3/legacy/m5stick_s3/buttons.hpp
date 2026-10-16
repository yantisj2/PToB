#pragma once
#include <cstdint>
namespace sticks3 {
// T096 ui_button_update: 40 ms debounce, action on the stable DOWN edge.
// Treat any overlapping presses as one gesture until both keys are released.
// A key held at startup is suppressed until that complete release too.
class GoButtons {
public:
 static constexpr uint64_t DebounceTicks=800000; // W12 20 MHz, 40 ms.
 explicit GoButtons(bool a=false,bool b=false,uint64_t now=0):
   keys_{{a,a,now},{b,b,now}},latched_(a||b) {}
 bool update(bool a,bool b,uint64_t now) {
  const bool inputs[]{a,b};bool edge=false;
  for(unsigned i=0;i<2;++i) {
   auto& k=keys_[i];
   if(k.raw!=inputs[i]){k.raw=inputs[i];k.changed=now;}
   if(k.raw!=k.stable && now>=k.changed && now-k.changed>=DebounceTicks) {
    k.stable=k.raw;edge|=k.stable;
   }
  }
  const bool advance=edge && !latched_;
  if(advance)latched_=true;
  if(!keys_[0].raw && !keys_[1].raw && !keys_[0].stable && !keys_[1].stable)latched_=false;
  return advance;
 }
private:
 struct Key { bool raw,stable;uint64_t changed; } keys_[2];
 bool latched_=false;
};
}
