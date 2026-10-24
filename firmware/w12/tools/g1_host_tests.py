import os
#!/usr/bin/env python3
"""Exact G1 codec/public-NimBLE-boundary checks; never opens hardware."""
import argparse,hashlib,json,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
SDK=Path(os.environ['IDF_PATH'])
CRYPTO=SDK/'components/bt/host/nimble/nimble/ext/tinycrypt'
CC=os.environ.get('CC','clang');CXX=os.environ.get('CXX',CC+'++')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def verify_sources():
    m=json.loads((APP/'contracts/r2_g1_client.json').read_text());src=APP/'main/r2/r2-g1'
    assert sha(APP/'contracts/g1_upstream_contract.md')==m['contract_sha256']
    assert sha(src/'group_vectors.json')==m['vectors_sha256']
    assert sha(src/'v14_vectors.json')==m['v14_vectors_sha256']
    for n,h in m['source_files'].items():assert sha(src/n)==h,n
    assert sha(APP/'contracts/g1_upstream_field_profile.md')==m['upstream_field_profile_sha256']
    assert m['capabilities']==m['control']==0
    assert sha(APP/m['crypto_local_fix']['file'])==m['crypto_local_fix']['sha256']
    assert sha(CRYPTO/'src/hmac.c')==m['crypto_local_fix']['sdk_original_sha256']
    return m
def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();verify_sources();a.out.mkdir(exist_ok=False,parents=True);out=a.out.resolve();src=APP/'main/r2/r2-g1';records=[]
    def run(cmd,label):
        with (out/(label+'.log')).open('w') as f:subprocess.run([str(x) for x in cmd],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=40)
        records.append(label)
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-I'+str(out),'-I'+str(src),'-I'+str(APP/'main/r2/rc7/include'),'-I'+str(CRYPTO/'include')]
    objects=[]
    for file in [src/(n+'.c') for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec')]+[APP/'main/r2/rc7/protocol/rc_dynamic.c',APP/'main/r2/rc7/src/rc_boundary.c',APP/'main/r2/rc7/src/rc_slots.c',APP/'main/g1_hmac.c']+[CRYPTO/'src'/n for n in ('sha256.c','utils.c')]:
        obj=out/(file.stem+'.o');run([CC,'-std=c11',*flags,'-c',file,'-o',obj],file.stem+'-compile');objects.append(obj)
    # Every public wire vector is independently checked with Python hashlib,
    # and actual compiled C decodes join/direct/relay and refuses changed tags.
    run([CC,'-std=c11',*flags,APP/'tests/g1_vectors_test.c',*objects,'-o',out/'vectors'],'vectors-build')
    run([out/'vectors',src/'v14_vectors.json'],'vectors')
    run(['python3',APP/'tools/g1_vectors.py',src/'v14_vectors.json'],'independent-hmac')
    # Focused current migration checks. Historical discovery/fixture expectations
    # predate the 3s startup selector and are retained outside this selected run.
    cases=['persist','cancel','blocked','late','listening-profile',
           'connect-refusal-contained','connect-refusal-other','connect-refusal-owned','connect-refusal-late',
           'terminate-yield-connect','terminate-yield-wrong-handle','terminate-yield-stale','terminate-yield-missing-disconnect',
           'direct-session-boundary','host-stall-source-expiry','coarse-hold-fences','reset-recovery',
           'reset-recovery-repeated','reset-recovery-no-sync','reset-recovery-explicit-off',
           'reset-recovery-cache-off','no-source-held','foreign-root-held','warm-control-PA','reset-recovery-late-off','reset-recovery-late-drain','reset-recovery-aux-active']
    import sys
    sys.path.insert(0,str(APP.parents[1]/'registry'))
    from registry import load,header
    (out/'ptob_registry.h').write_text(header(load(APP.parents[1]/'registry/example.json')))
    for unit,identity in [('C1',0x020000000005),('C2',0x020000000006),('C3',0x020000000007)]:
        (out/'ptob_device.h').write_text('#define PTOB_FACTORY_MAC {2,0,0,0,0,'+str(identity&255)+'}\n#define PTOB_DEVICE_LABEL \"C-'+unit[1]+'\"\n#define PTOB_LEGACY_WIRE_ID 3\n')
        (out/'r2_bench_enrollment.h').write_text('static const r2_group_enrollment provision={{{'+','.join(map(str,range(32)))+'},UINT64_C(0x123456789abcdef0),1},UINT64_C(0x%016x),"C-%s",40000};\n'%(identity,unit[1]))
        binary=out/unit
        run([CXX,'-std=c++20',*flags,'-Wno-missing-field-initializers','-DV12_R2_ENABLED=1','-DV12_R2_FIELD_CLIENT=1','-DV12_R2_CONFIG=13','-DV12_UNIT="'+unit+'"','-I'+str(APP/'tests/r2_fakes'),'-I'+str(APP/'tests/console_fakes'),'-I'+str(APP/'main'),'-I'+str(APP/'legacy/reference'),'-I'+str(out),APP/'tests/native_g1_test.cpp',*objects,'-o',binary],unit+'-build')
        for case in cases:run([binary,case],unit+'-'+case)
        if unit in ('C2','C3'):
            full=out/(unit+'-default100')
            run([CXX,'-std=c++20',*flags,'-Wno-missing-field-initializers','-DV12_R2_ENABLED=1','-DV12_R2_FIELD_CLIENT=1','-DV12_R2_CONFIG=13','-DV12_R2_G1_LISTENING_PERCENT=100','-DV12_UNIT="'+unit+'"','-I'+str(APP/'tests/r2_fakes'),'-I'+str(APP/'tests/console_fakes'),'-I'+str(APP/'main'),'-I'+str(APP/'legacy/reference'),'-I'+str(out),APP/'tests/native_g1_test.cpp',*objects,'-o',full],unit+'-default100-build')
            run([full,'listening-profile'],unit+'-default100-start-sustain-off-select50')
            for case in cases:run([full,case],unit+'-default100-'+case)
    run([CXX,'-std=c++20',*flags,'-I'+str(APP/'legacy'),'-I'+str(APP/'legacy/reference'),'-I'+str(APP/'main'),APP/'tests/r2_timestamp_test.cpp','-o',out/'timestamp-tests'],'timestamp-build')
    run([out/'timestamp-tests'],'timestamp-association-servo-quiet')
    run([CXX,'-std=c++20',*flags,'-I'+str(APP/'main'),APP/'tests/g1_quality_display_test.cpp','-o',out/'quality-display-tests'],'quality-display-build')
    run([out/'quality-display-tests'],'settled-quality-countdown-scope-downgrade')
    run([CXX,'-std=c++20',*flags,APP/'tests/g1_ui_indicators_test.cpp','-o',out/'ui-indicator-tests'],'ui-indicators-build')
    run([out/'ui-indicator-tests'],'main-long-press-PPS-RX-TX-GPS-quality-expiry')
    run(['python3',APP/'tests/device_inputs_test.py'],'explicit-GNSS-input-validation')
    run(['python3',APP/'tests/provisional_manifest_test.py'],'provisional-path-status-binding')
    run(['python3',APP/'tests/provisional_generated_config_test.py','--out',out/'generated-config','--objects',out],'actual-generated-profile-to-production-emitted-time')
    for enabled in (0,1):
        (out/'ptob_device.h').write_text('#define PTOB_FACTORY_MAC {2,0,0,0,0,5}\n#define PTOB_DEVICE_LABEL "Example"\n#define PTOB_LEGACY_WIRE_ID 2\n#define PTOB_GNSS_INDICATORS '+str(enabled)+'\n')
        binary=out/('device-profile-'+str(enabled))
        run([CXX,'-std=c++20',*flags,'-DW12_GNSS_VERIFIED_INPUTS='+str(enabled),'-I'+str(APP/'legacy'),APP/'tests/device_profile_test.cpp','-o',binary],'device-profile-'+str(enabled)+'-build')
        run([binary],'device-profile-'+str(enabled)+'-factory-capability')
    run([CXX,'-std=c++20',*flags,'-DV12_R2_ENABLED=1','-DV12_R2_CONFIG=13','-Wno-unused-parameter','-Wno-sign-compare','-I'+str(APP/'legacy'),'-I'+str(APP/'legacy/reference'),'-I'+str(APP/'main'),APP/'tools/preview_r2_dashboard.cpp','-o',out/'dashboard-preview'],'dashboard-framebuffer-build')
    run([out/'dashboard-preview',out],'dashboard-causal-RMS-estimated-hold-layout')
    run([CXX,'-std=c++20',*flags,'-I'+str(APP/'legacy'),'-I'+str(APP/'legacy/reference'),'-I'+str(APP/'main'),APP/'tests/g1_corrected_clock_test.cpp',*objects,'-o',out/'corrected-clock-tests'],'corrected-clock-build')
    run([CXX,'-std=c++20',*flags,'-DPTOB_W12_FINE_CLOCK=1','-I'+str(APP/'legacy'),'-I'+str(APP/'legacy/reference'),'-I'+str(APP/'main'),APP/'tests/g1_corrected_clock_test.cpp',*objects,'-o',out/'fine-corrected-clock-tests'],'fine-corrected-clock-build')
    run([out/'fine-corrected-clock-tests'],'fine-corrected-clock')
    run([out/'corrected-clock-tests'],'functional-clock-frequency-scope-hold')
    for fine in (False,True):
        binary=out/('w12-phase-fine' if fine else 'w12-phase-legacy')
        run([CXX,'-std=c++20',*flags,'-DPTOB_W12_SYNC_WATCHDOG=1',*(['-DPTOB_W12_FINE_CLOCK=1'] if fine else []),'-I'+str(APP/'legacy'),'-I'+str(APP/'legacy/reference'),'-I'+str(APP/'main'),APP/'tests/w12_phase_offsets_test.cpp',*objects,'-o',binary],binary.name+'-build')
        run([binary],binary.name+'-raw-fit-delivery-relay')
    run([CXX,'-std=c++20',*flags,'-I'+str(APP/'main'),APP/'tests/g1_direct_session_test.cpp',*objects,'-o',out/'direct-session-tests'],'direct-session-build')
    run([out/'direct-session-tests'],'direct-session-matched-maps-renewal-expiry-refusals')
    run([CXX,'-std=c++20',*flags,'-Wno-sign-compare','-I'+str(APP/'legacy'),'-I'+str(APP/'legacy/reference'),'-I'+str(APP/'main'),APP/'tests/g1_diagnostic_test.cpp','-o',out/'diagnostic-tests'],'diagnostic-build')
    run([out/'diagnostic-tests'],'diagnostic-width-and-refusal-counters')
    run(['python3',APP/'tests/g1_diagnostic_analyze_test.py'],'diagnostic-causal-analysis')
    run([CXX,'-std=c++20',*flags,'-I'+str(APP/'tests/console_fakes'),APP/'tests/g1_hci_trace_test.cpp','-o',out/'hci-trace-tests'],'hci-trace-build')
    run([out/'hci-trace-tests'],'hci-exact-callthrough-ACK-stall-loss')
    run([CXX,'-std=c++20',*flags,'-Wno-sign-compare','-I'+str(APP/'tests/console_fakes'),APP/'tests/bounded_log_test.cpp','-o',out/'bounded-log-tests'],'bounded-log-build')
    run([out/'bounded-log-tests'],'bounded-log-expiry-control-byte-evidence')
    run([CXX,'-std=c++20',*flags,'-I'+str(APP/'legacy'),APP/'tests/l76k_aid_test.cpp','-o',out/'l76k-aid-tests'],'l76k-aid-build')
    run([out/'l76k-aid-tests'],'l76k-aid-time-uncertainty-refusals-ack')
    run(['python3',APP/'tests/main_stack_budget_test.py'],'actual-main-stack-budget-regression')
    run(['python3',APP/'tests/gnss_fast_map_diagnostic_test.py'],'GNSS-fast-map-build-profile-disabled-path-and-driver-refusals')
    run(['python3',APP/'tools/status_budget_test.py'],'actual-status-capacity')
    run(['python3',APP/'tools/status_budget_test.py','--gps-shadow'],'C3-shadow-status-capacity')
    run(['python3',APP/'tools/status_budget_test.py','--rc7-estimate'],'RC7-estimate-status-capacity')
    run([CXX,'-std=c++20',*flags,'-I'+str(APP/'legacy'),'-I'+str(APP/'legacy/reference'),'-I'+str(APP/'main'),APP/'tests/g1_gps_shadow_test.cpp','-o',out/'gps-shadow-tests'],'C3-shadow-build')
    run([out/'gps-shadow-tests'],'C3-causal-PPS-history-expiry-fix-bias-retained-tail')
    run([CXX,'-std=c++20',*flags,'-I'+str(APP/'legacy'),'-I'+str(APP/'legacy/reference'),'-I'+str(APP/'main'),APP/'tests/g1_pps_diagnostic_test.cpp','-o',out/'g1-pps-diagnostic-tests'],'production-G1-PPS-diagnostic-build')
    run([out/'g1-pps-diagnostic-tests'],'production-G1-PPS-causal-scope-expiry-reference-separation')
    run(['python3',APP/'tests/g1_pps_diagnostic_render_test.py'],'production-G1-PPS-profile-and-log-capacity')
    run(['python3',APP/'tools/gps_shadow_tests.py','--out',out/'gps-shadow-prediction'],'causal-GNSS-shadow-clock-drift-and-legacy-controls')
    run([CXX,'-std=c++20',*flags,APP/'tests/c3_lora_off_test.cpp','-o',out/'C3-lora-off-tests'],'C3-LoRa-forced-off-build')
    run([out/'C3-lora-off-tests'],'C3-LoRa-no-task-no-GPIO-no-admission')
    run(['python3',APP/'tests/g1_shared_reset_test.py'],'shared-reset-owner-participation')
    for name in ('g1_rc_continuity_test','g1_startup_fixture','g1_parent_fixture','g1_lock_fallback_test','g1_gm_hold_test','rc7_relay_estimate_test','rc7_provisional_path_test','rc7_tx_reference_test','lifecycle_test','update_gate_test','fault_watchdog_test'):
        run([CXX,'-std=c++20',*flags,'-I'+str(APP/'main'),APP/'tests'/(name+'.cpp'),*objects,'-o',out/name],name+'-build')
        run([out/name],name+'-run')
    run(['python3',APP/'tools/precision_priority_tests.py','--out',out/'precision-priority','--objects',out],'precision-before-control-and-radio-reservation')
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=APP,text=True).strip()
    (out/'receipt.json').write_text(json.dumps({'schema':1,'status':'PASSED','scope':'G1_EXACT_CODECS_AND_NATIVE_PUBLIC_BOUNDARY','source_commit':commit,'contract_sha256':sha(APP/'contracts/r2_g1_client.json'),'tests':records,'physical_RF':'NOT_RUN','precision':'UNAVAILABLE'},indent=2)+'\n')
    print('PASS exact G1 vectors and synthetic device/production native client adapters')
if __name__=='__main__':main()
