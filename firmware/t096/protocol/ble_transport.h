/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "authority_codec.h"
#include "group_control.h"
struct r2_ble_event {uint64_t at;uint32_t incarnation;uint8_t peer;uint16_t length;uint8_t data[R2_GROUP_CONTROL_MAX];};
/* Explicit enrolled static random addresses; all operations use public APIs.
 * One explicitly bound serialized workqueue owner calls poll/send during normal service.
 * send refuses other contexts to preserve ATT/TX resource backpressure.
 * After stop has completed, an independent owner may poll only for closure. */
int r2_ble_start(unsigned self,unsigned count,const uint8_t addresses[][6]);
void r2_ble_poll(uint64_t now);
bool r2_ble_ready(unsigned peer);
bool r2_ble_receive(struct r2_ble_event *event);
int r2_ble_send(unsigned peer,const uint8_t *data);
void r2_ble_disconnect(unsigned peer);
void r2_ble_stop(void);
bool r2_ble_closed(void);
uint32_t r2_ble_drops(void);
uint32_t r2_ble_receipts(void);
uint32_t r2_ble_errors(void);
uint32_t r2_ble_stages(void);
void r2_ble_topology(unsigned coordinator,unsigned members);
/* Explicit volatile public-client profile only; default start retains all
 * provisioned direct peers. Stops reconnecting a selected public leaf. */
void r2_ble_public_client(unsigned peer);

uint32_t r2_ble_parameters(unsigned peer);

int r2_ble_stop_error(void);

/* Display-only activity from an actual decoded/captured timing PDU. SOURCE
 * marks its original; SERVICE marks carried corrections. Grants no authority. */
void r2_ble_timing_lamps(bool transmit,bool corrections,uint64_t now);
#define R2_BLE_LAMP_PULSE_US UINT64_C(375000)
void r2_ble_lamps(uint8_t out[8],uint64_t now);

/* Serialized owner requests discovery through the existing scan resource. */
void r2_ble_precision_discovery(bool enabled);
void r2_ble_precision_draining(bool enabled); /* Pause owned scan during PA drain. */
void r2_ble_serving(bool enabled);
/* Serialized public-controller acquisition owners. Established periodic sync
 * may coexist with connections; implicit sync acquisition and initiation may
 * not contend for the pinned controller's scan set. */
void r2_ble_sync_acquiring(bool enabled);
bool r2_ble_initiating(void);

int r2_ble_field_start(unsigned self,const uint64_t identity[4],const uint8_t addresses[][6]);
unsigned r2_ble_admission_peer(void);
/* Observed queue depth and immutable public-API TX slots, no RF-time claim. */
uint32_t r2_ble_backlog(void);

/* Discovery IDs are untrusted until the authenticated control handshake. */
int r2_ble_group_start(uint64_t identity,uint64_t group,uint32_t epoch,const uint8_t address[6]);
uint64_t r2_ble_peer_identity(unsigned peer,uint8_t address[6]);
void r2_ble_peer_authenticated(unsigned peer,bool authenticated);

/* Separate transient admission connection; never a fifth GM/control slot. */
#define R2_BLE_ADMISSION_PEER 4u
bool r2_ble_admission_server(void);
bool r2_ble_admission_connected(void);
/* Original transient resource expiry; zero means no usable owner. */
uint64_t r2_ble_admission_deadline(void);
/* Original last G5 create submission, consumed once by next_poll. */
uint64_t r2_ble_gm5_attempt_at(void);
bool r2_ble_admission_due(uint64_t now);
void r2_ble_admission_release(void);

void r2_ble_group_client(bool enabled);
void r2_ble_client_bootstrap(bool needed,bool urgent);

void r2_ble_client_window(bool enabled);
void r2_ble_client_defer(uint64_t until);

bool r2_ble_control_available(void);
bool r2_ble_control_member(void);
int r2_ble_admission_promote(void);

int r2_ble_admission_bind(uint64_t identity,const uint8_t canonical_address[6]);
void r2_ble_admission_completed(uint64_t identity,uint64_t now);
/* Discovery preference only. An authenticated live relay lease pins renewals
 * to its parent; all candidates still prove PSK, boot, scope and capability. */
void r2_ble_admission_policy(uint64_t parent,bool seek_relay);
void r2_ble_admission_anchor_deadline(uint64_t peer,uint64_t until);

/* Field scheduling only: no membership, demand or timing authority. */
void r2_ble_field_admission_window(bool allowed,bool renewal);

/* Bounded bootstrap scheduling fallback; grants no relay demand. */
bool r2_ble_admission_urgent(uint64_t now);

void r2_ble_group_relay(bool enabled);

int r2_ble_admission_nonce(uint64_t *out);

bool r2_ble_admission_scan_opportunity(void);

/* Existing guest-initiation scan reservation; no added radio time. */
bool r2_ble_admission_reserves_scan(uint64_t now);

/* V14 shared client window phase; one authenticated coarse alignment per boot. */
uint64_t r2_ble_client_window_start(uint64_t now);
bool r2_ble_client_window_open(uint64_t now,uint32_t width);
bool r2_ble_client_align(uint64_t now,uint64_t utc_us);
void r2_ble_client_seek(uint64_t now,bool cold);

struct k_thread;
/* Before transport start, one immutable exact queue thread. Never ISR. */
int r2_ble_owner_bind(struct k_thread *owner);
/* Serialized owner sets the original submitted-age expiry before each cycle.
 * Public GATT calls execute on syswork and return synchronously to this owner. */
void r2_ble_owner_deadline(uint64_t until);
struct r2_transport_fault {uint64_t at;int error;unsigned peer,operation;
 uint32_t bridge_calls,bridge_queue_max_us,bridge_call_max_us,bridge_timeouts;bool bridge_pending;
 /* Existing tail padding: source153 first-retirement host/work snapshot. */
 uint16_t host_detail;};
_Static_assert(sizeof(struct r2_transport_fault)==40&&offsetof(struct r2_transport_fault,host_detail)==38,"Preserve existing fault storage");
void r2_ble_fault_snapshot(struct r2_transport_fault *out);
/* Diagnostic-only fixed vector, source/boot bound by the existing JOIN reply.
 * No controller calls or authority. Schema layout is documented with the build. */
#define R2_GM_TRANSPORT_WORDS 84u
void r2_ble_gm_diagnostics(uint32_t out[R2_GM_TRANSPORT_WORDS]);

/* Normal G5 only. Exactly one wanted persistent coordination identity; all
 * other proved candidates remain in network inventory, outside control slots. */
void r2_ble_gm5_policy(bool enabled,uint64_t selected,bool inventory_due);

/* GM profile only: retain ordinary incoming client JOIN; disable outgoing
 * GM coordination and promotion. Policy changes require an acknowledged scan
 * stop before the management channel owner changes. RC defaults remain false. */
void r2_ble_gm7_server(bool enabled);
/* Ordinary RC service only. GM management scanning remains enabled. */
void r2_ble_gm7_source(bool available);
int r2_ble_gm7_scan(bool aligned);
