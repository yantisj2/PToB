#!/usr/bin/env python3
"""Generate passive observation hooks only against exact pinned Zephyr4.1 files."""
from pathlib import Path
import hashlib,json,sys
PIN={
 'lll_adv_sync.c':'195711e5410a569935a69106aacc9dc5ec23a00d800bc8482ed634ec796c8c26',
 'lll_sync.c':'8fc7817863f64db4bf741aae54103003af719f4b0694a20d65cbecdf6c69049d',
 'lll_scan.c':'0a75dc0e5f9a6e78767b2c8a8b65d85b220b87c5106b36014cefb80a5ef9a35b'}
def change(s,old,new,count=1):
 assert s.count(old)==count,(old,s.count(old),count)
 return s.replace(old,new)
def generate(zephyr,out):
 out.mkdir(parents=True,exist_ok=True);manifest={}
 for name,digest in PIN.items():
  source=zephyr/'subsys/bluetooth/controller/ll_sw/nordic/lll'/name
  s=source.read_text();assert hashlib.sha256(s.encode()).hexdigest()==digest,name
  if name=='lll_scan.c':
   s=change(s,'#include <stdint.h>','#include <stdint.h>\n#include "initiator_diag.h"')
   s=change(s,'\t/* Initialize scanning state */',
    '\tif (lll->conn && !lll->conn->central.cancelled) {\n'
    '\t\tr2_init_diag_target(lll->adv_addr, lll->adv_addr_type, lll->chan);\n\t}\n'
    '\t/* Initialize scanning state */')
   s=change(s,'\tbool dir_report = false;\n\n\tif (0) {',
    '\tbool dir_report = false;\n'
    '\tif (lll->conn && !lll->conn->central.cancelled) {\n'
    '\t\tr2_init_diag_count(R2_INIT_RX);\n'
    '\t\tif (pdu_adv_rx->type != PDU_ADV_TYPE_EXT_IND &&\n'
    '\t\t    isr_scan_init_check(lll, pdu_adv_rx, rl_idx)) {\n'
    '\t\t\tr2_init_diag_count(R2_INIT_MATCH);\n\t\t}\n\t}\n\n\tif (0) {')
   s=change(s,'\t\tif (!rx) {\n\t\t\treturn -ENOBUFS;\n\t\t}',
    '\t\tif (!rx) {\n'
    '\t\t\tr2_init_diag_starved(ull_pdu_rx_alloc_peek(3) ? 3 :\n'
    '\t\t\t\tull_pdu_rx_alloc_peek(2) ? 2 :\n'
    '\t\t\t\tull_pdu_rx_alloc_peek(1) ? 1 : 0,\n'
    '\t\t\t\tIS_ENABLED(CONFIG_BT_CTLR_CHAN_SEL_2) ? 4 : 3);\n'
    '\t\t\treturn -ENOBUFS;\n\t\t}')
   s=change(s,'\t\t\treturn -ETIME;',
    '\t\t\tr2_init_diag_count(R2_INIT_WINDOW);\n\t\t\treturn -ETIME;')
   s=change(s,'\t\tradio_switch_complete_and_disable();\n\n\t\t/* Acquire the connection context */',
    '\t\tr2_init_diag_count(R2_INIT_REQUEST);\n\t\tradio_switch_complete_and_disable();\n\n\t\t/* Acquire the connection context */')
   s=change(s,'static void isr_abort(void *param)\n{',
    'static void isr_abort(void *param)\n{\n'
    '\tstruct lll_scan *diag = param;\n'
    '\tif (diag->conn && !diag->conn->central.cancelled) r2_init_diag_count(R2_INIT_ABORT);')
   dest=out/name
   if not dest.is_file() or dest.read_text()!=s:dest.write_text(s)
   manifest[name]={'original':digest,'generated':hashlib.sha256(s.encode()).hexdigest()}
   continue
  s=change(s,'#include <stdint.h>','#include <stdint.h>\n#include "radio_capture.h"')
  s=change(s,'\tif (!prepare_param) {','\tif (!prepare_param) {\n\t\tr2_radio_capture_abort_site(param, R2_CAPTURE_ABORT_ACTIVE_CANCEL);')
  s=change(s,'\t\tLL_ASSERT_OVERHEAD(overhead);','\t\tr2_radio_capture_abort_site(lll, R2_CAPTURE_ABORT_PREPARE_OVERHEAD);\n\t\tLL_ASSERT_OVERHEAD(overhead);')
  if name=='lll_adv_sync.c':
   s=change(s,'\tstart_us = radio_tmr_start(1, ticks_at_start, remainder);',
    '\tr2_radio_capture_arm(lll, pdu, event_counter, data_chan_use, phy_s, true);\n'
    '\tr2_radio_capture_metadata(lll, HAL_TICKER_TICKS_TO_US(ull->ticks_slot),\n'
    '\t\tHAL_TICKER_TICKS_TO_US(MAX(ull->ticks_active_to_start, ull->ticks_prepare_to_start)), 0U, 0U);\n'
    '\tstart_us = radio_tmr_start(1, ticks_at_start, remainder);')
   s=change(s,'static void isr_done(void *param)\n{','static void isr_done(void *param)\n{\n\tr2_radio_capture_complete(param, radio_is_done(), true);')
   # A chained packet is not accepted; release capture before controller starts its next PDU.
   s=change(s,'static void isr_tx(void *param)\n{','static void isr_tx(void *param)\n{\n\tr2_radio_capture_abort_site(param, R2_CAPTURE_ABORT_TX_CHAIN);')
  else:
   # Both caller paths advance the current event counter immediately before common prepare.
   s=change(s,'\tremainder_us = radio_tmr_start(0, ticks_at_start, remainder);',
    '\tr2_radio_capture_arm(lll, node_rx->pdu, (uint16_t)(lll->event_counter - 1U), chan_idx, lll->phy, false);\n'
    '\tr2_radio_capture_metadata(lll, HAL_TICKER_TICKS_TO_US(ull->ticks_slot),\n'
    '\t\tHAL_TICKER_TICKS_TO_US(MAX(ull->ticks_active_to_start, ull->ticks_prepare_to_start)),\n'
    '\t\tlll->window_widening_event_us, lll->window_size_event_us +\n'
    '\t\t((EVENT_JITTER_US + EVENT_TICKER_RES_MARGIN_US + lll->window_widening_event_us) << 1));\n'
    '\tremainder_us = radio_tmr_start(0, ticks_at_start, remainder);')
   for function in ['isr_rx_adv_sync_estab','isr_rx_adv_sync']:
    begin=s.index('static void '+function+'(void *param)\n{');end=s.index('\nstatic ',begin+1)
    body=change(s[begin:end],'\t/* Clear radio rx status and events */\n\tlll_isr_rx_status_reset();',
     '\t/* Preserve the exact completed packet before controller resets/reuses it. */\n\tr2_radio_capture_complete(lll, trx_done, crc_ok);\n\t/* Clear radio rx status and events */\n\tlll_isr_rx_status_reset();')
    s=s[:begin]+body+s[end:]
   s=change(s,'static void isr_done(void *param)\n{','static void isr_done(void *param)\n{\n\tr2_radio_capture_abort_site(param, R2_CAPTURE_ABORT_RX_DONE);')
  dest=out/name
  if not dest.is_file() or dest.read_text()!=s:dest.write_text(s)
  manifest[name]={'original':digest,'generated':hashlib.sha256(s.encode()).hexdigest()}
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 return manifest
if __name__=='__main__':generate(Path(sys.argv[1]),Path(sys.argv[2]))
