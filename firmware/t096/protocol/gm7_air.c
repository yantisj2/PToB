/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_air.h"
#include <errno.h>
#include <string.h>
_Static_assert(offsetof(struct gm7_job, ad) ==
                   offsetof(struct gm7_job, pdu_prefix) + 12,
               "Frozen AUX header and AD must be contiguous");
int gm7_air_build(uint8_t primary[15], struct gm7_job *j,
                  const uint8_t address[6], uint8_t channel) {
  if (!primary || !j || !address || channel > 36 || !j->length ||
      j->length > GM7_MAX_AD_BYTES || j->state != GM7_JOB_QUEUED ||
      (address[5] & 0xc0) != 0xc0)
    return -EINVAL;
  uint32_t airtime;
  if (gm7_complete_airtime(&airtime, j->length, 13, 10, 1, 2) ||
      GM7_AUX_TARGET_US + (uint32_t)(11 + 10 + j->length) * 4 > 2000)
    return -ERANGE;
  uint16_t adi = (uint16_t)((14u << 12) | (j->buffer_version & 0xfff));
  /* Header type7, random advertiser address. Common extended header mode0.
   * Flags: AdvA, ADI, AuxPtr. Offset units30us, clock accuracy500ppm. */
  memset(primary, 0, 15);
  primary[0] = 0x47;
  primary[1] = 13;
  primary[2] = 12;
  primary[3] = 0x19;
  memcpy(primary + 4, address, 6);
  primary[10] = (uint8_t)adi;
  primary[11] = (uint8_t)(adi >> 8);
  primary[12] = channel;
  primary[13] = GM7_AUX_OFFSET_US / 30;
  primary[14] = 0x20;
  uint8_t *aux = j->pdu_prefix;
  memset(aux, 0, 12);
  aux[0] = 0x47;
  aux[1] = (uint8_t)(10 + j->length);
  aux[2] = 9;
  aux[3] = 9;
  memcpy(aux + 4, address, 6);
  aux[10] = (uint8_t)adi;
  aux[11] = (uint8_t)(adi >> 8);
  return 0;
}
int gm7_air_received(const uint8_t *p, size_t available, const uint8_t **ad,
                     size_t *length, uint8_t address[6]) {
  if (!p || !ad || !length || !address || available < 12 || (p[0] & 15) != 7 ||
      (p[0] & 0x80) || !(p[0] & 0x40) || p[1] + 2u > available || p[1] < 10 ||
      p[2] != 9 || p[3] != 9)
    return -EINVAL;
  /* A complete AUX_ADV_IND, with exactly AdvA+ADI and no AuxPtr, SyncInfo,
   * ACAD or CTE. HMAC/nonce/scope validation remains in the serialized owner.
   */
  size_t n = p[1] - 10u;
  const uint8_t *body = p + 12;
  if (n < 4 || n > GM7_MAX_AD_BYTES || body[0] + 1u != n || body[1] != 255 ||
      body[2] != 255 || body[3] != 255 || n < 8 || memcmp(body + 4, "G7V2", 4))
    return -EPROTO;
  memcpy(address, p + 4, 6);
  *ad = body;
  *length = n;
  return 0;
}
