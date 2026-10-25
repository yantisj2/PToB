#!/usr/bin/env python3
"""Add bounded GM7 contexts and passive capture to already pinned generators.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import hashlib,json,sys
from pathlib import Path
def replace(s,a,b,count=1):
 assert s.count(a)==count,(a,s.count(a),count)
 return s.replace(a,b)
def generate(ull,scan,aux,sync,out):
 out.mkdir(parents=True,exist_ok=True);manifest={}
 pins=json.loads((Path(__file__).resolve().parents[1]/"cmake/gm7-controller.json").read_text())["inputs"]
 for path in [ull,scan,aux,sync]:
  s=path.read_text();before=hashlib.sha256(s.encode()).hexdigest()
  assert before in pins[path.name],("Unpinned GM7 controller input",path.name,before)
  if path.name=='ull.c':
   s=replace(s,'#define USER_TICKER_NODES         0','#define USER_TICKER_NODES         3 /* dedicated GM7 complete events */')
   s=replace(s,'#define TICKER_USER_THREAD_VENDOR_OPS 0','#define TICKER_USER_THREAD_VENDOR_OPS 3 /* bounded GM7 submissions/drain */')
   s=replace(s,'#define EVENT_DONE_MAX 5','#define EVENT_DONE_MAX 7 /* preserve stock5 plus GM7 prepare overlap */')
  elif path.name=='ull_adv_sync.c':
   s=replace(s,'#include <zephyr/kernel.h>','#include <zephyr/kernel.h>\n#include "gm7_controller.h"')
   s=replace(s,'\tinterval_us = (uint32_t)sync->interval * PERIODIC_INT_UNIT_US;',
     '\tinterval_us = (uint32_t)sync->interval * PERIODIC_INT_UNIT_US;\n'
     '\tif (!r2_gm7_precision_anchor(ull_adv_handle_get(adv), &sync->ull, &ticks_anchor, interval_us)) return TICKER_STATUS_FAILURE;')
  elif path.name=='lll_scan.c':
   s=replace(s,'#include <stdint.h>','#include <stdint.h>\n#include "gm7_controller.h"')
   needle='lll_chan_set(37 + lll->chan);'
   # A live initiator never changes its channel policy. Passive GM discovery
   # pins37 throughout the enabled S1 GM owner, independent of phase.
   # Ordinary RC and live initiators keep their original channel policy.
   s=replace(s,needle,'lll_chan_set((!lll->conn && r2_gm7_controller_scan37()) ? 37 : 37 + lll->chan);',2)
  else:
   s=replace(s,'#include <stdint.h>','#include <stdint.h>\n#include "gm7_controller.h"')
   # Both immediate LLL AUX follow and ULL-prepared AUX follow keep capture
   # armed through END. Physical RX owns PACKETPTR; queued aborts cannot
   # disarm another current event by a reused controller-context pointer.
   assert s.count('\tradio_pkt_rx_set(node_rx->pdu);')==2
   s=s.replace('\tradio_pkt_rx_set(node_rx->pdu);','\tradio_pkt_rx_set(node_rx->pdu);\n'
    '\tif (phy_aux == 2) r2_gm7_capture_receive_arm(node_rx->pdu);',1)
   # prepare_cb has lll_aux->phy rather than the fast path's phy_aux variable.
   marker='\tradio_pkt_rx_set(node_rx->pdu);'
   begin=s.index('static int prepare_cb(struct lll_prepare_param *p)\n{')
   i=s.index(marker,begin)+len(marker)
   s=s[:i]+'\n\tif (lll_aux->phy == 2) r2_gm7_capture_receive_arm(node_rx->pdu);'+s[i:]
   begin=s.index('static void isr_rx(struct lll_scan *lll, struct lll_scan_aux *lll_aux,\n\t\t   uint8_t phy_aux)\n{')
   i=s.index('\t/* Clear radio rx status and events */',begin)
   s=s[:i]+'\tr2_gm7_capture_done((void *)(uintptr_t)NRF_RADIO->PACKETPTR, trx_done, crc_ok,\n\t\t(void *)(uintptr_t)NRF_RADIO->PACKETPTR);\n'+s[i:]
   s=replace(s,'static void isr_done(void *param)\n{','static void isr_done(void *param)\n{\n\tr2_gm7_capture_abort((void *)(uintptr_t)NRF_RADIO->PACKETPTR);')
   # Abort cleanup is common to pending and current events: only current
   # radio cleanup has an armed PACKETPTR; the done hook retires it.
  dest=out/path.name;dest.write_text(s)
  manifest[path.name]={'input_sha256':before,'output_sha256':hashlib.sha256(s.encode()).hexdigest()}
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 return manifest
if __name__=='__main__':generate(*map(Path,sys.argv[1:]))
