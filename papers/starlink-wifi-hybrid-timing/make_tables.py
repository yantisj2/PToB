#!/usr/bin/env python3
"""Render numerical TeX selections from the selected-data analysis.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
from pathlib import Path
import json

def products(folder):
 r=json.loads((folder/'results.json').read_text());o=json.loads((folder/'observations.json').read_text());s=json.loads((folder/'sealed-model.json').read_text())
 q=o['capture_audit'];c=r['counts'];v=r['heldout'];fmt=lambda x:f'{x:.3f}'
 extras=[];control=None;longspan=None
 if (folder/'controls/same-boot-01/results.json').exists():
  control=json.loads((folder/'controls/same-boot-01/results.json').read_text());longspan=json.loads((folder/'controls/same-boot-longspan/results.json').read_text())
  extras=[('ControlRMS',fmt(control['heldout']['rms_us'])),('LongRMS',fmt(longspan['heldout']['rms_us']))]
 macros='\n'.join('\\newcommand{\\'+name+'}{'+value+'}' for name,value in [
  ('ResultRMS',fmt(v['rms_us'])),('ResultMax',fmt(v['max_abs_us'])),('ResultN',str(v['n']))]+extras)+'\n'
 summary=(f"The first SL024 window retained {q['PPS_rows']} PPS: all qualified and independently replayed, with zero mismatches, gaps or diagnostic drops. "
 f"All {q['NTP_accepted']} NTP exchanges were accepted; {c['training_exchange_n']} training exchanges supplied {c['complete_bursts']} complete burst minima. "
 f"The omitted partial burst had {c['omitted_burst_lengths'][0]} exchanges. "
 f"Its GNSS-measured fixed base was {s['base_us']:.3f}\\us. "
 f"Uncorrected latest-reply/MIN\\_RTT8 phase RMS was {r['all_qualified_PPS']['raw']['rms_us']:.3f}/{r['all_qualified_PPS']['MIN_RTT8']['rms_us']:.3f}\\us. "
 f"A descriptive MIN\\_RTT8 comparator uses its own {r['paired_future_MIN_RTT8_comparator']['base_us']:.3f}\\us\\ last-16-training-PPS base; its paired future RMS is {r['paired_future_MIN_RTT8_comparator']['fixed_base_corrected']['rms_us']:.3f}\\us.\n")
 header='\\begin{center}\\small\n\\begin{tabular}{lrrrrrr}\\toprule\nAge ceiling & $N$ & Mean & Std & RMS & Abs. P99 & Max. \\\\\n & & \\multicolumn{5}{c}{microseconds} \\\\ \\midrule\n'
 body=[]
 for h in ('10','30','60','120','300'):
  d=r['horizons'][h]
  values=[fmt(d[k]) for k in ('mean_us','centered_std_us','rms_us','abs_p99_us','max_abs_us')] if d['n'] else ['--']*5
  body.append(h+' s & '+str(d['n'])+' & '+' & '.join(values)+r' \\')
 body.append('All future & '+str(v['n'])+' & '+' & '.join(fmt(v[k]) for k in ('mean_us','centered_std_us','rms_us','abs_p99_us','max_abs_us'))+r' \\')
 if control:
  for label,d in (('Repeat, all',control['heldout']),('Long span, all',longspan['heldout'])):
   body.append(label+' & '+str(d['n'])+' & '+' & '.join(fmt(d[k]) for k in ('mean_us','centered_std_us','rms_us','abs_p99_us','max_abs_us'))+r' \\')
 table=header+'\n'.join(body)+'\n\\bottomrule\\end{tabular}\\end{center}\n'
 j=r['observed_source_plus_transfer_residual'];a=j['all_training_exchanges'];b=j['training_burst_minima'];u=r['prior_innovations']
 jitter=(f"Accepted network delays ranged from {r['network_delay_us']['min']} to {r['network_delay_us']['max']}\\us "
 f"(median {r['network_delay_us']['median']:.1f}\\us). "
 f"For {a['n']} training exchanges, residual centered scatter around the sealed NTP line was {a['centered_std_us']:.3f}\\us "
 f"with {a['max_abs_us']:.3f}\\us\\ maximum absolute residual. "
 f"The {b['n']} burst minima retained {b['centered_std_us']:.3f}\\us\\ centered scatter "
 f"({b['rms_us']:.3f}\\us\\ RMS; {b['max_abs_us']:.3f}\\us\\ maximum). "
 f"The {u['n']} prior-fit innovations had {u.get('rms_us',0):.3f}\\us\\ RMS. "
 f"The real gate rejected {c['gate_rejections']} candidates and declared {c['source_shift_resets']} source/path-change resets. "
 f"Leaving out one admitted burst at a time changes the weighted rate from {r['leave_one_burst_out_rate_sensitivity']['min_ppm']:.6f} to {r['leave_one_burst_out_rate_sensitivity']['max_ppm']:.6f} ppm. "
 "This is a training sensitivity diagnostic, not an uncertainty interval or model reselection. "
 "The residuals include source, local-clock, transfer and timestamp effects; no component-specific jitter bound follows.\n")
 comparison=''
 if control:
  historical=json.loads((folder/'historical-results.json').read_text());old=historical['preceding_repeat_field04'];d=old['GNSS_rate_diagnostic_only']
  comparison='\\begin{center}\\small\n\\begin{tabular}{lrrrr}\\toprule\nRun & NTP ppm & GNSS train ppm & GNSS future ppm & Future RMS ($\\mu$s) \\\\ \\midrule\n'
  comparison+=f"SL023 prior & {old['rate_ppm']:.6f} & {d['train']['GNSS_witness_relative_rate_ppm']:.6f} & {d['future']['GNSS_witness_relative_rate_ppm']:.6f} & {old['future300']['rms_us']:.3f}"+r' \\'+'\n'
  for label,a in (('SL024 first',r),('SL024 repeat',control),('Long span',longspan)):
   comparison+=f"{label} & {a['model']['ppm']:.6f} & {a['GNSS_rate_diagnostic_only']['training']['rate_ppm']:.6f} & {a['GNSS_rate_diagnostic_only']['future']['rate_ppm']:.6f} & {a['heldout']['rms_us']:.3f}"+r' \\'+'\n'
  comparison+='\\bottomrule\\end{tabular}\\end{center}\n{\\small GNSS rate fits are post-hoc diagnostics, never NTP frequency training. SL023 failed its clean-history gate. The last two rows share future PPS witnesses.}\n'
  summary+=f"\\par The unchanged-image/boot repeat retained {control['counts']['PPS_total']} qualified PPS with full replay. Its primary and separate {longspan['counts']['admitted_minima']}-minimum longer-span model share the same future PPS; all errors are retained. "

  summary+=f"Within true age below 60 seconds, repeat primary RMS was {control['horizons']['60']['rms_us']:.3f}\\us; the longer fit gave {longspan['horizons']['60']['rms_us']:.3f}\\us over {longspan['horizons']['60']['n']} PPS.\n"
 clean=[('S1',r),('S2',control)] if control else [('S1',r)]
 fixed=folder/'controls/same-boot-02/results.json'
 if fixed.exists():clean.append(('S3',json.loads(fixed.read_text())))
 all_models=clean+([('S2-L',longspan)] if longspan else [])
 comparison='\\begin{center}\\small\n\\begin{tabular}{lrrrr}\\toprule\nRun & NTP ppm & GNSS train ppm & GNSS future ppm & Future RMS ($\\mu$s) \\\\ \\midrule\n'
 for label,a in all_models:
  comparison+=f"{label} & {a['model']['ppm']:.6f} & {a['GNSS_rate_diagnostic_only']['training']['rate_ppm']:.6f} & {a['GNSS_rate_diagnostic_only']['future']['rate_ppm']:.6f} & {a['heldout']['rms_us']:.3f}"+r' \\'+'\n'
 comparison+='\\bottomrule\\end{tabular}\\end{center}\n{\\small GNSS rates are post-hoc diagnostics, never NTP frequency training. S2-L shares S2 future PPS witnesses.}\n'
 accuracy='\\begin{center}\\small\n\\begin{tabular}{lrrrrrrr}\\toprule\nModel & $N$ & Mean & Std & RMS & Max. & Drift & Detrended \\\\\n & & \\multicolumn{4}{c}{microseconds} & $\\mu$s/s & RMS ($\\mu$s) \\\\ \\midrule\n'
 short='\\begin{center}\\small\n\\begin{tabular}{lrrrrr}\\toprule\nModel & $N$ & Min. age (s) & Max. age (s) & RMS ($\\mu$s) & Max. ($\\mu$s) \\\\ \\midrule\n'
 source='\\begin{center}\\small\n\\begin{tabular}{lrrrrrr}\\toprule\nRun & NTP & Train & Minima & All $\\sigma$ & Minima $\\sigma$ & Gate rejections \\\\\n & count & count & count & $\\mu$s & $\\mu$s & count \\\\ \\midrule\n'
 for label,a in all_models:
  v=a['heldout'];trend=a['future_error_trend']
  accuracy+=label+' & '+str(v['n'])+' & '+' & '.join(fmt(v[k]) for k in ('mean_us','centered_std_us','rms_us','max_abs_us'))+f" & {trend['slope_us_per_s']:+.6f} & {trend['descriptive_detrended_residual']['rms_us']:.3f}"+r' \\'+'\n'
  h=a['horizons']['60']
  short+=f"{label} & {h['n']} & {a['heldout_age_range_s'][0]:.3f} & {h['maximum_forecast_age_s']:.3f} & {h['rms_us']:.3f} & {h['max_abs_us']:.3f}"+r' \\'+'\n'
  if label!='S2-L':
   c=a['counts'];j=a['observed_source_plus_transfer_residual']
   source+=f"{label} & {a['network_delay_us']['n']} & {c['training_exchange_n']} & {c['admitted_minima']} & {j['all_training_exchanges']['centered_std_us']:.3f} & {j['training_burst_minima']['centered_std_us']:.3f} & {c['gate_rejections']}"+r' \\'+'\n'
 ending='\\bottomrule\\end{tabular}\\end{center}\n'
 accuracy+=ending+'{\\small S1/S2/S3 are the three clean primary forecasts; S2-L uses extra historical NTP and shares S2 witnesses. Std includes drift. Detrended RMS is descriptive only, with no correction to held-out accuracy.}\n'
 short+=ending+'{\\small Only witnessed validation PPS whose true age since the final admitted training reply is below 60 seconds are included; these are partial short-age segments, not complete 60-second validation windows.}\n'
 source+=ending+'{\\small Residuals are relative to each sealed NTP line and combine source, transfer, client timestamp and local-clock effects; no component-specific jitter bound follows.}\n'
 return {'results.tex':macros,'fresh-summary.tex':summary,'horizon-table.tex':table,'jitter-summary.tex':jitter,'comparison-table.tex':comparison,
         'clean-accuracy-table.tex':accuracy,'short-age-table.tex':short,'clean-jitter-table.tex':source}

if __name__=='__main__':
 folder=Path(__file__).resolve().parent
 for name,text in products(folder).items():(folder/name).write_text('% Generated by make_tables.py; CC BY 4.0 Jonathan Yantis.\n'+text)
