#!/usr/bin/env python3
"""MIT. Copyright(c)2026 Jonathan Yantis. Offline schema7/exact-ELF decoder.
Accept one512B retained/flash record or8saved framed R2diag replies. No device IO.
DWARF-CFI only: missing registers/windows or unsupported rules stop the unwind.
"""
import argparse,hashlib,json,struct,zlib
from pathlib import Path
from elftools.elf.elffile import ELFFile
from elftools.dwarf.callframe import FDE

MAGIC=0x44325231;COMMIT=0x434f4d31

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def payloads(q):
    if isinstance(q,dict):
        if 'chunk'in q and 'hex'in q:yield q
        else:
            for v in q.values():yield from payloads(v)
    elif isinstance(q,list):
        for v in q:yield from payloads(v)

def load(path,first):
    raw=path.read_bytes()
    if len(raw)==512:return raw,None
    text=raw.decode('utf8')
    try:rows=[json.loads(text)]
    except json.JSONDecodeError:rows=[json.loads(s)for s in text.splitlines()if s.strip()]
    chunks={};identity=None
    for q in payloads(rows):
        if not first<=q['chunk']<first+8:continue
        assert q['result']==0 and len(q['hex'])==128,'Failed/truncated R2 chunk'
        current=tuple(q[k]for k in('source','commit','silicon','unit','boot'))
        if identity is None:identity=current
        assert current==identity,'Mixed current readout identities/physical boots'
        value=bytes.fromhex(q['hex']);idx=q['chunk']-first
        assert idx not in chunks or chunks[idx]==value,'Conflicting duplicate chunk'
        chunks[idx]=value
    assert len(chunks)==8,'Missing dump chunks; no zero fill or inferred bytes'
    return b''.join(chunks[i]for i in range(8)),dict(zip(('source','commit','silicon','unit','boot'),identity))

def decode(raw):
    assert len(raw)==512
    w=struct.unpack('<128I',raw)
    assert w[0]==MAGIC and w[1]==7 and w[3]==512 and w[127]==COMMIT
    assert zlib.crc32(raw[:504])==w[126],'CRC mismatch/torn record'
    assert w[4]or w[5];assert w[6]or w[7]
    assert w[25]<1024 and w[61]==0 and w[52]<=192 and w[54]<=64 and not(w[52]&3 or w[54]&3)
    f=w[25];assert f&16
    if f&2:assert f&7==7
    if f&4:assert f&1 and w[44]in(0xfffffff1,0xfffffff9,0xfffffffd)
    else:assert not any(w[42:45])
    if not f&2:assert not any(w[34:43])
    if f&32:assert f&7==7 and not f&(8|128) and w[52] and w[51]==w[42] and 1<=w[55]<=12
    else:assert not(w[51]or w[52]or w[55])
    if f&64:assert f&4 and not f&8 and w[54] and w[53]==w[43]
    else:assert not(w[53]or w[54])
    assert not f&256 or f&32;assert not f&512 or f&64
    retained=w[2]==0xffffffff
    assert (not any(w[56:59]))if retained else(w[2]<16 and bool(w[56]or w[57]))
    windows=[]
    for start,length,offset,flag,name in((w[51],w[52],248,32,'thread'),(w[53],w[54],440,64,'ISR')):
        if length:
            assert w[25]&flag and not start&3 and 0x20002000<=start<=0x20040000-length
            windows.append(dict(kind=name,address=start,data=raw[offset:offset+length]))
    regs={}
    if w[25]&1:
        regs.update({i:w[26+i]for i in range(4)});regs.update({12:w[30],14:w[31],15:w[32]})
    if w[25]&2:regs.update({4+i:w[34+i]for i in range(8)})
    if w[25]&5==5 and not w[25]&8:
        assert w[44]in(0xfffffff1,0xfffffff9,0xfffffffd)
        frame=w[42]if w[44]==0xfffffffd else w[43]
        if frame and (w[44]!=0xfffffffd or w[25]&2):regs[13]=frame+32+(4 if w[33]&(1<<9)else 0)
    result=dict(schema=7,storage='retained_RAM'if retained else'bank0_flash',slot=None if retained else w[2],generation=w[56]|w[57]<<32,source=raw[32:64].hex(),build_binding=raw[64:96].hex(),silicon=f'{w[7]:08x}{w[6]:08x}',crashed_boot=f'{w[5]:08x}{w[4]:08x}',reason=w[24],flags=w[25],registers={f'r{k}':f'0x{v:08x}'for k,v in regs.items()},xpsr=f'0x{w[33]:08x}',PSP=f'0x{w[42]:08x}',entry_MSP=f'0x{w[43]:08x}',EXC_RETURN=f'0x{w[44]:08x}',TCB_value_only=f'0x{w[45]:08x}',vector=w[46],SCB_post_clear=[f'0x{x:08x}'for x in w[47:51]],allocation_id=w[55],windows=[dict(kind=q['kind'],address=f'0x{q["address"]:08x}',bytes=len(q['data']))for q in windows],crc32=f'{w[126]:08x}',retained_id=f'{w[126]if retained else w[58]:08x}')
    return result,regs,windows

def unwind(elf,regs,windows):
    dwarf=elf.get_dwarf_info();entries=[q for q in dwarf.CFI_entries()if isinstance(q,FDE)]
    executable=[(int(q['sh_addr']),int(q['sh_addr'])+int(q['sh_size']))for q in elf.iter_sections()if q['sh_flags']&6==6 and q['sh_size']]
    def code(pc):return any(lo<=pc<hi for lo,hi in executable)
    symbols=[]
    symtab=elf.get_section_by_name('.symtab')
    if symtab:
        symbols=[(int(q['st_value'])&~1,int(q['st_size']),q.name)for q in symtab.iter_symbols()if q['st_info']['type']=='STT_FUNC'and q['st_size']]
    def symbol(pc):return next((name for base,size,name in symbols if base<=pc<base+size),None)
    def word(addr):
        if addr&3:raise ValueError('unaligned saved register')
        for q in windows:
            off=addr-q['address']
            if 0<=off<=len(q['data'])-4:return struct.unpack_from('<I',q['data'],off)[0]
        raise ValueError('saved register outside captured windows')
    frames=[];seen=set();stop='frame_limit'
    for _ in range(16):
        if 15 not in regs or 13 not in regs:stop='original_PC_or_SP_unavailable';break
        pc=regs[15]&~1;sp=regs[13]
        if(pc,sp)in seen:stop='repeated_frame';break
        seen.add((pc,sp));frames.append(dict(pc=f'0x{pc:08x}',sp=f'0x{sp:08x}',symbol=symbol(pc),evidence='original_context'if len(frames)==0 else'DWARF_CFI'))
        if not code(pc):stop='PC_outside_allocated_executable_sections';break
        fde=next((q for q in entries if q['initial_location']<=pc<q['initial_location']+q['address_range']),None)
        if fde is None:stop='no_DWARF_CFI';break
        row=next((q for q in reversed(fde.get_decoded().table)if q['pc']<=pc),None)
        if row is None:stop='no_CFI_row';break
        try:
            rule=row['cfa']
            if rule.expr or rule.reg not in regs:raise ValueError('CFA expression/register unavailable')
            cfa=regs[rule.reg]+rule.offset;caller=dict(regs)
            for reg,rule in row.items():
                if not isinstance(reg,int):continue
                if rule.type=='OFFSET':caller[reg]=word(cfa+rule.arg)
                elif rule.type=='VAL_OFFSET':caller[reg]=cfa+rule.arg
                elif rule.type=='REGISTER':caller[reg]=regs[rule.arg]
                elif rule.type=='UNDEFINED':caller.pop(reg,None)
                elif rule.type!='SAME_VALUE':raise ValueError('unsupported CFI rule '+rule.type)
            ra=caller.get(int(fde.cie['return_address_register']))
            if not ra or not ra&1:raise ValueError('missing or non-Thumb return address')
            if not code((ra&~1)-2):raise ValueError('caller_PC_outside_allocated_executable_sections')
            caller[13]=cfa;caller[15]=(ra&~1)-2;regs=caller
        except(KeyError,ValueError)as error:stop=str(error);break
    return dict(frames=frames,stop=stop,limits='ExactELF DWARF CFI only; no guessed pointer scan, missing-byte fill or guarantee through assembly/optimization/corruption.192B/64B windows are intentionally short. Recovered SP accounts for basic32B frame and xPSR alignment; noFP frame supported.')

def faulted_windows(exc_return,windows):
    selected='thread'if exc_return=='0xfffffffd'else'ISR'if exc_return in('0xfffffff1','0xfffffff9')else None
    return selected,[q for q in windows if q['kind']==selected]

def main():
    p=argparse.ArgumentParser();p.add_argument('--input',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--first-chunk',type=lambda s:int(s,0),default=256);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    assert not a.out.exists();raw,current=load(a.input,a.first_chunk);d,regs,windows=decode(raw)
    receipt=json.loads((a.build/'receipt.json').read_text());device=json.loads((a.build/'device.json').read_text());elfpath=a.build/'target/zephyr/zephyr.elf'
    assert receipt['source_sha256']==d['source']and receipt['watchdog_binding_sha256']==d['build_binding'],'Wrong exact source/config build'
    assert device['device_id']==d['silicon'],'Wrong exact factory build'
    assert sha(elfpath)==receipt['artifacts']['target/zephyr/zephyr.elf'],'ELF hash differs from receipt'
    assert sha(a.build/'device.json')==receipt['device_config_sha256']
    # Unwind only the EXC_RETURN-selected faulted context. Interrupted PSP
    # bytes remain raw evidence and cannot satisfy an ISR-CFA register read.
    selected,fault_windows=faulted_windows(d['EXC_RETURN'],windows)
    with elfpath.open('rb')as stream:elf=ELFFile(stream);assert elf['e_machine']=='EM_ARM'and elf.little_endian;d['unwind']=unwind(elf,regs,fault_windows)
    d['unwind']['selected_stack_window']=selected
    d.update(status='DECODED_CRC_AND_EXACT_ELF_BOUND_CRASH7',readout_identity=current,hardware_operated=False,files={str(p):sha(p)for p in(a.input,elfpath,a.build/'receipt.json',a.build/'device.json')},limits='Original source/build/crashedboot stay distinct from current readout boot. Dump identifies captured context, not the earlier corrupting writer or CPU-fault resolution.')
    a.out.write_text(json.dumps(d,indent=2)+'\n');print(d['status'])
if __name__=='__main__':main()
