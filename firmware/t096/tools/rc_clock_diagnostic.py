"""Observation-only LF release diagnostic on one exact pinned controller source."""
import hashlib,json,sys,difflib
from pathlib import Path
src=Path(sys.argv[1])/'subsys/bluetooth/controller/ll_sw/nordic/lll/lll_clock.c'
out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
old=src.read_text();assert hashlib.sha256(old.encode()).hexdigest()=='ffa539aee03b5dd1299142a9c49fa451ef1069d38bedd77aa77e0bc207947b6b'
a='\t/* Cancel any ongoing request */\n\t(void)onoff_cancel(mgr, &lf_cli);\n\n\treturn onoff_release(mgr);'
b='''\t/* Observer only: preserve exactly one cancel and one release call. */
\tunsigned before_refs = mgr->refs, before_flags = mgr->flags;
\tint cancelled = onoff_cancel(mgr, &lf_cli);
\tunsigned middle_refs = mgr->refs, middle_flags = mgr->flags;
\tint released = onoff_release(mgr);
\tprintk("RC_LF_DEINIT before_refs=%u before_flags=%u cancel=%d middle_refs=%u middle_flags=%u release=%d after_refs=%u after_flags=%u\\n", before_refs, before_flags, cancelled, middle_refs, middle_flags, released, mgr->refs, mgr->flags);
\treturn released;'''
assert old.count(a)==1
new=old.replace(a,b).replace('#include <soc.h>','#include <soc.h>\n#include <zephyr/sys/printk.h>')
assert new.replace(b,a).replace('#include <soc.h>\n#include <zephyr/sys/printk.h>','#include <soc.h>')==old
(out/'lll_clock.c').write_text(new)
(out/'diagnostic.diff').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='pinned/lll_clock.c',tofile='diagnostic/lll_clock.c')))
(out/'manifest.json').write_text(json.dumps({'class':'LF_RELEASE_OBSERVATION_ONLY','original_sha256':hashlib.sha256(old.encode()).hexdigest(),'derived_sha256':hashlib.sha256(new.encode()).hexdigest(),'behavior':'Same cancel/release order and return value; one post-release diagnostic. No error suppression.'},indent=2)+'\n')
# The already-applied supported upstream backport has a second deinit gate.
ticker=Path(sys.argv[3])/'ticker/ticker.c';old=ticker.read_text()
assert hashlib.sha256(old.encode()).hexdigest()=='99ca17309c0d21aef7177828aff7c52ad17dae3c120c99b022bf115661298f96'
a='\tif (instance->ticker_id_head != TICKER_NULL) {\n\t\treturn -EBUSY;\n\t}'
b='''\tif (instance->ticker_id_head != TICKER_NULL) {
\t\tunsigned id = instance->ticker_id_head;
\t\tfor (unsigned count = 0; id != TICKER_NULL && count < 16; ++count) {
\t\t\tstruct ticker_node *n = &instance->nodes[id];
\t\t\tprintk("RC_TICKER_DEINIT_BUSY id=%u next=%u req=%u ack=%u period=%u delta_ticks=%u current_ticks=%u callback=%p context=%p\\n", id, n->next, n->req, n->ack, n->ticks_periodic, n->ticks_to_expire, instance->ticks_current, n->timeout_func, n->context);
\t\t\tid = n->next;
\t\t}
\t\treturn -EBUSY;
\t}'''
assert old.count(a)==1
new='#include <zephyr/sys/printk.h>\n'+old.replace(a,b)
assert new.removeprefix('#include <zephyr/sys/printk.h>\n').replace(b,a)==old
(out/'ticker.c').write_text(new)
(out/'ticker.diff').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='supported-backport/ticker.c',tofile='diagnostic/ticker.c')))
(out/'ticker-manifest.json').write_text(json.dumps({'original_sha256':hashlib.sha256(old.encode()).hexdigest(),'derived_sha256':hashlib.sha256(new.encode()).hexdigest(),'behavior':'Only logs existing EBUSY branch, same error returned; bounded16nodes.'},indent=2)+'\n')
# Bounded acquisition observer: no scheduling or RF decisions changed.
sync_src=Path(sys.argv[4])/'lll_sync.c'
old=sync_src.read_text();assert hashlib.sha256(old.encode()).hexdigest()=='4b27e47c715ac8d6336ab1075624fd02d63ba0d5330341b5729d19a905e871d7'
a='\tlll = p->param;\n\n\t/* Calculate the current event latency */'
b='\tlll = p->param;\n\tif (k_uptime_get() >= 40000 && k_uptime_get() < 50000) printk("RC_SYNC_PREP ms=%lld event=%u lazy=%u skip=%u\\n", (long long)k_uptime_get(), lll->event_counter, lll->lazy_prepare, lll->skip_prepare);\n\n\t/* Calculate the current event latency */'
assert old.count(a)==2
new=old.replace(a,b)
a2='\tlll = prepare_param->param;\n\tlll->skip_prepare += (lll->lazy_prepare + 1U);'
b2='\tlll = prepare_param->param;\n\tif (k_uptime_get() >= 40000 && k_uptime_get() < 50000) printk("RC_SYNC_ABORT ms=%lld event=%u lazy=%u skip=%u\\n", (long long)k_uptime_get(), lll->event_counter, lll->lazy_prepare, lll->skip_prepare);\n\tlll->skip_prepare += (lll->lazy_prepare + 1U);'
assert old.count(a2)==1
new='#include <zephyr/sys/printk.h>\n'+new.replace(a2,b2)
assert new.removeprefix('#include <zephyr/sys/printk.h>\n').replace(b,a).replace(b2,a2)==old
(out/'lll_sync.c').write_text(new)
(out/'sync.diff').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='pinned-plus-capture/lll_sync.c',tofile='diagnostic/lll_sync.c')))
(out/'sync-manifest.json').write_text(json.dumps({'original_sha256':hashlib.sha256(old.encode()).hexdigest(),'derived_sha256':hashlib.sha256(new.encode()).hexdigest(),'behavior':'Only prepare/abort logging in40–50s interval; no scheduling/RF modifications.'},indent=2)+'\n')
