// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef PTOB_MINIMAL_RX_H
#define PTOB_MINIMAL_RX_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { PTOB_BOOTSTRAPPING, PTOB_SEARCHING, PTOB_SYNCING, PTOB_ACQUIRING,
               PTOB_TIME_VALID, PTOB_TIME_LOST, PTOB_STOPPED, PTOB_FAULT } PtobRxState;
typedef struct {
    uint8_t key[32];
    uint32_t key_epoch;
    bool discipline_posix; // This task must be the app's sole wall-clock writer.
    bool step_on_first_lock;
    // Optional measured phase correction for one authenticated grandmaster.
    // Zero source ID disables it. A positive offset advances delivered UTC.
    uint64_t phase_source_id;
    int32_t phase_adjust_ns;
} PtobRxConfig;
typedef struct {
    PtobRxState state;
    int64_t utc_ns, phase_ns;
    uint64_t source_id;
    int32_t rate_ppb;
    uint32_t pairs, misses, drops;
    bool posix_adjusting;
} PtobRxStatus;

// One instance, one start per boot. Initialize the app's NimBLE host first.
// Start, read and stop are task-context calls. The receiver owns the scanner,
// one temporary JOIN connection, and one periodic sync while following.
bool ptob_rx_start(const PtobRxConfig *config);
// Chain these from the application's existing NimBLE sync/reset callbacks.
void ptob_rx_host_sync(void);
void ptob_rx_host_reset(void);
bool ptob_rx_now_ns(int64_t *utc_ns);
PtobRxStatus ptob_rx_status(void);
void ptob_rx_stop(void);
#endif
