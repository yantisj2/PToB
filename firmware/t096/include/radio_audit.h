/* Bounded read-only bench evidence, populated by serialized owners, never ISR. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define R2_RADIO_AUDIT_RECORDS 8u
enum r2_radio_audit_kind {R2_AUDIT_TX=1,R2_AUDIT_RX=2,R2_AUDIT_MATCH=3};
struct r2_radio_audit_record {
 uint64_t peer_boot,root_boot,tick,ns,wire_ticks,available,original_available,followup,activation_lo,activation_hi,generation;
 uint32_t sequence,epoch,peer_epoch,root_epoch,train,publication,crc;
 uint32_t address_raw,raw;
 uint16_t event;uint8_t channel,kind;bool disciplined;
};
struct r2_radio_audit_status {uint32_t produced,overwritten;};
bool r2_radio_audit_selected(uint16_t event);
void r2_radio_audit_put(const struct r2_radio_audit_record *);
bool r2_radio_audit_read(uint32_t after,struct r2_radio_audit_record *,struct r2_radio_audit_status *);
