#include "public_receiver.hpp"
#include "public_crypto.hpp"
extern "C" {
#include "shared/public_codec.h"
}
namespace v12 {
namespace {void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}}
void PublicReceiver::receive(const uint8_t* b,size_t n){
  struct v12_public_auth_view view{};
  if(v12_public_auth_view(&view,b,n)){inc(status.malformed);return;}
  if(b[5]==129||b[5]==130){
    RootGrant root;RelayGrant relay;const bool is_root=b[5]==129;
    if(is_root?!decode_root(b,n,CandidatePolicy,root):!decode_relay(b,n,CandidatePolicy,relay)){inc(status.malformed);return;}
    const uint8_t issuer=is_root?root.issuer:relay.issuer;
    if(!trust.provisioned||trust.issuer!=issuer){inc(status.no_trusted_key);return;}
    const auto* domain=is_root?RootGrantDomain:RelayGrantDomain;
    if(!verify_public(trust.xy,view.signature,view.body,view.body_bytes,domain,view.domain_bytes)){inc(status.invalid_signature);return;}
    inc(status.authenticated);
    // Verified bytes do not establish cold current UTC or physical authority.
    if(is_root){parents.add_authenticated(root,false,0);inc(status.freshness_unknown);}
    else if(!parents.relay_parent(relay,false,0))inc(status.missing_parent);
    return;
  }
  if(b[5]!=1&&b[5]!=2){inc(status.malformed);return;}
  v12_public_timing timing{};
  if(v12_public_timing_unpack(&timing,b,n)){inc(status.malformed);return;}
  // Without a fresh trusted parent and supported capture adapter no output
  // follows. Shape alone creates neither authentication nor usable originals.
  inc(status.timing_no_capture);
}
}
