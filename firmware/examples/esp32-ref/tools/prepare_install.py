#!/usr/bin/env python3
"""Prepare a reviewable exact-device test package. Never open or flash hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os
from pathlib import Path

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image-receipt',required=True,type=Path)
    p.add_argument('--host-receipt',required=True,type=Path)
    p.add_argument('--current',required=True,type=Path,help='local current-installation record, read only')
    p.add_argument('--out',required=True,type=Path)
    a=p.parse_args();os.umask(0o077)
    if a.out.exists():p.error('fresh ignored output required')
    repo=Path(__file__).resolve().parents[4]
    if repo/'artifacts' not in a.out.resolve().parents:p.error('prepare only under ignored artifacts/')
    r=json.loads(a.image_receipt.read_text());h=json.loads(a.host_receipt.read_text())
    if r.get('artifact_class')!='ESP32_REF' or r.get('status')!='BUILD_COMPLETE_PHYSICAL_UNKNOWN' or r.get('binding')!='EXACT_DEVICE':
        p.error('inspected exact-device reference image required')
    if h.get('status')!='HOST_CHECKS_PASSED' or h.get('sources')!=r.get('sources'):p.error('passing host checks must bind this exact source snapshot')
    lesson=r.get('lesson',1)
    if h.get('lesson',1)!=lesson or (lesson==2 and not h.get('published_images_verified')):
        p.error('use the matching lesson host check and verify its published images')
    root=a.image_receipt.resolve().parent
    for name,value in r['artifacts'].items():
        if digest(root/name)!=value:p.error('candidate artifact changed: '+name)
    host_root=a.host_receipt.resolve().parent
    for name,value in h['artifacts'].items():
        if digest(host_root/name)!=value:p.error('host-check artifact changed: '+name)
    raw=a.current.read_bytes();current=json.loads(raw)
    device=json.loads((root/'local/device.json').read_text())
    if current.get('factory_mac','').lower()!=device['factory_mac'].lower():p.error('current and candidate factory identities differ')
    recovery=Path(current['recovery_image']).resolve()
    if repo/'artifacts' not in recovery.parents:p.error('recovery must be an existing local artifact')
    recovery_sha=digest(recovery)
    if recovery_sha!=current['recovery_image_sha256']:p.error('existing recovery digest differs')
    blocked=['USER_APPROVAL_REQUIRED','FRESH_EXACT_DEVICE_IDENTITY_REQUIRED','FRESH_INSTALL_OPERATOR_REVIEW_REQUIRED']
    if not current.get('resources_released',False) or current.get('active_observation',False):blocked.append('OTHER_AGENT_OWNS_DEVICE')
    result={'schema':1,'status':'PREPARED_AWAITING_APPROVAL_AND_HANDOFF','hardware_access':False,
            'candidate_receipt':str(a.image_receipt.resolve()),'candidate_receipt_sha256':digest(a.image_receipt),
            'application':str(root/r['application']),'application_sha256':r['application_sha256'],
            'factory_mac':device['factory_mac'],'device_id':device['device_id'],'role':r['profile'],
            'source_sha256':r['source_sha256'],'host_receipt':str(a.host_receipt.resolve()),
            'current_manifest':str(a.current.resolve()),'current_manifest_sha256':hashlib.sha256(raw).hexdigest(),
            'recovery_image':str(recovery),'recovery_image_sha256':recovery_sha,
            'application_offset':65536,'application_max_bytes':1048576,'pulse_gpio':r['pulse_gpio'],
            'blocked_until':blocked,'full_flash_backup':False,'copy_installed_application':False,
            'operator_checks':['obtain explicit approval and completed ownership handoff',
                'refresh current installation/recovery records; this saved snapshot is not live ownership',
                'verify exact silicon identity and original bounded production-W12 maintenance drain',
                'write only the approved application; preserve protected prefix and any necessary partial final sector',
                'read back the actual whole new application and compare protected bytes',
                f'check REF{lesson}_STATUS and source receipt; inspect display and button-off behavior',
                'release all device resources and retain failures as well as successes'],
            'precision':'UNQUALIFIED; no independent output/RF measurement'}
    a.out.mkdir(parents=True)
    (a.out/'review.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Prepared offline review:',a.out/'review.json')
    print('Device remains untouched. Approval and ownership handoff are still required.')
if __name__=='__main__':main()
