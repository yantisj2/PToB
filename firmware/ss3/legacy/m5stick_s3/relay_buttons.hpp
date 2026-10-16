#pragma once
#include "../button_input.hpp"
#include <cstdint>
namespace sticks3 {
// Main GPIO11: tap on release, one mode toggle at1.5s, rearm after release.
// Side GPIO12 keeps page navigation. Startup-held/overlap gestures are suppressed.
class RelayButtons {
  struct Key {bool raw=false,stable=false;uint64_t changed=0;};
  Key a{},b{};uint64_t down=0;bool held=false,suppressed=false;
public:
  RelayButtons(bool main,bool side,uint64_t now):a{main,main,now},b{side,side,now},suppressed(main||side){}
  ButtonInput::Action update(bool main,bool side,uint64_t now){
    bool release=false,side_down=false;
    Key* keys[]{&a,&b};const bool input[]{main,side};
    for(unsigned i=0;i<2;++i){auto& k=*keys[i];
      if(k.raw!=input[i]){k.raw=input[i];k.changed=now;}
      if(k.raw!=k.stable&&now>=k.changed&&now-k.changed>=800000){
        k.stable=k.raw;
        if(!i){if(k.stable){down=k.changed;held=false;}else release=true;}
        else side_down=k.stable;
      }
    }
    if(main&&side)suppressed=true;
    if(suppressed){if(!a.raw&&!b.raw&&!a.stable&&!b.stable){suppressed=false;down=0;held=false;}return ButtonInput::None;}
    if(a.stable&&a.raw&&!b.raw&&down&&!held&&now>=down&&now-down>=30000000){held=true;return ButtonInput::ToggleRelayOnly;}
    if(release){const bool tap=down&&!held;down=0;held=false;if(tap)return ButtonInput::NextPage;}
    return side_down?ButtonInput::NextPage:ButtonInput::None;
  }
};
}
