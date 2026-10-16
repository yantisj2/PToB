#!/usr/bin/env python3
"""Regenerate hypothetical model tables; no raw capture or hardware required."""
import csv, json, math
from pathlib import Path
OUT = Path(__file__).resolve().parent
RMS_TWO_LINK_NS = 62.503871
s = RMS_TWO_LINK_NS / math.sqrt(2)
def write(name, rows):
    with (OUT / name).open('w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
write('hop-sensitivity.csv', [dict(ble_links=h, rc_intermediaries=h-1,
    independent_rms_ns=s*math.sqrt(h), fully_correlated_rms_ns=s*h,
    independent_plus_hypothetical_100ns_bias_per_link_rms_ns=math.hypot(s*math.sqrt(h),100*h),
    classification='HYPOTHETICAL_NOT_A_SUPPORTED_HOP_COUNT') for h in (1,2,3,4,8,16)])
write('availability-sensitivity.csv', [dict(ble_links=h, assumed_link_usability=p,
    hypothetical_path_usability=p**h,classification='INDEPENDENCE_ASSUMED_NOT_MEASURED')
    for p in (.99,.999) for h in (1,2,3,4,8,10,16,20)])
write('holdover-sensitivity.csv', [dict(residual_frequency_ppm=ppm,
    remaining_error_budget_ns=500, hypothetical_hold_seconds=500e-9/(ppm*1e-6),
    classification='NO_DRIFT_OR_OTHER_ERROR_ASSUMED_NOT_PERMISSION') for ppm in (1,.1,.01)])
summary=dict(schema=1,model_status='ILLUSTRATIVE_ONLY',anchor_two_link_rms_ns=RMS_TWO_LINK_NS,
    effective_increment_ns=s,not_a_measured_per_link_parameter=True,
    hypothetical_500ns_RMS_hop_ceiling_independent=math.floor((500/s)**2),
    hypothetical_500ns_RMS_hop_ceiling_correlated=math.floor(500/s),
    record_serialization_bps=190*8*16,raw_1Mbps_fraction=190*8*16/1e6,
    hypothetical_1000_child_300s_renewals_per_second=1000/300,
    hypothetical_eight_full_period_waits_seconds=8/16)
(OUT/'model-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
