import os
#!/usr/bin/env python3
"""Focused synthetic RC7 permission/model/native checks. Never uses Git or hardware."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

APP = Path(__file__).resolve().parents[1]
SDK_DEFAULT = Path(os.environ['IDF_PATH'])
NATIVE_CASES = ('rc7-lineage-root', 'rc7-lineage-boot', 'rc7-lineage-epoch', 'rc7-lineage-root-train', 'rc7-lineage-parent-train', 'rc7-lineage-pin', 'rc7-lineage-renewal',
                'direct-session-native', 'autonomous-direct', 'typed-direct', 'direct-to-rc7',
                'changed-full-path', 'response-path-changed', 'permission-native',
                'loss-recovery-preserved', 'retained-new-coarse-root', 'rc7-foreign-root-hint', 'retained-expiry', 'retained-clock-step', 'mapped-read-failure', 'mapped-finalize-failure', 'mapped-deliver-failure')
SERVER_CASES = ('incoming-capacity', 'duplicate-active', 'tx-observation', 'provisional-output', 'warm-budget-publisher', 'lock-startup',
                'hold-transient', 'hold-expiry', 'hold-flapping', 'hold-management',
                'hold-lease-expiry', 'hold-scope', 'hold-stop', 'hold-drain-deadline', 'hold-queue-loss',
                'slot-startup-t4', 'slot-startup-conflict', 'slot-startup-weak', 'slot-startup-foreign',
                'demand-idle', 'demand-bad-tag', 'demand-activate', 'demand-expiry', 'demand-multiple',
                'demand-renewal', 'demand-restart', 'demand-start-expiry', 'demand-start-lock-loss', 'demand-queue-expiry',
                'demand-stop-failure', 'demand-hook-failure', 'demand-stop-deadline', 'demand-late-expiry',
                'pending', 'ready', 'small-mtu', 'offset', 'bad-tag', 'upstream-yield', 'expired-source',
                'late-drain', 'late-connect', 'late-connect-past-deadline', 'capture-budget',
                'queue-local-expiry', 'queue-inherited-expiry', 'queue-envelope-expiry', 'queue-invariant',
                'context-withdraw', 'queue-trace', 'overlap-fast', 'overlap-enotconn', 'overlap-slow',
                'overlap-deadline', 'overlap-late-disconnect', 'overlap-duplicate', 'overlap-alias',
                'overlap-invalid-descriptor', 'overlap-wrong-role',
                'overlap-client-stopped', 'overlap-client-stopping', 'overlap-stop-pending')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Native fixture source seam changed: ' + old[:100])
    return text.replace(old, new)


def native_fixture(out):
    # Follow the existing owner fixture include pattern. Production native
    # code is copied with only its device-binding include replaced; all radio
    # APIs are synthetic callbacks from the existing tracked native test.
    native = (APP / 'main/native_g1.cpp').read_text()
    native = replace_once(native, '#include "native_device_binding.hpp"',
                          '#include "rc7_permission_device_fixture.hpp"')
    (out / 'native_g1_fixture.cpp').write_text(native)
    (out / 'rc7_permission_device_fixture.hpp').write_text('''#pragma once
namespace v12 {
struct NativeDeviceBinding {uint8_t factory[6];const char* label;uint8_t wire_id;};
inline const NativeDeviceBinding* native_device_binding(){
 static constexpr NativeDeviceBinding device{{2,3,4,5,6,7},"Fixture",3};return &device;
}
}
''')
    (out / 'r2_bench_enrollment.h').write_text('''static const r2_group_enrollment provision={
 {{1,2,3},17,9},UINT64_C(0x020304050607),"Fixture",40000};
''')
    s = (APP / 'tests/native_g1_test.cpp').read_text().split('int main(')[0]
    s = s.replace(',marker_bad=false', '')
    # Unused legacy discovery helper contains its own old identity fixture;
    # it is deliberately omitted from this fully synthetic regression.
    a = s.index('void discovery(')
    b = s.index('void fresh_direct(', a)
    s = s[:a] + s[b:]
    s = replace_once(s, '#include "../main/native_g1.cpp"', '''#define V12_RC7_CLOCK_TEST_INSPECT 1
#define V12_R2_RX_SHADOW 1
using StackType_t=uint32_t;
inline unsigned uxTaskGetStackHighWaterMark(void*){return 2048;}
unsigned fixture_clock_reads=0,fixture_clock_fail=0,fixture_clock_backwards=0;
extern "C" bool clock_reference_read(uint64_t* t){
 ++fixture_clock_reads;if(fixture_clock_fail&&fixture_clock_reads==fixture_clock_fail)return false;
 *t=(fixture_clock_backwards&&fixture_clock_reads==fixture_clock_backwards)?1:fake_now*20;return true;
}
#include "native_g1_fixture.cpp"
namespace v12::periodic_rx {
uint32_t capture_epoch=0;bool installed=false,armed=false;Stamp fixture_stamp{};
RfReference rf_reference(){return {};}
bool start(){installed=true;return true;}bool stop(){installed=armed=false;return true;}
void poll(){}uint32_t scope(uint16_t,bool acquiring){armed=acquiring;return ++capture_epoch;}
bool adopt(uint16_t,uint32_t e){armed=e==capture_epoch;return armed;}
Stats status(){Stats s;s.installed=installed;s.armed=armed;s.epoch=capture_epoch;return s;}
bool match(uint16_t,const uint8_t*,size_t,uint64_t,Stamp& s){s=fixture_stamp;return armed&&s.actual;}
}
rc7_control fixture_cap{},fixture_response{},fixture_request{};
bool fixture_response_path_change=false;void* fixture_sync_arg=nullptr;
''')
    s = replace_once(s, 'int ble_gattc_read(uint16_t,uint16_t,int(*)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void*){return BLE_HS_ENOTCONN;}\n', '')
    s = replace_once(s, 'int ble_gattc_write_flat(uint16_t,uint16_t,const void*,uint16_t,int(*)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void*){return BLE_HS_ENOTCONN;}\n', '')
    a = s.index('int ble_gattc_read_by_uuid(')
    b = s.index('\nint ble_gattc_disc_svc_by_uuid', a)
    s = s[:a] + '''int ble_gattc_read_by_uuid(uint16_t h,uint16_t,uint16_t,const ble_uuid_t* uuid,int(*cb)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void* arg){
 assert(!ble_uuid_cmp(uuid,&v12::data_uuid.u));os_mbuf b;b.bytes={'R','2','G','1',1};
 ble_gatt_attr a;a.om=&b;ble_gatt_error e;cb(h,&e,&a,arg);return 0;
}
int ble_gattc_read(uint16_t h,uint16_t,int(*cb)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void* arg){
 os_mbuf b;b.bytes.resize(RC_DYNAMIC_CONTROL_BYTES);const auto& c=v12::peer.stage==2?fixture_cap:fixture_response;
 assert(!rc7_control_encode(b.bytes.data(),&c,&v12::provision.group));ble_gatt_attr a;a.om=&b;ble_gatt_error e;cb(h,&e,&a,arg);return 0;
}
int ble_gattc_write_flat(uint16_t h,uint16_t,const void* wire,uint16_t n,int(*cb)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void* arg){
 assert(n==RC_DYNAMIC_CONTROL_BYTES);assert(!rc7_control_decode(&fixture_request,static_cast<const uint8_t*>(wire),n,&v12::provision.group));
 fixture_response={};auto& r=fixture_response;const auto& q=fixture_request;r.type=RC7_RESPONSE;
 r.sender=q.target;r.target=q.sender;r.sender_boot=q.target_boot;r.target_boot=q.sender_boot;
 r.challenge=q.challenge;r.nonce=q.nonce;r.sequence=q.sequence;r.lease_us=q.lease_us;r.next_challenge=q.challenge+1;
 r.path=fixture_cap.path;if(fixture_response_path_change)++r.path.node[1].train;r.ready=true;
 r.publication_floor=10;r.root_lo_ns=fake_now*1000-1000000;r.root_hi_ns=fake_now*1000+1000000;r.source_expires_ns=fake_now*1000+1500000000;
 fixture_cap.next_challenge=r.next_challenge;ble_gatt_error e;cb(h,&e,nullptr,arg);return 0;
}''' + s[b:]
    s = replace_once(s, 'c.uuid.u128=v12::data_uuid;', 'c.uuid.u128=v12::peer.relay?v12::rc_data_uuid:v12::data_uuid;c.properties=v12::peer.relay?0x0a:0x04;')
    s = replace_once(s, 'int ble_gap_periodic_adv_sync_create(const ble_addr_t*,uint8_t sid,const ble_gap_periodic_sync_params*,int(*)(ble_gap_event*,void*),void*){assert(sid==12);return sync_pool_held?BLE_HS_ENOMEM:0;}', 'int ble_gap_periodic_adv_sync_create(const ble_addr_t*,uint8_t sid,const ble_gap_periodic_sync_params*,int(*)(ble_gap_event*,void*),void* arg){assert(sid==12);fixture_sync_arg=arg;return sync_pool_held?BLE_HS_ENOMEM:0;}')
    s = s.replace('v12::gap(&e,nullptr);', 'v12::gap(&e,fixture_sync_arg);')
    s = replace_once(s, 'challenge.receiver_boot=22', 'challenge.receiver_boot=v12::boot_id')
    s = replace_once(s, 'r2_group_put(b+11,1,4)', 'r2_group_put(b+11,v12::provision.group.epoch,4)')
    s += '''
void rc_discovery(){
 v12::Discovery d{};d.props=1;d.sid=13;d.rssi=-30;d.at=fake_now;d.address=v12::canonical(fixture_cap.sender);d.length=23+RC_DYNAMIC_CAPABILITY_BYTES;
 d.bytes[0]=2;d.bytes[1]=1;d.bytes[2]=6;d.bytes[3]=17;d.bytes[4]=7;std::memcpy(d.bytes+5,v12::rc_service.value,16);
 d.bytes[21]=RC_DYNAMIC_CAPABILITY_BYTES+1;d.bytes[22]=255;assert(!rc7_capability_encode(d.bytes+23,&fixture_cap,&v12::provision.group));
 ble_gap_event e{};e.type=BLE_GAP_EVENT_EXT_DISC;e.ext_disc.addr=d.address;e.ext_disc.rssi=d.rssi;e.ext_disc.sid=13;e.ext_disc.props=1;e.ext_disc.data_status=BLE_HCI_ADV_DATA_STATUS_COMPLETE;e.ext_disc.length_data=d.length;e.ext_disc.data=d.bytes;
 v12::gap(&e,nullptr);step(21);assert(v12::peer.connecting);e={};e.type=BLE_GAP_EVENT_CONNECT;v12::gap(&e,conn_arg);step(18);
}
#define V12_RC7_PERMISSION_NATIVE 1
#include "rc7_permission_test.cpp"
'''
    (out / 'native.cpp').write_text(s)


def server_fixture(out):
    s = (out / 'native.cpp').read_text().split('void rc_discovery(){')[0]
    # Only this radio-owner fixture controls the parent-lock input. Clock
    # fitting/quality thresholds are covered by the production clock tests.
    s = replace_once(s, '#include "native_g1_fixture.cpp"',
                     'bool fixture_parent_locked=true;\n#include "native_g1_fixture.cpp"')
    relay = (APP / 'main/rc7_relay_native.inc').read_text()
    relay = replace_once(relay, 'bool relay_parent_locked(uint64_t now){return lock_protected(now);}',
                         'bool relay_parent_locked(uint64_t){return ::fixture_parent_locked;}')
    (out / 'rc7_relay_native.inc').write_text(relay)
    s = replace_once(s, 'int ble_gap_conn_find(uint16_t,ble_gap_conn_desc* d){d->peer_id_addr=v12::peer.address;return 0;}', '''extern bool server_connected;extern ble_addr_t child_address;
extern int server_find_rc;extern uint8_t server_role;
int ble_gap_conn_find(uint16_t h,ble_gap_conn_desc* d){
 if(h==33&&server_connected){if(server_find_rc)return server_find_rc;d->conn_handle=33;d->role=server_role;d->peer_id_addr=child_address;}
 else d->peer_id_addr=v12::peer.address;return 0;}
''')
    s = replace_once(s, 'int ble_gap_terminate(uint16_t,int){++terminated;', '''extern bool server_slow;extern int server_terminate_rc;extern unsigned server_terminate_calls;
int ble_gap_terminate(uint16_t h,int){if(h==33){++server_terminate_calls;
 if(server_terminate_rc){if(server_terminate_rc==BLE_HS_ENOTCONN)server_connected=false;return server_terminate_rc;}
 if(!server_slow){ble_gap_event e{};e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.conn_handle=33;
 e.disconnect.conn.role=server_role;e.disconnect.conn.peer_id_addr=child_address;server_connected=false;v12::relay_gap(&e,nullptr);}return 0;}++terminated;''')
    s += (APP / 'tests/rc7_server_owner_fixture.inc').read_text()
    (out / 'server.cpp').write_text(s)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, default=SDK_DEFAULT)
    parser.add_argument('--cc', default='/opt/homebrew/opt/llvm/bin/clang')
    parser.add_argument('--model-only', action='store_true')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)  # Never replace a retained attempt.
    crypto = args.sdk.resolve() / 'components/bt/host/nimble/nimble/ext/tinycrypt'
    src = APP / 'main/r2/r2-g1'
    inputs = [Path(__file__).resolve(), APP / 'tests/rc7_permission_test.cpp',
              APP / 'tests/native_g1_test.cpp', APP / 'main/native_g1.cpp',
              APP / 'main/rc7_relay_native.inc', APP / 'tests/rc7_server_owner_fixture.inc',
              *[APP / 'main' / n for n in ('g1_corrected_clock.hpp', 'rc7_client.hpp',
                                         'g1_client_policy.hpp', 'g1_direct_session.hpp', 'g1_clock_prediction.hpp',
                                         'rc7_relay_hold.hpp', 'rc7_relay_estimate.hpp',
                                         'rc7_tx_reference.hpp', 'rc7_tx_estimate.hpp', 'g1_diagnostic_render.hpp')]]
    c_sources = [src / (n + '.c') for n in ('group_auth', 'group_join', 'group_precision', 'precision_codec', 'relay_codec')]
    c_sources += [APP / 'main/r2/rc7/protocol/rc_dynamic.c', APP / 'main/r2/rc7/src/rc_boundary.c', APP / 'main/r2/rc7/src/rc_slots.c', APP / 'main/g1_hmac.c']
    c_sources += [crypto / 'src' / n for n in ('sha256.c', 'utils.c')]
    inputs += c_sources
    before = {str(p): sha(p) for p in inputs}
    receipt = {'schema': 1, 'status': 'RUNNING', 'scope': 'FOCUSED_SYNTHETIC_PERMISSION_AND_NATIVE_BOUNDARY;NOT_FULL_SUITE',
               'actual_RF': False, 'hardware_access': False, 'Git_operations': False,
               'source_hashes': before, 'completed': [], 'native_binding': 'SYNTHETIC_DEVICE_ADAPTER_ONLY',
               'native_profiles': [] if args.model_only else [100],
               'half_duty_scope': 'MODEL_RX_EPOCH_RECREATION_ONLY;RADIO_SCHEDULING_IS_EXISTING_CONTROLLER_RUNNER'}
    stage = 'setup'

    def run(command, label):
        nonlocal stage
        stage = label
        command = list(map(str, command))
        (out / (label + '-command.json')).write_text(json.dumps(command, indent=2) + '\n')
        p = subprocess.run(command, capture_output=True, text=True, timeout=60,
                           env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1', 'UBSAN_OPTIONS': 'halt_on_error=1'})
        (out / (label + '.log')).write_text(p.stdout + p.stderr)
        if p.returncode:
            raise RuntimeError(f'{label} failed: {p.returncode}; attempt retained at {out}')
        receipt['completed'].append(label)
        return p.stdout

    try:
        (out / 'rc7_permission_test.cpp').write_bytes((APP / 'tests/rc7_permission_test.cpp').read_bytes())
        flags = ['-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
                 '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                 *['-I' + str(p) for p in (out, src, APP / 'main/r2/rc7/include', crypto / 'include', APP / 'main')]]
        objects = []
        for source in c_sources:
            obj = out / (source.stem + '.o')
            run([args.cc, '-std=c11', *flags, '-c', source, '-o', obj], source.stem + '-compile')
            objects.append(obj)
        run([args.cc + '++', '-std=c++20', *flags, out / 'rc7_permission_test.cpp', *objects, '-o', out / 'model'], 'model-build')
        receipt['model'] = json.loads(run([out / 'model'], 'model-run'))
        if not args.model_only:
            native_fixture(out)
            receipt['native'] = []
            native_flags = ['-Wno-missing-field-initializers', '-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192',
                            '-DV12_R2_ENABLED=1', '-DV12_R2_CONFIG=13', '-DV12_R2_FIELD_CLIENT=1', '-DV12_UNIT="C2"',
                            *['-I' + str(p) for p in (APP / 'tests/r2_fakes', APP / 'tests/console_fakes', APP / 'legacy/reference')]]
            # Half-duty radio scheduling is covered by the existing controller
            # runner. This fixture exercises native ownership/caller semantics
            # at100%; the pure model separately recreates a50% RX incarnation.
            for profile in (100,):
                binary = out / ('native-' + str(profile))
                run([args.cc + '++', '-std=c++20', *flags, *native_flags, f'-DV12_R2_G1_LISTENING_PERCENT={profile}', out / 'native.cpp', *objects, '-o', binary], f'native-{profile}-build')
                for case in NATIVE_CASES:
                    receipt['native'].append(json.loads(run([binary, case], f'native-{profile}-{case}')))
            server_fixture(out)
            server = out / 'server'
            run([args.cc + '++', '-std=c++20', *flags, *native_flags,
                 '-DV12_R2_G1_LISTENING_PERCENT=100', '-DV12_R2_TX_SHADOW=1',
                 '-DV12_R2_RC7_TX_ESTIMATE_LAB=1', out / 'server.cpp', *objects, '-o', server], 'server-build')
            receipt['server_cases'] = []
            for case in SERVER_CASES:
                run([server, case], 'server-' + case)
                receipt['server_cases'].append(case)
        changed = [str(p) for p in inputs if sha(p) != before[str(p)]]
        if changed:
            raise RuntimeError('Inputs changed during check: ' + ', '.join(changed))
        receipt['status'] = 'PASSED'
    except Exception as error:
        receipt.update(status='FAILED_RETAINED', failed_stage=stage, error=str(error))
        raise
    finally:
        receipt['writers_stopped'] = True
        (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'status': 'PASSED', 'model_cases': len(receipt['model']['cases']),
                      'native_cases': len(receipt.get('native', [])),
                      'server_cases': len(receipt.get('server_cases', [])), 'out': str(out)}))


if __name__ == '__main__':
    main()
