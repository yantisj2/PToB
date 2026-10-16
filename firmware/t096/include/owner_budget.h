/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* Expensive authenticated records per serialized owner cycle. Deferred inputs
 * retain original physical/host capture times and original expiry checks.
 * Radio intervals, signed permissions, queue capacities and deadlines unchanged. */
#define R2_OWNER_CONTROL_RX 2u
#define R2_OWNER_CONTROL_TX 2u
#define R2_OWNER_CAPTURE_RX 2u
