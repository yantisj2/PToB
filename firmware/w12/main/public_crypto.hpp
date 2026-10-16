#pragma once
#include <cstddef>
#include <cstdint>
namespace v12 {
// Public P-256 verifier: raw uncompressed XY and canonical low-S r||s.
// Authenticates bytes only. Caller must separately prove trust, original scope,
// freshness, expiry and physical association. No client secret is accepted.
bool verify_public(const uint8_t xy[64],const uint8_t signature[64],
                   const uint8_t* bytes,size_t length,const uint8_t* domain,size_t domain_length);
}
