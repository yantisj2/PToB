// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// This is the ONLY controller-specific file. See the pinned adapter notes before
// changing its ABI facts. It observes the SDK; it never programs the radio.
#include "receive_stamp.h"
#include "counter.h"
#include "radio_math.h"
#include "ref_timebase.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_private/esp_clk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>
#if !CONFIG_IDF_TARGET_ESP32S3 || CONFIG_BT_CTRL_PINNED_TO_CORE != 0 || CONFIG_BT_NIMBLE_PINNED_TO_CORE != 0 || CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ != 240
#error "ESP32-REF controller adapter requires pinned ESP32-S3 CPU0 / 240MHz"
#endif

typedef struct { uint32_t slots, half_us; } BleTime;
_Static_assert(sizeof(BleTime) == 8 && sizeof(void *) == 4, "Pinned S3 ABI differs");
extern BleTime r_rwip_time_get(void);
extern void **r_ip_funcs_p, **r_plf_funcs_p;
extern void *p_lld_env;
extern void *volatile lld_sync_env[];
extern uint16_t lld_exp_sync_pos_tab[4];
extern void *r_emi_get_mem_addr_by_offset(uint16_t);
extern void r_lld_sync_frm_rx_isr(uint8_t);
extern void r_lld_sync_process_pkt_rx_aux_sync_ind(uint8_t, uint8_t);
extern void r_lld_rxdesc_free(void);

enum { RX_SLOT = 0x150 / 4, AUX_SLOT = 0x160 / 4,
       FREE_SLOT = 0x2a8 / 4, CAPACITY = 8 };
static portMUX_TYPE scope_lock = portMUX_INITIALIZER_UNLOCKED;
typedef struct {
    uint8_t bytes[PTOB_GR3_BYTES];
    PtobCapture capture;
} RxEvent;

static DRAM_ATTR RxEvent queue[CAPACITY], pending;
static DRAM_ATTR uint32_t bridge_us;
static DRAM_ATTR RefRadioBridge rx_bridge;
static volatile uint32_t head, tail, dropped;
static volatile bool installed, fault, in_rx;
static volatile unsigned packets, candidates;
static bool pending_valid;

static inline __attribute__((always_inline)) void barrier(void)
{ __asm__ volatile("memw" ::: "memory"); }
static inline __attribute__((always_inline)) bool ram(const void *p, size_t n)
{
    const uintptr_t a = (uintptr_t)p;
    return n <= 0x78000 && a >= 0x3fc88000 && a <= 0x3fd00000 - n;
}
static inline __attribute__((always_inline)) bool family(const volatile uint8_t *p)
{
    return p[0] == 189 && p[1] == 255 && p[2] == 255 && p[3] == 255 &&
           p[4] == 'G' && p[5] == 'R' && p[6] == 3;
}

static bool IRAM_ATTR bridge(RefRadioBridge *out)
{
    uint64_t before, after;
    if (!ref_counter_port_now(&before)) return false;
    const BleTime bt = r_rwip_time_get();
    if (!ref_counter_port_now(&after) || after < before || after - before > bridge_us * REF_TICKS_PER_US ||
        bt.slots > 0x0fffffff || bt.half_us > 624) return false;
    *out = (RefRadioBridge){before, after, bt.slots, bt.half_us};
    return true;
}

static void IRAM_ATTR enqueue(const RxEvent *event)
{
    if (head == UINT32_MAX) { fault = true; return; }
    if (head - tail >= CAPACITY) { ++dropped; return; }
    queue[head % CAPACITY] = *event;
    barrier();
    ++head;
}

static void IRAM_ATTR free_hook(void)
{
    if (in_rx) ++packets;
    r_lld_rxdesc_free(); // The original SDK function always runs exactly once.
}

static void IRAM_ATTR aux_hook(uint8_t activity, uint8_t first)
{
    if (installed && !fault && in_rx && first == 1 && activity < 6 &&
        ram(p_lld_env, 217) && ram((const void *)lld_sync_env, 24)) {
        const unsigned index = ((volatile uint8_t *)p_lld_env)[216];
        volatile uint16_t *base = r_emi_get_mem_addr_by_offset(0x1000);
        if (index < 10 && ram((const void *)base, 200)) {
            volatile uint16_t *d = base + index * 10;
            const uint16_t control = d[0], status = d[1], header = d[2], ext = d[8], offset = d[9];
            const unsigned length = header >> 8, ext_length = ext & 63;
            // Plain AUX_SYNC_IND, one complete AD record. Reject chains/ACAD
            // and unsupported packet forms instead of guessing their endpoint.
            if ((control & 0x8000) && !(status & 0x603d) && (header & 15) == 7 &&
                !(ext & 0xffc0) && ext_length == 0 && length == 191 &&
                offset >= 0x1400 && offset < 0xcc00 && (offset & 1023) + 190 <= 1024) {
                volatile uint8_t *p = r_emi_get_mem_addr_by_offset(offset);
                volatile uint8_t *env = lld_sync_env[activity];
                if (ram((const void *)p, 190) && ram((const void *)env, 89) && family(p) &&
                    env[88] == 0 && ram(lld_exp_sync_pos_tab, 8)) {
                    ++candidates;
                    const uint32_t coarse = (uint32_t)d[4] | ((uint32_t)d[5] << 16);
                    const uint16_t fine = d[6], metadata = d[3];
                    const unsigned channel = metadata >> 8;
                    const unsigned position = ((volatile uint16_t *)lld_exp_sync_pos_tab)[0];
                    uint64_t local;
                    if (channel <= 36 && position >= 40 && position <= 167 &&
                        ref_radio_rx_tick(&rx_bridge, coarse, fine, position - 40, &local) &&
                        local <= rx_bridge.after && rx_bridge.after - local <= REF_MS_TICKS(250)) {
                        pending.capture.tick = local;
                        pending.capture.event = *(volatile uint16_t *)(env + 74);
                        pending.capture.channel = (uint8_t)channel;
                        for (unsigned i = 0; i < 190; ++i) pending.bytes[i] = p[i];
                        barrier();
                        bool stable = control == d[0] && status == d[1] && header == d[2] &&
                            ext == d[8] && offset == d[9] && metadata == d[3] &&
                            coarse == ((uint32_t)d[4] | ((uint32_t)d[5] << 16)) && fine == d[6] &&
                            env == lld_sync_env[activity] && env[88] == 0 &&
                            pending.capture.event == *(volatile uint16_t *)(env + 74) &&
                            position == ((volatile uint16_t *)lld_exp_sync_pos_tab)[0];
                        for (unsigned i = 0; i < 190; ++i) stable = stable && pending.bytes[i] == p[i];
                        pending_valid = stable && candidates == 1;
                    }
                }
            }
        }
    }
    r_lld_sync_process_pkt_rx_aux_sync_ind(activity, first);
}

static void IRAM_ATTR rx_hook(uint8_t activity)
{
    const uint32_t started = esp_cpu_get_cycle_count();
    if (in_rx) { fault = true; r_lld_sync_frm_rx_isr(activity); return; }
    in_rx = true; packets = candidates = 0; pending_valid = false;
    const bool mapped = installed && !fault && bridge(&rx_bridge);
    r_lld_sync_frm_rx_isr(activity);
    in_rx = false;
    if (mapped && pending_valid && packets == 1 && candidates == 1 && !fault) enqueue(&pending);
    if ((uint32_t)(esp_cpu_get_cycle_count() - started) > 96000) fault = true;
}

bool ptob_stamp_install(uint32_t max_bridge_us)
{
    if (installed || xPortGetCoreID() != 0 || esp_clk_cpu_freq() != 240000000 ||
        !r_ip_funcs_p || !r_plf_funcs_p || !ram(r_ip_funcs_p, 0x2ac) ||
        (uintptr_t)lld_sync_env != 0x3fcefc30 ||
        (uintptr_t)lld_exp_sync_pos_tab != 0x3fcefc80 ||
        r_ip_funcs_p[RX_SLOT] != (void *)r_lld_sync_frm_rx_isr ||
        r_ip_funcs_p[AUX_SLOT] != (void *)r_lld_sync_process_pkt_rx_aux_sync_ind ||
        r_ip_funcs_p[FREE_SLOT] != (void *)r_lld_rxdesc_free ||
        r_plf_funcs_p[0xbc / 4] != (void *)r_emi_get_mem_addr_by_offset) return false;
    portENTER_CRITICAL(&scope_lock);
    r_ip_funcs_p[RX_SLOT] = (void *)rx_hook;
    r_ip_funcs_p[AUX_SLOT] = (void *)aux_hook;
    r_ip_funcs_p[FREE_SLOT] = (void *)free_hook;
    head = tail = dropped = 0;
    bridge_us = max_bridge_us;
    barrier(); installed = true;
    portEXIT_CRITICAL(&scope_lock);
    return true;
}


// Called only on the NimBLE host (CPU0), the same core as the producer ISR.
void ptob_stamp_flush(void) { tail = head; }
bool ptob_stamp_fault(void) { return fault; }

bool ptob_stamp_match(const uint8_t bytes[PTOB_GR3_BYTES], uint64_t now, PtobCapture *out)
{
    const uint32_t end = head;
    barrier();
    unsigned matches = 0;
    uint32_t consumed = tail;
    for (uint32_t i = tail; i < end; ++i) {
        const RxEvent *item = &queue[i % CAPACITY];
        if (item->capture.tick > now) continue;
        if (now - item->capture.tick >= REF_MS_TICKS(100)) {
            consumed = i + 1;
        } else if (!memcmp(item->bytes, bytes, PTOB_GR3_BYTES)) {
            *out = item->capture;
            consumed = i + 1;
            ++matches;
        }
    }
    barrier();
    tail = consumed;
    return matches == 1;
}

bool ptob_stamp_stop(void)
{
    if (!installed) return true;
    if (xPortGetCoreID() != 0 || in_rx ||
        r_ip_funcs_p[RX_SLOT] != (void *)rx_hook ||
        r_ip_funcs_p[AUX_SLOT] != (void *)aux_hook ||
        r_ip_funcs_p[FREE_SLOT] != (void *)free_hook) return false;
    portENTER_CRITICAL(&scope_lock);
    installed = false;
    barrier();
    r_ip_funcs_p[RX_SLOT] = (void *)r_lld_sync_frm_rx_isr;
    r_ip_funcs_p[AUX_SLOT] = (void *)r_lld_sync_process_pkt_rx_aux_sync_ind;
    r_ip_funcs_p[FREE_SLOT] = (void *)r_lld_rxdesc_free;
    barrier();
    portEXIT_CRITICAL(&scope_lock);
    ptob_stamp_flush();
    fault = false;
    return true;
}
