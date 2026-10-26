#!/usr/bin/env python3
"""MIT. Copyright(c)2026 Jonathan Yantis. Focused actual crash/journal/start tests."""
import argparse,hashlib,importlib.util,json,os,platform,struct,subprocess,sys,zlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);app=Path(__file__).resolve().parents[1];passed=[]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
inputs=[Path(__file__),*[app/n for n in('src/health.c','src/diagnostic.c','src/main.c','src/core.c','include/health.h','include/diagnostic.h','include/crash_record.h','include/snapshot_witness.h','tests/health_fake.h','tests/zephyr_fake.h','tests/crash_capture_test.c','tests/crash_journal_test.c','tools/crash_decode.py')]]
bound={str(p):sha(p)for p in inputs}
for n in('zephyr/fatal.h','zephyr/irq.h','zephyr/kernel.h','zephyr/timing/timing.h','cmsis_core.h','build_identity.h','ptob_device.h','zephyr/drivers/flash.h'):
 q=out/n;q.parent.mkdir(parents=True,exist_ok=True);q.write_text('#define PTOB_DEVICE_ID "123456789abcdef0"\n'if n=='ptob_device.h'else'#ifndef R2_SOURCE_SHA\n#define R2_SOURCE_SHA "'+'f'*64+'"\n#endif\n'if n=='build_identity.h'else'/* Boundary supplied by fixture. */\n')
main=(app/'src/main.c').read_text();begin=main.index('      if(rc!=-EAGAIN){',main.index('int main(void)'));end=main.index('    }\n    else if(r2_switch_rc_active())',begin);(out/'crash_main_start.inc').write_text(main[begin:end])
cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').is_file()else'clang');flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol')]
def run(label,command):
 with(out/(label+'.log')).open('x')as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=40)
 passed.append(label)
try:
 link=[]
 run('compile-producer',[*flags,*link,str(app/'tests/crash_capture_test.c'),'-o',str(out/'producer')]);run('producer',[str(out/'producer'),str(out/'crash.bin')])
 rejected_fpu=subprocess.run([*flags,'-DCONFIG_FPU=1','-fsyntax-only',str(app/'tests/crash_capture_test.c')],capture_output=True,text=True,timeout=30)
 (out/'FPU-reject.log').write_text(rejected_fpu.stdout+rejected_fpu.stderr);assert rejected_fpu.returncode and 'Bounded crash capture requires basic integer exception frames'in rejected_fpu.stderr;passed.append('FPU-enabled-producer-rejected')
 run('compile-journal',[*flags,'-DR2_LORA=1','-include',str(app/'tests/zephyr_fake.h'),str(app/'tests/crash_journal_test.c'),str(app/'src/core.c'),'-o',str(out/'journal')])
 for case in range(15):run('journal-'+str(case),[str(out/'journal'),str(case),str(out/'crash.bin')])
 spec=importlib.util.spec_from_file_location('crash_decoder',app/'tools/crash_decode.py');d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)
 raw=(out/'crash.bin').read_bytes();decoded,regs,windows=d.decode(raw);assert decoded['source']=='0123456789abcdef'*4 and decoded['silicon']=='123456789abcdef0'and regs[5]==5
 witness=(out/'crash.bin.witness').read_bytes();wd,_,_=d.decode(witness)
 assert wd['schema']==8 and wd['snapshot_witness']['completed_stage']==3 and wd['snapshot_witness']['mismatch_flags']==0
 partial=(out/'crash.bin.partial').read_bytes();pd,_,_=d.decode(partial)
 assert pd['snapshot_witness']['completed_stage']==1 and pd['snapshot_witness']['in_progress_stage']==2
 assert list(pd['snapshot_witness']['views'])==['entry']
 unchecked=(out/'crash.bin.unchecked').read_bytes();ud,_,_=d.decode(unchecked)
 assert ud['snapshot_witness']['completed_stage']==3 and not ud['snapshot_witness']['comparison_completed']
 assert ud['snapshot_witness']['mismatch_flags']==0 and ud['snapshot_witness']['observed_mismatch_flags']==1
 for slot,value in ((61,0),(52,68)):
  words=list(struct.unpack('<128I',witness));words[slot]=value;words[126]=zlib.crc32(struct.pack('<126I',*words[:126]))
  try:d.decode(struct.pack('<128I',*words))
  except AssertionError:pass
  else:raise AssertionError(('witness negative',slot,value))
 for slot,value in ((62,0),(63,0x10003|0x100),(93,0)):
  words=list(struct.unpack('<128I',witness));words[slot]=value;words[126]=zlib.crc32(struct.pack('<126I',*words[:126]));bad,br,bw=d.decode(struct.pack('<128I',*words))
  assert bad['snapshot_witness']['status']=='INCONSISTENT_OR_UNAVAILABLE'and len(bad['snapshot_witness']['raw_words'])==32
  assert bad['crashed_boot']==wd['crashed_boot']and bad['reason']==wd['reason']and br
 good_path=out/'schema8.bin';good_path.write_bytes(witness)
 bad_path=out/'schema8-inconsistent.bin';bad_path.write_bytes(struct.pack('<128I',*words))
 run('journal-schema8',[str(out/'journal'),'0',str(good_path)])
 run('journal-schema8-inconsistent',[str(out/'journal'),'0',str(bad_path)])
 passed.append('schema8-actual-producer-partial-and-negative-decoder')
 rejected=0
 for slot,value in((25,3),(25,17|4),(52,196),(54,68),(51,0x200080ff),(44,0xffffffe1),(2,0),(61,1)):
  words=list(struct.unpack('<128I',raw));words[slot]=value;words[126]=zlib.crc32(struct.pack('<126I',*words[:126]));bad=struct.pack('<128I',*words)
  try:d.decode(bad)
  except AssertionError:rejected+=1
  else:raise AssertionError((slot,value))
 # BASIC+RETURN but absent CALLEE must not synthesize PSP+32.
 words=list(struct.unpack('<128I',raw));words[25]=1|4|16;words[34:43]=[0]*9;words[51:56]=[0]*5;words[126]=zlib.crc32(struct.pack('<126I',*words[:126]));_,missing,_=d.decode(struct.pack('<128I',*words));assert 13 not in missing
 # Nested ISR CFI cannot read a misleading interrupted PSP window. Use actual
 # unwind engine with a tiny fake FDE boundary, not a pointer-scan model.
 class Rule:
  type='OFFSET';arg=0
 class CFA:
  expr=None;reg=4;offset=0
 class F:
  cie={'return_address_register':14}
  def __getitem__(self,k):return {'initial_location':0x1000,'address_range':0x100}[k]
  def get_decoded(self):return type('D',(),{'table':[{'pc':0x1000,'cfa':CFA(),14:Rule()}]})()
 class ELF:
  def get_section_by_name(self,n):return None
  def get_dwarf_info(self):return type('D',(),{'CFI_entries':lambda self:[F()]})()
  def iter_sections(self):return iter([dict(sh_addr=0x1000,sh_size=0x100,sh_flags=6)])
 old=d.FDE;d.FDE=F
 try:
  misleading=[{'kind':'thread','address':0x20008000,'data':struct.pack('<I',0x1001)},{'kind':'ISR','address':0x20030000,'data':bytes(64)}]
  kind,selected=d.faulted_windows('0xfffffff1',misleading);assert kind=='ISR'and len(selected)==1;u=d.unwind(ELF(),{15:0x1000,13:0x20030020,4:0x20008000},selected);assert u['stop']=='saved register outside captured windows'and len(u['frames'])==1
  u=d.unwind(ELF(),{15:0,13:0x20030020,14:0x10b},selected);assert u['stop']=='PC_outside_allocated_executable_sections'and len(u['frames'])==1
 finally:d.FDE=old
 # Actual savedchunk loader, preserving currentreadout identity separately.
 identity=dict(source='a'*64,commit='b'*40,silicon='123456789abcdef0',unit='TEST1',boot='0000000000000009')
 rows=[dict(identity,chunk=256+i,result=0,hex=raw[64*i:64*(i+1)].hex())for i in range(8)]
 q=out/'chunks.json';q.write_text(json.dumps(rows));loaded,current=d.load(q,256);assert loaded==raw and current==identity
 for label,changed in [('missing',rows[:-1]),('mixed-boot',rows[:-1]+[dict(rows[-1],boot='000000000000000a')]),('conflict',rows+[dict(rows[0],hex='00'*64)])]:
  q=out/(label+'.json');q.write_text(json.dumps(changed))
  try:d.load(q,256)
  except AssertionError:pass
  else:raise AssertionError('invalid savedchunks '+label)
 passed.append('actual-savedchunk-loader-negative-bindings')
 passed.append('decoder-provenance-and-context-isolation')
 assert all(sha(Path(p))==h for p,h in bound.items())
finally:
 (out/'receipt.json').write_text(json.dumps(dict(passed=len(passed)==24,hardware=False,class_='ACTUAL_PRODUCER_JOURNAL_GM7_START_FAKE_ARM_FLASH_BOUNDARIES',cases=passed,inputs=bound,decoder_rejections=locals().get('rejected',0),limits=['Actual register producer and word-copy bodies use safe host buffers; real numeric ARM bounds/provenance tested independently. No native SRAM mapping, hardware exception or physical retention claim','Faulted-context DWARF fakeCFI boundary tests memoryisolation only; exact nativeELF/resources reviewed separately','Original CPUcorruption producer remainsunknown']),indent=2)+'\n')
print('PASS focused actual crash producer/journal/GM7 start composition',len(passed))
