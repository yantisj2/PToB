// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// This is the ONLY controller-specific file. See docs/esp32s3-port.md before
// changing its ABI facts. It observes the SDK; it never programs the radio.
#include "radio.h"
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
extern void *volatile lld_sync_env[], *volatile lld_per_adv_env[];
extern uint16_t lld_exp_sync_pos_tab[4];
extern void *r_emi_get_mem_addr_by_offset(uint16_t);
extern void r_lld_sync_frm_rx_isr(uint8_t);
extern void r_lld_sync_process_pkt_rx_aux_sync_ind(uint8_t, uint8_t);
extern void r_lld_rxdesc_free(void);
extern void r_lld_per_adv_frm_isr_hack(uint8_t, uint32_t, uint8_t);

enum { RX_SLOT = 0x150 / 4, AUX_SLOT = 0x160 / 4,
       FREE_SLOT = 0x2a8 / 4, TX_SLOT = 0x2f0 / 4, CAPACITY = 16 };
static portMUX_TYPE scope_lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR RefRadioEvent queue[CAPACITY], pending;
static DRAM_ATTR RefRadioBridge rx_bridge;
static volatile uint32_t head, tail, generation = 1, dropped;
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
{ return p[0] == 189 && p[1] == 0x21 && p[18] == 'R' && p[19] == 'E' &&
         p[20] == 'F' && p[21] == '1' && p[22] == 1; }

static bool IRAM_ATTR bridge(RefRadioBridge *out)
{
    uint64_t before, after;
    if (!ref_counter_port_now(&before)) return false;
    const BleTime bt = r_rwip_time_get();
    if (!ref_counter_port_now(&after) || after < before || after - before > 20 * REF_TICKS_PER_US ||
        bt.slots > 0x0fffffff || bt.half_us > 624) return false;
    *out = (RefRadioBridge){before, after, bt.slots, bt.half_us};
    return true;
}

static void IRAM_ATTR enqueue(const RefRadioEvent *event)
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
                        pending.tick = local; pending.available_tick = rx_bridge.after;
                        pending.generation = generation;
                        pending.event = *(volatile uint16_t *)(env + 74);
                        pending.channel = (uint8_t)channel; pending.transmit = false;
                        for (unsigned i = 0; i < 190; ++i) pending.bytes[i] = p[i];
                        barrier();
                        bool stable = control == d[0] && status == d[1] && header == d[2] &&
                            ext == d[8] && offset == d[9] && metadata == d[3] &&
                            coarse == ((uint32_t)d[4] | ((uint32_t)d[5] << 16)) && fine == d[6] &&
                            pending.event == *(volatile uint16_t *)(env + 74) &&
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
    const bool mapped = bridge(&rx_bridge);
    r_lld_sync_frm_rx_isr(activity);
    in_rx = false;
    if (mapped && pending_valid && packets == 1 && candidates == 1 && !fault) enqueue(&pending);
    if ((uint32_t)(esp_cpu_get_cycle_count() - started) > 96000) fault = true;
}

static void IRAM_ATTR tx_hook(uint8_t activity, uint32_t cookie, uint8_t status)
{
    const uint32_t started = esp_cpu_get_cycle_count();
    if (installed && !fault && activity < 6 && status == 0 &&
        ram((const void *)lld_per_adv_env, 24)) {
        volatile uint8_t *env = lld_per_adv_env[activity];
        volatile uint8_t *controls = r_emi_get_mem_addr_by_offset(0x400);
        volatile uint8_t *descriptors = r_emi_get_mem_addr_by_offset(0x1400);
        if (ram((const void *)env, 84) && ram((const void *)controls, (activity + 1) * 90) &&
            ram((const void *)descriptors, activity * 126 + 14)) {
            volatile uint16_t *cs = (volatile uint16_t *)(controls + activity * 90);
            volatile uint16_t *d = (volatile uint16_t *)(descriptors + activity * 126);
            const uint32_t selected_cookie = *(volatile uint32_t *)(env + 4);
            const uint16_t event = *(volatile uint16_t *)(env + 76);
            const uint16_t offset = *(volatile uint16_t *)(env + 78);
            const uint16_t length = *(volatile uint16_t *)(env + 80);
            if (selected_cookie == cookie && env[82] == activity && env[83] == 1 &&
                (cs[0] & 31) == 6 && cs[26] == 0x1400 + 126 * activity && cs[40] == event &&
                d[0] == 0x8000 && d[1] == 0xbf27 && d[2] == offset && d[3] == 0 &&
                length == 190 && offset >= 0x1400 && offset < 0xcc00 && (offset & 1023) + 190 <= 1024) {
                volatile uint8_t *p = r_emi_get_mem_addr_by_offset(offset);
                RefRadioEvent result;
                RefRadioBridge b;
                const uint16_t map[] = {cs[17], cs[18], (uint16_t)(cs[19] & 31)};
                const uint32_t access = (uint32_t)cs[6] | ((uint32_t)cs[7] << 16);
                uint64_t available, tick;
                if (ram((const void *)p, 190) && family(p) && bridge(&b) &&
                    ref_counter_port_now(&available) && ref_radio_tx_tick(&b, cookie, available, &tick) &&
                    ref_radio_channel(access, map, event, &result.channel)) {
                    result.tick = tick; result.available_tick = available;
                    result.event = event; result.generation = generation; result.transmit = true;
                    for (unsigned i = 0; i < 190; ++i) result.bytes[i] = p[i];
                    barrier();
                    bool stable = selected_cookie == *(volatile uint32_t *)(env + 4) &&
                        event == *(volatile uint16_t *)(env + 76) && offset == *(volatile uint16_t *)(env + 78) &&
                        length == *(volatile uint16_t *)(env + 80) && d[0] == 0x8000 &&
                        d[1] == 0xbf27 && d[2] == offset && d[3] == 0 && cs[40] == event;
                    for (unsigned i = 0; i < 190; ++i) stable = stable && result.bytes[i] == p[i];
                    if (stable) enqueue(&result);
                }
            }
        }
    }
    r_lld_per_adv_frm_isr_hack(activity, cookie, status);
    if ((uint32_t)(esp_cpu_get_cycle_count() - started) > 96000) fault = true;
}

bool ref_radio_port_install(void)
{
    if (installed || xPortGetCoreID() != 0 || esp_clk_cpu_freq() != 240000000 ||
        !r_ip_funcs_p || !r_plf_funcs_p || !ram(r_ip_funcs_p, 0x2f4) ||
        (uintptr_t)lld_sync_env != 0x3fcefc30 || (uintptr_t)lld_per_adv_env != 0x3fcefc58 ||
        (uintptr_t)lld_exp_sync_pos_tab != 0x3fcefc80 ||
        r_ip_funcs_p[RX_SLOT] != (void *)r_lld_sync_frm_rx_isr ||
        r_ip_funcs_p[AUX_SLOT] != (void *)r_lld_sync_process_pkt_rx_aux_sync_ind ||
        r_ip_funcs_p[FREE_SLOT] != (void *)r_lld_rxdesc_free ||
        r_ip_funcs_p[TX_SLOT] != (void *)r_lld_per_adv_frm_isr_hack ||
        r_plf_funcs_p[0xbc / 4] != (void *)r_emi_get_mem_addr_by_offset) return false;
    portENTER_CRITICAL(&scope_lock);
    r_ip_funcs_p[RX_SLOT] = (void *)rx_hook;
    r_ip_funcs_p[AUX_SLOT] = (void *)aux_hook;
    r_ip_funcs_p[FREE_SLOT] = (void *)free_hook;
    r_ip_funcs_p[TX_SLOT] = (void *)tx_hook;
    barrier(); installed = true;
    portEXIT_CRITICAL(&scope_lock);
    return true;
}

void ref_radio_port_scope(void)
{
    portENTER_CRITICAL(&scope_lock);
    tail = head;
    if (++generation == 0) fault = true;
    portEXIT_CRITICAL(&scope_lock);
}

bool ref_radio_port_pop(RefRadioEvent *event)
{
    if (tail == head) return false;
    barrier(); *event = queue[tail % CAPACITY]; barrier(); ++tail;
    return true;
}

bool ref_radio_port_fault(void) { return fault; }
uint32_t ref_radio_port_dropped(void) { return dropped; }
uint32_t ref_radio_port_generation(void) { return generation; }

bool ref_radio_port_stop(void)
{
    if (in_rx) return false;
    if (installed) {
        if (r_ip_funcs_p[RX_SLOT] != (void *)rx_hook || r_ip_funcs_p[AUX_SLOT] != (void *)aux_hook ||
            r_ip_funcs_p[FREE_SLOT] != (void *)free_hook || r_ip_funcs_p[TX_SLOT] != (void *)tx_hook) return false;
        portENTER_CRITICAL(&scope_lock);
        installed = false;
        r_ip_funcs_p[RX_SLOT] = (void *)r_lld_sync_frm_rx_isr;
        r_ip_funcs_p[AUX_SLOT] = (void *)r_lld_sync_process_pkt_rx_aux_sync_ind;
        r_ip_funcs_p[FREE_SLOT] = (void *)r_lld_rxdesc_free;
        r_ip_funcs_p[TX_SLOT] = (void *)r_lld_per_adv_frm_isr_hack;
        barrier();
        portEXIT_CRITICAL(&scope_lock);
    }
    return ref_counter_port_stop();
}
