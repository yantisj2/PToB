#!/usr/bin/env python3
"""Offline instruction/probe-budget feasibility checks; never installs a probe.

Copyright (c) 2026 Jonathan Yantis. MIT. Byte identity is not timing accuracy.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))


def validate(snapshot,manifest,observed_reads_per_s):
    if snapshot.get('mode')!='OFFLINE_DISASSEMBLY_OF_NORMAL_BOOT_FILE_NO_INSTRUMENTATION' or snapshot.get('running_build_id_matches_image') is not True or snapshot.get('symbol_banner_mapping_verified') is not True or snapshot.get('live_kernel_memory_read') is not False:
        raise ValueError('not a verified offline normal-boot-file layout')
    if snapshot.get('kernel')!=manifest.get('kernel'):raise ValueError('kernel identity changed')
    fragments=[f for f in snapshot['fragments'] if f['symbol']=='debugfs_print_regs32']
    if len(fragments)!=1:raise ValueError('ambiguous function identity')
    f=fragments[0];data=bytes.fromhex(f['hex']);digest=hashlib.sha256(data).hexdigest()
    if digest!=f['sha256'] or digest!=manifest.get('function_sha256'):raise ValueError('compiled function identity changed')
    before=manifest.get('before_offset');after=manifest.get('after_offset')
    if any(type(x) is not int for x in (before,after)) or not 0<=before<after<=len(data)-4 or before%4 or after%4:
        raise ValueError('unaligned/outside instruction interval')
    # Explicit fixed-build sequence: ldr w3,[x3],dmb oshld, then readl's
    # dependency sequence. This validates declared bytes, not probe safety.
    expected=struct.pack('<IIIII',0xb9400063,0xd50331bf,0x2a0303e0,0xca000000,0xb5000000)
    if after-before!=len(expected) or data[before:after]!=expected or data.count(expected)!=1:
        raise ValueError('declared MMIO/ordering sequence changed or ambiguous')
    if type(observed_reads_per_s) not in (int,float) or not math.isfinite(observed_reads_per_s) or not 0<=observed_reads_per_s<=1024:
        raise ValueError('invalid observed read budget')
    hits=16*observed_reads_per_s
    return dict(kind='OFFLINE_FEASIBILITY_ONLY',kernel=snapshot['kernel'],function_sha256=digest,
        declared_before_offset=before,declared_after_offset=after,layout_bytes_validated=True,
        instruction_probing_safety_qualified=False,clock_point_qualified=False,
        first_register_isolated_in_execution=False,registers_per_show=8,probe_locations=2,
        probe_hits_per_show=16,observed_reads_per_s=observed_reads_per_s,
        projected_probe_hits_per_s=hits,hard_read_budget_per_s=1024,hard_budget_probe_hits_per_s=16384,
        record_filters_remove_global_hit_cost=False,other_generic_function_callers_cost='UNKNOWN',
        added_CPU_percent_scenarios=[dict(assumed_ns_per_hit=cost,physically_measured=False,
            observed_rate_one_core_percent=hits*cost/1e7,hard_read_budget_one_core_percent=16384*cost/1e7)
            for cost in (200,1000,10000)],
        live_authorized=False,installs_probe=False,grants_time_authority=False,chrony_eligible=False,
        absolute_uncertainty_ns=None,physical_controller_to_wire_bias_ns=None)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('snapshot',type=Path);p.add_argument('manifest',type=Path)
    p.add_argument('--observed-reads-per-second',type=float,required=True);p.add_argument('--out',type=Path,required=True)
    args=p.parse_args();out=args.out.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'artifacts'):raise ValueError('choose a new ignored artifacts/ output')
    sources={f.name:f.read_text() for f in Path(__file__).parent.glob('*.py')}
    with out.with_suffix('.implementation.json').open('x') as f:json.dump(dict(source_snapshot=sources,
        hashes={k:hashlib.sha256(v.encode()).hexdigest() for k,v in sources.items()}),f,indent=2)
    snapshot=args.snapshot.read_bytes();manifest=args.manifest.read_bytes()
    result=validate(json.loads(snapshot),json.loads(manifest),args.observed_reads_per_second)
    result['input_provenance']=dict(snapshot=str(args.snapshot.resolve()),snapshot_sha256=hashlib.sha256(snapshot).hexdigest(),
        manifest=str(args.manifest.resolve()),manifest_sha256=hashlib.sha256(manifest).hexdigest())
    with out.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
    print(out)


if __name__=='__main__':main()
