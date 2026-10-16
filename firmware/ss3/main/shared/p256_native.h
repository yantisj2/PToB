/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define V12_P256_PUBLIC_BYTES 64u /* raw big-endian X || Y, not SEC1 */
#define V12_P256_SIGNATURE_BYTES 64u /* raw big-endian r || low-S */
#define V12_P256_ENTROPY_BYTES 128u

/* One worker owns the fixed Mbed TLS arena and restart operation budget. No authority, key
 * enrollment, wire scope, deadline or physical association follows from a
 * successful primitive. Mbed TLS3.6.7 is a pinned software comparator backend;
 * production backend/security/throughput acceptance remains OPEN. */
bool v12_p256_signature_canonical(const uint8_t signature[64]);
int v12_p256_public_valid(const uint8_t public_xy[64]);
int v12_p256_verify_digest(const uint8_t public_xy[64],const uint8_t digest[32],
                           const uint8_t signature[64]);
/* Secure independent entropy is supplied by the driver-owned worker. The
 * fixed pool bounds all library RNG calls; insufficient entropy refuses. */
int v12_p256_sign_digest(const uint8_t private_key[32],const uint8_t digest[32],
                         const uint8_t entropy[V12_P256_ENTROPY_BYTES],
                         uint8_t signature[64]);
/* Build-owned canonicalization/KAT only; never accepts a high-S wire record. */
int v12_p256_normalize_signature(uint8_t signature[64]);
typedef bool (*v12_p256_work_guard)(void *context);
/* Supported restartable APIs yield through the worker's epoch/deadline guard.
 * Logical operation count does not establish a physical walltime/CPU bound. */
int v12_p256_verify_work(const uint8_t public_xy[64],const uint8_t digest[32],
                        const uint8_t signature[64],v12_p256_work_guard guard,void *context);
int v12_p256_sign_work(const uint8_t private_key[32],const uint8_t digest[32],
                      const uint8_t entropy[V12_P256_ENTROPY_BYTES],uint8_t signature[64],
                      v12_p256_work_guard guard,void *context);
int v12_p256_keypair_work(const uint8_t entropy[V12_P256_ENTROPY_BYTES],
                         uint8_t private_key[32],uint8_t public_xy[64],
                         v12_p256_work_guard guard,void *context);
