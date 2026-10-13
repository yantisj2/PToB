#!/usr/bin/env python3
"""MIT, copyright (c) 2026 Jonathan Yantis.

Adapted for the public repository paths. Focused offline checks for this proposal's tables/examples, not the full suite."""
from pathlib import Path
import hashlib, hmac, json, re, struct
ROOT=Path(__file__).resolve().parents[1]
def main():
    contract=json.loads((ROOT/'protocol/v14_rc1/contract.json').read_text())
    vectors=json.loads((ROOT/'protocol/v14_rc1/vectors.json').read_text())
    ledger=json.loads((ROOT/'evidence/v14/2026-10-12/provenance/ptob-v14-sig-wire.json').read_text())
    tex=(ROOT/'papers/ptob-v14-sig-proposal.tex').read_text()
    key=bytes.fromhex(vectors['test_key_hex'])
    context=struct.pack('<BQI',1,vectors['group_id'],vectors['group_epoch'])
    def mac(domain,body):return hmac.new(key,domain.encode()+context+body,hashlib.sha256).digest()[:16]
    routes=bytes([len(vectors['routes'])])+b''.join(struct.pack('<QBBBB',*r) for r in vectors['routes'])+bytes(12*(8-len(vectors['routes'])))
    assert len(routes)==97
    route=mac('RC5ROUTE',routes)[:8]
    all_fields=0
    for kind,w in contract['wire'].items():
        occupied=[i for f in w['fields'] for i in range(f['offset'],f['offset']+f['bytes'])]
        assert sorted(occupied)==list(range(w['bytes'])),kind
        fs=w['fields'] if kind!='GC17_HELLO' else w['fields'][24:]
        rows=ledger['table_rows'][kind]
        assert len(rows)==len(fs)
        for row,f in zip(rows,fs):
            end=f['offset']+f['bytes']-1
            offset=str(f['offset']) if f['bytes']==1 else f"{f['offset']}--{end}"
            assert row[:3]==[offset,str(f['bytes']),f['name']]
            prefix=' & '.join([offset,str(f['bytes']),f['name'].replace('_',r'\_')])+' & '
            assert prefix in tex,(kind,prefix)
        if kind=='GC17_HELLO':
            assert w['fields'][:26]==contract['wire']['GC17']['fields'][:26]
            assert 'shares bytes 0--115' in tex
        all_fields+=len(w['fields'])
    selected=list(ledger['selected_examples'].values())
    blocks=re.findall(r'\\begin\{verbatim\}\n(.*?)\\end\{verbatim\}',tex,re.S)
    assert len(blocks)==6
    for row,block in zip(selected,blocks):
        wire=bytes.fromhex(row['hex']); printed=bytearray()
        for line in block.splitlines():
            if not line.strip():continue
            offset,data=line.split(': ',1);assert int(offset)==len(printed)
            printed.extend(bytes.fromhex(data))
        assert bytes(printed)==wire,row['id']
        assert row==next(r for r in vectors['vectors'] if r['id']==row['id'])
        kind=row['profile'];layout='GC17_HELLO' if len(wire)==223 else kind
        w=contract['wire'][layout]
        assert len(wire)==w['bytes']
        domain=w['domains'].get('packet') or w['domains']['record']
        assert mac(domain,wire[:-16])==wire[-16:]
        if kind=='GR3':
            assert mac('PTOBG3O3',wire[14:74])==wire[74:90]
            q=int.from_bytes(wire[143:151],'little');assert q*125//2==100000000
            assert int.from_bytes(wire[66:74],'little',signed=True)==-1234
        if kind=='RC5':
            assert route==wire[88:96]
            assert mac('RC5ORIGN',route+wire[12:72])==wire[72:88]
            assert int.from_bytes(wire[133:141],'little')==100000000
            assert int.from_bytes(wire[48:56],'little',signed=True)==-1234
        if kind=='RQ5':assert route==wire[112:120]
        if kind in ['GR3','RC5']:
            stride=19 if kind=='GR3' else 27;start=136 if kind=='GR3' else 118
            assert not any(wire[start+wire[7]*stride:start+2*stride])
        if kind=='RC5':assert wire[172:174]==bytes([3,0])
        if kind=='JOIN4':assert not any(wire[118+wire[117]:142])
        if layout=='GC17_HELLO':assert not any(wire[183+(wire[182]&31):207])
    cites=set(x for group in re.findall(r'\\cite\{([^}]+)\}',tex) for x in group.split(','))
    bib=set(re.findall(r'\\bibitem\{([^}]+)\}',tex));assert cites<=bib
    links=re.findall(r'\\href\{([^}]+)\}',tex);assert all(u.startswith('https://') for u in links)
    assert not re.search(r'/Users/|/dev/cu\.|/Volumes/|(?:[0-9a-f]{2}:){5}[0-9a-f]{2}',tex,re.I)
    assert r'\input{' not in tex and r'\include{' not in tex
    assert '10543' not in tex
    print(json.dumps({'status':'PASS_DOCUMENT_EXAMPLES_ONLY','layouts':6,'contract_fields':all_fields,
        'selected_exact_examples':len(selected),'hmac_packet_origin_route_checks':'PASS',
        'reserved_and_padding_checks':'PASS','all_table_offsets_widths_names':'PASS',
        'citations_resolved':len(cites),'external_links':links,'standalone_source':True,
        'privacy_named_patterns':'PASS','broad_suite_rerun':False},indent=2))
if __name__=='__main__':main()
