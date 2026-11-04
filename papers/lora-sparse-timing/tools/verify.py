#!/usr/bin/env python3
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""Validate selected-data aggregates, privacy and matching edition hashes."""
import argparse,hashlib,json,math,re,shutil,subprocess
from pathlib import Path
from analyze import analyze
P=Path(__file__).resolve().parents[1]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def pagecount():
 if not shutil.which('pdfinfo'):return None
 info=subprocess.check_output(['pdfinfo',str(P/'main.pdf')],text=True)
 return int(re.search(r'^Pages:\s+(\d+)',info,re.M).group(1))
def compare(a,b):
 if isinstance(a,dict):
  assert a.keys()==b.keys()
  for k in a:compare(a[k],b[k])
 elif isinstance(a,list):
  assert len(a)==len(b)
  for x,y in zip(a,b):compare(x,y)
 elif isinstance(a,float):assert math.isclose(a,b,abs_tol=1e-7,rel_tol=1e-11),(a,b)
 else:assert a==b,(a,b)
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--write-edition',action='store_true');args=ap.parse_args()
 compare(analyze(),json.loads((P/'results.json').read_text()))
 c=json.loads((P/'data/cohorts.json').read_text());v=json.loads((P/'provenance.json').read_text())
 assert c['F287']['planned']==c['F287']['mapped']+c['F287']['no_map']+c['F287']['uncaptured']==900
 assert c['heterogeneous_extension']['snapshot_rows']==1800 and c['heterogeneous_extension']['available_rows']==267
 assert c['heterogeneous_extension']['independent_PPS_records']==0
 assert c['calibration_retuned_on_validation'] is False
 assert c['observer_only_delay_ns']==0
 assert c['source_TX_offer_denominator']=='UNKNOWN_NO_SOURCE_USB'
 assert len(v['inputs'])==12 and len({x['locator'] for x in v['inputs']})==12
 text_files=[x for x in P.rglob('*') if x.is_file() and x.suffix in ['.tex','.json','.md','.csv','.py'] and '__pycache__' not in x.parts]
 for p in text_files:
  text=p.read_text()
  if p.name!='verify.py':
   assert '/Users/' not in text and '/dev/cu.' not in text and '.internal/' not in text,p
   assert not re.search(r'\b(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}\b',text),p
   assert not re.search(r'(?i)-----BEGIN .*PRIVATE KEY-----|group_psk\s*[:=]|"factory"\s*:',text),p
 assert 'DRAFT FOR AUTHOR REVIEW' in (P/'main.tex').read_text()
 # Hash code/data/document files, excluding the self-referential edition record.
 selected=[x for x in P.rglob('*') if x.is_file() and '__pycache__' not in x.parts and x.name!='edition.json']
 selected=[x for x in selected if x.suffix in ['.tex','.pdf','.json','.md','.csv','.py'] or x.name=='.gitattributes']
 if args.write_edition:
  assert (P/'main.pdf').exists()
  pages=pagecount();assert pages is not None and 5<=pages<=7
  edition={'edition':'lora-sparse-timing-author-review-2026-11-04','status':'DRAFT_FOR_AUTHOR_REVIEW_NOT_PUBLICATION_APPROVED','author':'Jonathan Yantis','date_utc':'2026-11-04','pages':pages,'paper_license':'CC-BY-4.0','original_data_license':'CC-BY-4.0','code_license':'MIT','hardware_operations_performed':False,'new_experiments_performed':False,'source_scope':'Measured T096 0.15.288; wire3 schedule8 fields inspected against frozen10aba reference; no later firmware accuracy qualification','validation':'Selected-data recomputation and privacy checks pass; PDF pagecount/text and all-page visual review recorded in build-and-review.json','files':{str(x.relative_to(P)):digest(x) for x in sorted(selected)}}
  (P/'edition.json').write_text(json.dumps(edition,indent=2)+'\n')
 if (P/'edition.json').exists():
  e=json.loads((P/'edition.json').read_text());assert e['status']=='DRAFT_FOR_AUTHOR_REVIEW_NOT_PUBLICATION_APPROVED'
  pages=pagecount()
  if pages is not None:assert pages==e['pages']
  for rel,h in e['files'].items():assert digest(P/rel)==h,rel
  assert set(e['files'])=={str(x.relative_to(P)) for x in selected}
 print('PASS: selected populations, aggregates, calibration separation, availability limits, privacy and edition identities.')
if __name__=='__main__':main()
