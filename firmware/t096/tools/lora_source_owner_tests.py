#!/usr/bin/env python3
"""Actual PT publication path with synthetic source and kernel/radio boundaries.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--deps',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
 for name in ['kernel.h','sys/atomic.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h']:
  f=a.out/'zephyr'/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Existing radio_service_fake boundary. */\n')
 crypto=a.deps/'modules/crypto/tinycrypt/lib';flags=[os.environ.get('CC','clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-DR2_LORA=1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(a.out.resolve()),'-I'+str(crypto/'include')]
 sources=[APP/'tests/radio_service_test.c',*[APP/'src'/n for n in ['radio_precision.c','relay_precision.c','core.c','clock.c','nmea.c','precision.c','lora_source.c','lora_rc8_policy.c']],*[APP/'protocol'/n for n in ['gnss_diagnostics.c','role_announcement.c','group_join.c','group_auth.c','group_control.c','group_precision.c','authority_codec.c','precision_codec.c','relay_codec.c','lora_rc8.c']],*[crypto/'source'/n for n in ['hmac.c','sha256.c','utils.c']]]
 for profile,defs in [('classic',[]),('slow',['-DR2_LORA_PASSIVE_SLOW=1']),('fast',['-DR2_LORA_PASSIVE_FAST=1']),('fixed500',['-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_FIXED500_EXPERIMENT=1'])]:
  binary=a.out/('source_owner_'+profile);subprocess.run([*flags,*defs,*map(str,sources),'-o',str(binary)],check=True)
  modes=[*range(23,30),62,63]
  if profile=='fixed500':modes.remove(27);modes.extend((64,65))
  for mode in modes:subprocess.run([str(binary),str(mode)],check=True)
 # Causal negative: only restore the stale GM-publication withdrawal check.
 old=a.out.resolve()/'old_service.c';body=(APP/'src/radio_service.c').read_text()
 current='bool withdraw_own=!ready&&pt_input.grant.gm&&!pt_input.grant.follower;'
 assert body.count(current)==1
 old.write_text(body.replace(current,'bool withdraw_own=!ready&&pt_output.scope.gm;'))
 fixture=a.out.resolve()/'old_source_test.c'
 fixture.write_text((APP/'tests/radio_service_test.c').read_text().replace('"../src/radio_service.c"','"'+str(old)+'"').replace('"../src/radio_audit.c"','"'+str(APP/'src/radio_audit.c')+'"'))
 negative=a.out.resolve()/'stale-output-negative'
 subprocess.run([*flags,'-I'+str(APP/'tests'),*map(str,[fixture,*sources[1:]]),'-o',str(negative)],check=True)
 r=subprocess.run([str(negative),'63'],capture_output=True,text=True)
 (a.out/'stale-output-negative.log').write_text(r.stdout+r.stderr)
 assert r.returncode!=0 and '!lora_withdraws' in r.stderr
 print('PASS causal negative: stale GM output lets unrelated local GNSS loss withdraw follower LoRa')
 # Restore only the strict200 badge gate against the same observed source.
 badge=a.out.resolve()/'badge_service.c';body=(APP/'src/radio_service.c').read_text()
 start=body.index(' bool follower_export=out.view.locked;');end=body.index(' uint64_t export_until=',start)
 badge.write_text(body[:start]+' bool follower_export=out.view.locked;\n'+body[end:])
 fixture=a.out.resolve()/'badge_source_test.c'
 fixture.write_text((APP/'tests/radio_service_test.c').read_text().replace('"../src/radio_service.c"','"'+str(badge)+'"').replace('"../src/radio_audit.c"','"'+str(APP/'src/radio_audit.c')+'"'))
 negative=a.out.resolve()/'strict-badge-negative'
 subprocess.run([*flags,'-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_FIXED500_EXPERIMENT=1','-I'+str(APP/'tests'),*map(str,[fixture,*sources[1:]]),'-o',str(negative)],check=True)
 r=subprocess.run([str(negative),'64'],capture_output=True,text=True);(a.out/'strict-badge-negative.log').write_text(r.stdout+r.stderr)
 assert r.returncode!=0 and 'lora_publishes==i+1' in r.stderr
 print('PASS causal negative: strict200 BLE badge revokes a fresh unchanged LoRa time source')
 # Restore only receiver entry into the transmitter-export pipeline.
 receiver=a.out.resolve()/'receiver_tx_pipeline.c';body=(APP/'src/radio_service.c').read_text()
 needle=' if(r2_lora_source_needed()){';assert body.count(needle)==1
 receiver.write_text(body.replace(needle,' if(r2_lora_source_needed()||r2_lora_ble_needed()){'))
 fixture=a.out.resolve()/'receiver_tx_pipeline_test.c'
 fixture.write_text((APP/'tests/radio_service_test.c').read_text().replace('"../src/radio_service.c"','"'+str(receiver)+'"').replace('"../src/radio_audit.c"','"'+str(APP/'src/radio_audit.c')+'"'))
 negative=a.out.resolve()/'receiver-tx-pipeline-negative'
 subprocess.run([*flags,'-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_FIXED500_EXPERIMENT=1','-I'+str(APP/'tests'),*map(str,[fixture,*sources[1:]]),'-o',str(negative)],check=True)
 r=subprocess.run([str(negative),'65'],capture_output=True,text=True);(a.out/'receiver-tx-pipeline-negative.log').write_text(r.stdout+r.stderr)
 assert r.returncode!=0 and '!lora_publishes' in r.stderr
 print('PASS causal negative: receiver observation must not construct/publish hypothetical LoRa TX exports')
if __name__=='__main__':main()
