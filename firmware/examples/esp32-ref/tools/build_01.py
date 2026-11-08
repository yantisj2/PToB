#!/usr/bin/env python3
"""Build ESP32-REF 01 offline. This tool has no flash or device-open operation.

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
sys.path.insert(0, str(REPO / 'tools'))
sys.path.insert(0, str(REPO / 'registry'))
from firmware_artifacts import run_builder
from registry import load as load_registry, header as registry_header

SDK_PIN = json.loads((ROOT / 'dependencies.lock.json').read_text())['native_sdk']
SDK_COMMIT = SDK_PIN['commit']
CONTROLLER_SHA = SDK_PIN['controller_sha256']


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def dump(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def ignored(path):
    return subprocess.run(['git', '-C', str(REPO), 'check-ignore', '--quiet', str(path)]).returncode == 0


def main(example=1):
    receiver = example == 2
    project = '02_minimal_receiver' if receiver else '01_wireless_softpll'
    app_name = f'esp32_ref_{example:02}'
    p = argparse.ArgumentParser(description=f'Build ESP32-REF {example:02} offline.')
    p.add_argument('--idf', type=Path, required=True)
    p.add_argument('--tools', type=Path, required=True, help='ESP-IDF tools installation')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--enrollment', type=Path, required=True, help='ignored local trust JSON')
    p.add_argument('--device-config', type=Path, help='ignored exact factory binding; required for W12')
    p.add_argument('--registry', type=Path, default=REPO / 'registry/example.json')
    p.add_argument('--board', choices=['generic', 'w12'], default='generic')
    if receiver:
        p.add_argument('--tier', choices=['basic', 'refined', 'fine'], default='basic')
        p.set_defaults(role='receiver', pulse_gpio=-1)
    else:
        p.add_argument('--role', choices=['leader', 'follower'], default='follower')
        p.add_argument('--clock', choices=['gptimer20', 'mcpwm80'], default='gptimer20',
                       help='local timestamp counter; BLE descriptor resolution is unchanged')
        p.add_argument('--pulse-gpio', type=int, default=-1, help='separately verified output; -1 leaves every output pad unrouted')
    p.add_argument('--w12-lora-present', action='store_true', help='known W12 auxiliary radio: keep its reset and rails OFF')
    a = p.parse_args()
    os.umask(0o077)
    sdk, tools, out = a.idf.resolve(), a.tools.resolve(), a.out.resolve()
    tier = ['basic', 'refined', 'fine'].index(a.tier) if receiver else 0
    fine = tier == 2 if receiver else a.clock == 'mcpwm80'
    profile = 'RX_' + a.tier.upper() if receiver else a.role.upper()
    version = '0.1.0' if receiver else '0.2.1'
    host_stack = 4096 if receiver else 8192
    counter_hz = 80000000 if fine else 20000000
    if out.exists() or REPO / 'artifacts' not in out.parents: p.error('fresh ignored artifacts/ output required')
    if a.board == 'w12' and not a.device_config: p.error('W12 requires an exact local device config')
    if a.board == 'w12' and a.pulse_gpio not in [-1, 16]: p.error('W12 exposes only separately verified GPIO16 for optional output')
    if a.board == 'generic' and a.w12_lora_present: p.error('W12 auxiliary rails are board-specific')
    if a.pulse_gpio != -1 and a.pulse_gpio not in [1, 2, 7, 8, 9, 10, 11, 13, 14, 15, 16, 21, 38, 39, 40, 41, 42]:
        p.error('reserved or unreviewed pulse pin')
    enrollment = a.enrollment.resolve()
    if not ignored(enrollment): p.error('enrollment must remain ignored')
    e = json.loads(enrollment.read_text())
    allowed = {'psk', 'key_epoch', 'group_id'} if receiver else {'psk'}
    if (set(e) - allowed or not isinstance(e.get('psk'), str)
            or not re.fullmatch('[0-9a-f]{64}', e['psk']) or not int(e['psk'], 16)):
        p.error('expected one nonzero 32-byte PSK')
    epoch = e.get('key_epoch') if receiver else 0
    if receiver and (type(epoch) is not int or not 0 < epoch <= 0xffffffff):
        p.error('Example 02 requires a nonzero key_epoch')
    device = None
    if a.device_config:
        if not ignored(a.device_config.resolve()): p.error('real device config must remain ignored')
        device = json.loads(a.device_config.read_text())
        mac = device.get('factory_mac', '')
        if not re.fullmatch('[0-9a-f]{2}(:[0-9a-f]{2}){5}', mac) or not int(mac.replace(':', ''), 16) or int(mac[:2], 16) & 1:
            p.error('nonzero unicast factory MAC required')
        if device.get('device_id') != '0000' + mac.replace(':', ''): p.error('factory/wire identity differs')
        if a.board == 'w12' and device.get('board') != 'meshnology_w12_esp32s3': p.error('wrong W12 board binding')
    registry = load_registry(a.registry)
    if device and registry['devices'].get(device['device_id'], {}).get('target') != 'w12' and a.board == 'w12':
        p.error('exact device must be in the shared W12 registry')
    if subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip() != SDK_COMMIT:
        p.error('wrong public ESP-IDF revision')
    if subprocess.check_output(['git', '-C', str(sdk), 'status', '--porcelain', '--untracked-files=all'], text=True).strip():
        p.error('SDK source must be clean')
    archive = sdk / 'components/bt/controller/lib_esp32c3_family/esp32s3/libbtdm_app.a'
    if digest(archive) != CONTROLLER_SHA: p.error('controller archive differs from the pinned ABI')
    if fine and digest(sdk / 'components/esp_driver_mcpwm/src/mcpwm_private.h') != SDK_PIN['mcpwm_private_sha256']:
        p.error('MCPWM driver header differs from the pinned ABI')
    python_candidates = sorted(tools.glob('python_env/idf5.5*/bin/python'))
    if len(python_candidates) != 1: p.error('one installed IDF 5.5 Python environment required')
    python = python_candidates[0]

    out.mkdir(parents=True)
    (out / 'local').mkdir()
    sources = {str(q.relative_to(ROOT)): digest(q) for q in sorted(ROOT.rglob('*'))
               if q.is_file() and '__pycache__' not in q.parts}
    source_hash = hashlib.sha256(json.dumps(sources, sort_keys=True).encode()).hexdigest()
    for name in sources:
        dst = out / 'source' / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, dst)
    shutil.copyfile(enrollment, out / 'local/enrollment.json')
    shutil.copyfile(a.registry, out / 'local/registry.json')
    if device: shutil.copyfile(a.device_config, out / 'local/device.json')
    label = registry['devices'].get(device['device_id'], {}).get('alias', device.get('label', 'REF')) if device else 'REF'
    if not re.fullmatch('[A-Za-z0-9 _.-]{1,24}', label): p.error('portable display alias required')
    key = ','.join('0x' + e['psk'][i:i + 2] for i in range(0, 64, 2))
    factory = ','.join('0x' + x for x in device['factory_mac'].split(':')) if device else '0,0,0,0,0,0'
    header = ('#pragma once\n#include <stdint.h>\n' +
        '#define REF_ROLE_LEADER ' + str(int(a.role == 'leader')) + '\n' +
        '#define REF_KEY_EPOCH ' + str(epoch) + 'u\n' +
        '#define REF_BOARD_W12 ' + str(int(a.board == 'w12')) + '\n' +
        '#define REF_DEVICE_BOUND ' + str(int(device is not None)) + '\n' +
        '#define REF_W12_LORA_PRESENT ' + str(int(a.w12_lora_present)) + '\n' +
        '#define REF_PULSE_GPIO ' + str(a.pulse_gpio) + '\n' +
        '#define REF_DEVICE_LABEL ' + json.dumps(label) + '\n' +
        'static const uint8_t REF_EXPECTED_MAC[6]={' + factory + '};\n' +
        'static const uint8_t REF_GROUP_KEY[32]={' + key + '};\n' + registry_header(registry) +
        'static inline const char *ref_registry_name(uint64_t id) { const char *name=ptob_registry_name(id); return name?name:"UNKNOWN"; }\n')
    (out / 'local/ref_local.h').write_text(header)
    r = {'schema': 1, 'artifact_class': 'ESP32_REF', 'lesson': example, 'version': version, 'profile': profile,
         'status': 'INCOMPLETE', 'source_dirty': True, 'source_sha256': source_hash, 'sources': sources,
         'binding': 'EXACT_DEVICE' if device else 'GENERIC_NO_TARGET', 'device_id': device['device_id'] if device else None,
         'board': a.board, 'pulse_gpio': a.pulse_gpio, 'w12_lora_present': a.w12_lora_present,
         'physical_precision': 'UNKNOWN', 'installed': False,
         'approval_required': True, 'hardware_access': False,
         'clock_backend': {'mode': ('MCPWM80_' if fine else '') +
                            ('GR3_DESCRIPTOR_RX_' if receiver else 'DESCRIPTOR_RX_PROGRAMMED_TX_') + a.board.upper(),
                           'counter_hz': counter_hz},
         'sdk_commit': SDK_COMMIT, 'controller_sha256': CONTROLLER_SHA, 'enrollment_sha256': digest(enrollment),
         'device_config_sha256': digest(a.device_config) if device else None}
    dump(out / 'receipt.json', r)
    env = dict(os.environ)
    for name in ['CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'EXTRA_COMPONENT_DIRS', 'SDKCONFIG_DEFAULTS']:
        env.pop(name, None)
    env.update(IDF_PATH=str(sdk), IDF_TOOLS_PATH=str(tools), IDF_COMPONENT_MANAGER='0',
               PYTHONDONTWRITEBYTECODE='1', PYTHONPYCACHEPREFIX=str(out / 'pycache'), IDF_CCACHE_ENABLE='0')
    try:
        exports = subprocess.check_output([str(python), str(sdk / 'tools/idf_tools.py'), 'export', '--format=key-value'], env=env, text=True)
        for line in exports.splitlines():
            if '=' in line:
                name, value = line.split('=', 1)
                env[name] = value.replace('$PATH', env.get('PATH', ''))
        compiler = subprocess.check_output(['xtensa-esp-elf-gcc', '--version'], env=env, text=True)
        if 'esp-14.2.0_20260121' not in compiler: raise ValueError('wrong pinned Xtensa compiler')
        (out / 'compiler.txt').write_text(compiler)
        command = [str(python), str(sdk / 'tools/idf.py'), '-G', 'Unix Makefiles', '-B', str(out / 'target'),
                   '-C', str(out / 'source' / project), '-D', 'IDF_TARGET=esp32s3',
                   '-D', 'SDKCONFIG=' + str(out / 'local/sdkconfig'), '-D', 'REF_LOCAL_HEADER=' + str(out / 'local/ref_local.h'),
                   '-D', 'REF_SOURCE_ID=' + source_hash, '-D', 'REF_CLOCK_HZ=' + str(counter_hz)]
        if receiver:
            command += ['-D', 'PTOB_TIER=' + str(tier)]
        command.append('build')
        dump(out / 'command.json', command)
        with (out / 'build.log').open('w') as log:
            subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)
        image = out / 'target' / (app_name + '.bin')
        if not 0 < image.stat().st_size <= 1048576: raise ValueError('application exceeds the one-MiB app-only slot')
        symbols = subprocess.check_output(['xtensa-esp-elf-nm', str(out / 'target' / (app_name + '.elf'))], env=env, text=True)
        (out / 'symbols.txt').write_text(symbols)
        forbidden = ['esp_wifi_init', 'esp_wifi_start', 'nvs_flash_erase', 'nvs_set_blob', 'nvs_set_str']
        for name in forbidden:
            if re.search(r'\b' + name + r'$', symbols, re.M): raise ValueError('forbidden runtime symbol: ' + name)
        if receiver:
            # NimBLE links advertiser APIs for its peripheral role. Check calls
            # from the receiver and demo, which only use GATT for temporary JOIN.
            archives = [out / 'target/esp-idf' / name / f'lib{name}.a' for name in ('ptob_rx', 'main')]
            calls = subprocess.check_output(['xtensa-esp-elf-nm', '--undefined-only', *map(str, archives)],
                                            env=env, text=True)
            for name in ['ble_gap_adv_start', 'ble_gap_ext_adv_start', 'ble_gap_periodic_adv_start']:
                if re.search(r'\b' + name + r'$', calls, re.M): raise ValueError('receiver advertises: ' + name)
        sections = subprocess.check_output(['xtensa-esp-elf-objdump', '-t', str(out / 'target' / (app_name + '.elf'))], env=env, text=True)
        (out / 'sections.txt').write_text(sections)
        irq_names = ['rx_hook', 'aux_hook', 'free_hook', 'ref_counter_port_now', 'ref_radio_rx_tick']
        if not receiver:
            irq_names += ['tx_hook', 'ref_radio_tx_tick', 'ref_radio_channel']
        irq_names += ['latch_current', 'ref_capture32_advance', 'esp_timer_get_time'] if fine else ['gptimer_get_raw_count']
        for name in irq_names:
            hits = [line for line in sections.splitlines() if line.split() and line.split()[-1] == name]
            if len(hits) != 1 or '.iram0.text' not in hits[0]: raise ValueError('IRQ function not in IRAM: ' + name)
        frames = []
        for usage in (out / 'target').rglob('*.su'):
            for line in usage.read_text().splitlines():
                fields = line.split('\t')
                if len(fields) >= 2 and fields[1].isdigit() and '/source/' in fields[0]:
                    frames.append({'function': fields[0].split('/source/', 1)[1], 'bytes': int(fields[1]), 'kind': fields[-1]})
        if not frames or max(row['bytes'] for row in frames) > 4096: raise ValueError('example stack frame exceeds offline review limit')
        dump(out / 'inspection.json', {'status': 'OFFLINE_INSPECTION_PASS', 'image_bytes': image.stat().st_size,
             'irq_functions_in_iram': irq_names, 'static_frames': frames, 'main_task_bytes': host_stack,
             'host_task_bytes': host_stack, 'scope': 'SELECTED_PLACEMENT_AND_STATIC_FRAMES_NOT_COMPLETE_CALL_GRAPH',
             'hardware_precision': 'UNMEASURED', 'hardware_access': False})
        rows = json.loads((out / 'target/compile_commands.json').read_text())
        for row in rows:
            if 'firmware/w12/' in row['file'] or 'firmware/n30/' in row['file']: raise ValueError('operational target dependency')
        config = (out / 'local/sdkconfig').read_text()
        required_config = ['CONFIG_IDF_TARGET="esp32s3"', 'CONFIG_BT_CTRL_BLE_MAX_ACT=6',
                           f'CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE={host_stack}']
        if receiver:
            required_config += ['CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1', 'CONFIG_BT_NIMBLE_MAX_PERIODIC_SYNCS=1',
                                'CONFIG_BT_NIMBLE_GATT_CLIENT=y', 'CONFIG_BT_NIMBLE_GATT_SERVER=y',
                                'CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=247']
        for required in required_config:
            if required not in config: raise ValueError('compiled configuration differs: ' + required)
        # ESP-IDF's hidden Wi-Fi component switch can remain enabled even for
        # BLE-only builds. The ELF entry-point check above is the runtime gate.
        for unsafe in ['CONFIG_BT_NIMBLE_NVS_PERSIST=y', 'CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=y']:
            if unsafe in config: raise ValueError('unexpected runtime feature: ' + unsafe)
        if 'CONFIG_ESP_INT_WDT_TIMEOUT_MS=5000' not in config or 'CONFIG_ESP_TASK_WDT_TIMEOUT_S=5' not in config:
            raise ValueError('five-second watchdog minimum changed')
        if any(digest(ROOT / name) != value for name, value in sources.items()): raise ValueError('source changed during build')
        r.update(status='BUILD_COMPLETE_PHYSICAL_UNKNOWN', application='target/' + app_name + '.bin',
                 application_sha256=digest(image), application_bytes=image.stat().st_size,
                 application_offset=65536, application_max_bytes=1048576,
                 artifacts={str(q.relative_to(out)): digest(q) for q in out.rglob('*') if q.is_file() and
                            (q.suffix in ['.bin', '.elf', '.map', '.su'] or q.parent == out / 'local' or
                             q.name in ['compile_commands.json','inspection.json','compiler.txt','sections.txt'])})
        dump(out / 'receipt.json', r)
        print('Offline ' + a.role + ' image inspected: ' + str(image) + ' (' + str(image.stat().st_size) + ' bytes)')
    except BaseException as error:
        r.update(status='FAILED', error=str(error))
        dump(out / 'receipt.json', r)
        raise


if __name__ == '__main__':
    run_builder(main, REPO)
