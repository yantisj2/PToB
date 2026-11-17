#!/usr/bin/env python3
"""Pinned USBD suspend workaround lifecycle with synthetic register boundaries.

The SOF-only resume result is an explicit silicon model, not a physical
reproduction of a USB outage. Actual generated enable/disable/suspend bodies
must retain the upstream anomaly 211 workaround for the enabled lifetime.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

from usb_ep0_recovery import generate

APP = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deps', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    generate(args.deps / 'zephyr', out / 'common.c', 'usb-dma-health')
    body = (out / 'common.c').read_text()

    def function(name):
        match = re.search(r'^.*?\b' + name + r'\([^;]*?\)\s*\{', body, re.M)
        assert match, name
        depth, end = 1, body.index('{', match.start()) + 1
        while depth:
            depth += (body[end] == '{') - (body[end] == '}')
            end += 1
        return body[match.start():end]

    default = re.search(
        r'#ifndef NRF_USBD_COMMON_USE_WORKAROUND_FOR_ANOMALY_211\n.*?\n#endif',
        body, re.S)
    assert default
    source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "usb_health.h"
#define __ASSERT_NO_MSG(x) assert(x)
#define __ISB() ((void)0)
#define __DSB() ((void)0)
#define IS_ENABLED(x) (x)
#define CONFIG_NRF_USBD_ISO_IN_ZLP 0
#define K_FOREVER 0
#define K_MSEC(x) (x)
#define USBD_EVENTCAUSE_READY_Msk 1u
#define USBD_EVENTCAUSE_RESUME_Msk 2u
#define USBD_EVENTCAUSE_USBWUALLOWED_Msk 4u
#define USBD_LOWPOWER_LOWPOWER_LowPower 1u
#define USBD_LOWPOWER_LOWPOWER_ForceNormal 0u
#define USBD_LOWPOWER_LOWPOWER_Pos 0u
#define USBD_ISOSPLIT_SPLIT_HalfIN 0u
#define USBD_ISOSPLIT_SPLIT_Pos 0u
#define USBD_ISOINCONFIG_RESPONSE_ZeroData 0u
#define USBD_ISOINCONFIG_RESPONSE_NoResp 1u
#define USBD_ISOINCONFIG_RESPONSE_Pos 0u
#define NRF_USBD_COMMON_EPIN_CNT 9u
#define NRF_USBD_COMMON_EPIN_BITPOS_0 0u
#define NRF_USBD_COMMON_EPIN0 0u
#define NRF_USBD_COMMON_EPOUT0 0u
enum {NRFX_DRV_STATE_UNINITIALIZED, NRFX_DRV_STATE_INITIALIZED,
      NRFX_DRV_STATE_POWERED_ON};
static struct {
 uint32_t ENABLE, EVENTCAUSE, ISOSPLIT, ISOINCONFIG, EVENTS_ENDEPIN[9], LOWPOWER, EVENTS_SOF, INTENSET;
} regs;
#define NRF_USBD (&regs)
static uint32_t unlock_register, workaround_register;
/* Only the absolute undocumented MMIO endpoints are substituted. */
static volatile uint32_t *mmio(uintptr_t address) {
 assert(address == UINT32_C(0x4006ec00) || address == UINT32_C(0x4006ed14));
 return address == UINT32_C(0x4006ec00) ? &unlock_register : &workaround_register;
}
static bool affected187, affected211, affected223;
static bool m_first_enable, m_bus_suspend, r2_wakeup_requested, r2_host_resume_pending;
static uint32_t r2_host_resume_inten;
static unsigned m_drv_state, dma_available, m_dma_odd, dma_clears, stops, dma_ep, r2_lifecycle_failure;
static int64_t fake_ms;
static unsigned atomic_get(unsigned *p){return *p;}
static bool atomic_cas(unsigned *p,unsigned old,unsigned value){if(*p!=old)return false;*p=value;return true;}
static int64_t k_uptime_get(void){return fake_ms;}
static void k_sleep(unsigned ms){fake_ms+=ms;}
static bool irq_is_enabled(unsigned irq){(void)irq;return true;}
static void irq_disable(unsigned irq){(void)irq;}
#define USBD_INTEN_SOF_Msk 2u
#define USBD_IRQn 39
static bool nrf_usbd_common_suspend_check(void){return regs.LOWPOWER!=0;}
static bool nrf_usbd_common_is_started(void){return m_drv_state==NRFX_DRV_STATE_POWERED_ON;}
static uint32_t m_ep_ready, m_ep_dma_waiting, m_last_setup_dir;
static bool nrf_usbd_common_errata_187(void) {return affected187;}
static bool nrf_usbd_common_errata_211(void) {return affected211;}
static bool nrf_usbd_common_errata_223(void) {return affected223;}
static bool nrf_usbd_common_errata_171(void) {return false;}
static bool nrf_usbd_common_errata_166(void) {return false;}
static void usbd_errata_171_begin(void) {assert(false);}
static void usbd_errata_171_end(void) {assert(false);}
static unsigned irq_lock(void) {return 0;}
static void irq_unlock(unsigned key) {(void)key;}
static unsigned k_sem_count_get(const unsigned *sem) {return *sem;}
static int k_sem_take(unsigned *sem, unsigned timeout) {
 (void)timeout;assert(*sem == 1);*sem = 0;return 0;
}
static void k_sem_give(unsigned *sem) {assert(*sem == 0);*sem = 1;}
static void usbd_dma_pending_clear(void) {++dma_clears;}
static void nrf_usbd_common_stop(bool started) {(void)started;++stops;}
static void usbd_ep_dma_start(unsigned ep, uint32_t address, size_t size) {
 (void)address;assert(ep == 0 && size == 1);regs.EVENTS_ENDEPIN[0] = 1;
}
'''
    source += '\n' + default[0] + '\n'
    for name in ('r2_usb_common_failed','r2_usb_fail','r2_usb_dma_take','r2_usb_wait',
                 'usbd_errata_187_211_begin', 'usbd_errata_187_211_end',
                 'usbd_enable', 'nrf_usbd_common_enable',
                 'nrf_usbd_common_disable', 'nrf_usbd_common_suspend'):
        source += '\n' + function(name)
    # Preserve the code and branch structure. Map all absolute MMIO pointer
    # expressions, including the unrelated 166 path, at the hardware boundary.
    source = re.sub(r'\(\(volatile uint32_t \*\)\((0x[0-9a-fA-F]+)\)\)',
                    r'mmio(\1)', source)
    source += r'''
/* Documented silicon boundary: affected 211 can miss resume from SOF-only
 * activity unless the upstream workaround remains installed. This model
 * supplies neither proof that a particular host emitted that sequence nor
 * evidence that it caused a particular physical device's outage. */
static bool modeled_sof_only_resume(void) {
 assert(m_bus_suspend && regs.LOWPOWER);
 if (affected211 && workaround_register != 3) return false;
 regs.LOWPOWER = 0;m_bus_suspend = false;return true;
}
static void reset(unsigned variant) {
 memset(&regs, 0, sizeof regs);unlock_register = workaround_register = 0;
 r2_lifecycle_failure=0;fake_ms=0;r2_wakeup_requested=false;
 affected187 = variant & 1;affected211 = variant & 2;affected223 = variant & 4;
 m_first_enable = true;m_bus_suspend = false;m_drv_state = NRFX_DRV_STATE_INITIALIZED;
 dma_available = 1;m_dma_odd = 0;dma_clears = stops = 0;
 m_ep_ready = m_ep_dma_waiting = m_last_setup_dir = 0;
}
int main(int argc, char **argv) {
 bool require_resume = argc > 1 && !strcmp(argv[1], "require-resume");
 unsigned held = 0, missed = 0;
 for (unsigned variant = 0;variant < 8;variant++) {
  reset(variant);
  for (unsigned cycle = 0;cycle < 3;cycle++) {
   nrf_usbd_common_enable();
   assert(m_drv_state == NRFX_DRV_STATE_POWERED_ON && regs.ENABLE && dma_available);
   assert(workaround_register ==
          (affected211 && NRF_USBD_COMMON_USE_WORKAROUND_FOR_ANOMALY_211 ? 3u : 0u));
   if (workaround_register) ++held;
   if (affected223) assert(!m_first_enable);
   /* A stale queued SUSPEND cannot enter low power after RESUME. */
   m_bus_suspend = false;regs.EVENTCAUSE = USBD_EVENTCAUSE_RESUME_Msk;
   assert(!nrf_usbd_common_suspend() && !regs.LOWPOWER && dma_available);
   /* Legitimate idle suspend still works, and does not request a reset. */
   m_bus_suspend = true;regs.EVENTCAUSE = 0;
   assert(nrf_usbd_common_suspend() && regs.LOWPOWER && dma_available);
   struct r2_usb_health health = {0};
   uint32_t idle[4] = {0x2c1fu, 1, 1, 0};
   for (uint32_t ms = 0;ms <= 300000;ms += 30000)
    assert(!r2_usb_health_stalled(&health, idle, ms));
   bool resumed = modeled_sof_only_resume();
   assert(resumed == (!affected211 || NRF_USBD_COMMON_USE_WORKAROUND_FOR_ANOMALY_211));
   if (!resumed) {
    ++missed;
    /* The existing observer sees idle suspend; a CPU stall is unnecessary. */
    assert(regs.LOWPOWER && !r2_usb_health_stalled(&health, idle, 360000));
   }
   /* The exact disable body retires the workaround only after USBD disable. */
   nrf_usbd_common_disable();
   assert(!regs.ENABLE && !workaround_register && dma_available && stops == cycle + 1);
   assert(m_drv_state == NRFX_DRV_STATE_INITIALIZED);
   regs.LOWPOWER = 0; /* Physical disable resets its low-power state. */
  }
 }
 if (require_resume && missed) {
  puts("NEGATIVE CONTROL: modeled SOF-only resume remains suspended with 211 disabled");
  return 42;
 }
 assert(NRF_USBD_COMMON_USE_WORKAROUND_FOR_ANOMALY_211 && held == 12 && !missed);
 puts("PASS exact generated USBD 211 lifecycle: eight silicon combinations,24 enable/disable cycles, legitimate suspend; SOF-only resume is a hardware model");
 return 0;
}
'''
    (out / 'exact.c').write_text(source)
    compiler = os.environ.get('CC') or (
        '/opt/homebrew/opt/llvm/bin/clang'
        if Path('/opt/homebrew/opt/llvm/bin/clang').is_file() else 'clang')
    flags = [compiler,
             '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
             '-Wno-unused-function', '-Wno-pointer-to-int-cast', '-Wno-int-to-pointer-cast',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             '-I' + str(APP / 'include'), str(out / 'exact.c')]
    passed = False
    try:
        with (out / 'test.log').open('x') as log:
            for name, defines in (('fixed', []), ('negative', [
                    '-DNRF_USBD_COMMON_USE_WORKAROUND_FOR_ANOMALY_211=0'])):
                binary = out / name
                subprocess.run([*flags, *defines, '-o', str(binary)],
                               check=True, stdout=log, stderr=subprocess.STDOUT, timeout=30)
                result = subprocess.run([str(binary), 'require-resume'],
                                        stdout=log, stderr=subprocess.STDOUT, timeout=30)
                assert result.returncode == (0 if name == 'fixed' else 42), result.returncode
        passed = True
    finally:
        sha = lambda data: hashlib.sha256(data).hexdigest()
        (out / 'receipt.json').write_text(json.dumps({
            'passed': passed, 'hardware': False,
            'generated_common_sha256': sha(body.encode()),
            'runner_sha256': sha(Path(__file__).read_bytes()),
            'health_policy_sha256': sha((APP / 'include/usb_health.h').read_bytes()),
            'silicon_combinations': 8, 'lifecycle_cycles': 24,
            'negative_control': 'same production bodies with upstream 211 option disabled',
            'limits': 'Actual generated lifecycle bodies with fake MMIO/semaphore boundaries. '
                      'SOF-only resume is an explicit hardware model based on the upstream '
                      'anomaly contract, not a physical reproduction or field-cause attribution.'
        }, indent=2) + '\n')
    print('PASS USBD suspend lifecycle and disabled-211 negative control; no hardware')


if __name__ == '__main__':
    main()
