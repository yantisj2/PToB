#!/usr/bin/env python3
"""Apply exact upstream teardown fix, then explicit PToB precision policy."""
from pathlib import Path
import hashlib,json,sys
APP=Path(__file__).resolve().parents[1]
def generate(deps,out):
    manifest=json.loads((APP/'cmake/ticker-backport.json').read_text())
    patch=(APP/'cmake/zephyr-91430.patch').read_bytes()
    assert hashlib.sha256(patch).hexdigest()==manifest['patch_sha256']
    edits={};name=None;before=[];after=[]
    for line in patch.decode().splitlines(keepends=True):
        if line.startswith('diff --git '):
            if name and before:edits[name]=(''.join(before),''.join(after))
            name=line.split(' b/',1)[1].strip();before=[];after=[]
        elif line.startswith('@@'):continue
        elif name and not line.startswith(('index ','--- ','+++ ')):
            if line.startswith(' '):before.append(line[1:]);after.append(line[1:])
            elif line.startswith('-'):before.append(line[1:])
            elif line.startswith('+'):after.append(line[1:])
    if name and before:edits[name]=(''.join(before),''.join(after))
    assert set(edits)==set(manifest['files'])
    for name,(old,new) in edits.items():
        body=(deps/name).read_text();assert hashlib.sha256(body.encode()).hexdigest()==manifest['files'][name]['original'],name
        assert body.count(old)==1,name
        fixed=body.replace(old,new);assert hashlib.sha256(fixed.encode()).hexdigest()==manifest['files'][name]['fixed'],name
        if name.endswith('ticker/ticker.c'):
            # Separate PToB policy after the byte-exact upstream backport.
            # Age/force still resolve peers of the same class. Accumulated
            # control latency must never outrank a reserved precision event.
            needle='\t\t\t/* Check if next node is within this reservation slot'
            assert fixed.count(needle)==1
            fixed=fixed.replace(needle,'''#if defined(CONFIG_BT_TICKER_PRIORITY_SET)
            if ((ticker->priority == -1) != (ticker_next->priority == -1)) {
                if (ticker_next->priority == -1 && !lazy_next_periodic_skip) return 1U;
                id_head = ticker_next->next;
                continue;
            }
#endif
'''+needle)
            # Use a distinct queued operation field, initialized by ALL normal
            # update calls. It fits within the existing start-operation union.
            fixed=fixed.replace('struct ticker_user_op_update {',
                'struct ticker_user_op_update {\n\tint8_t r2_phase_us;')
            fixed=fixed.replace('user_op->params.update.ticks_drift_plus = ticks_drift_plus;',
                'user_op->params.update.r2_phase_us = 0;\n\tuser_op->params.update.ticks_drift_plus = ticks_drift_plus;')
            # A fractional-only request is real work. The stock dispatcher
            # rejects updates with every stock field zero before node_update.
            empty='(user_op->params.update.ticks_drift_plus == 0U) &&'
            assert fixed.count(empty)==1
            fixed=fixed.replace(empty,
                '(user_op->params.update.r2_phase_us == 0) &&\n\t\t\t     '+empty)
            marker='static inline uint32_t ticker_job_node_update('
            helper='''static int r2_ticker_remainder_step(uint32_t *remainder, int8_t us)
{
    if (!us) return 0;
    int32_t next = (int32_t)*remainder + (int32_t)us * 1000000;
    int carry = 0;
    if (next > (int32_t)(HAL_TICKER_REMAINDER_RANGE >> 1)) {
        next -= HAL_TICKER_REMAINDER_RANGE; carry = 1;
    } else if (next < -(int32_t)(HAL_TICKER_REMAINDER_RANGE >> 1)) {
        next += HAL_TICKER_REMAINDER_RANGE; carry = -1;
    }
    *remainder = (uint32_t)next;
    return carry;
}

'''
            fixed=fixed.replace(marker,helper+marker)
            fixed=fixed.replace('\t/* Update ticks_to_expire from drift input */', '''    int carry = r2_ticker_remainder_step(&ticker->remainder_current,
                                        user_op->params.update.r2_phase_us);
    if (carry > 0) ++ticks_to_expire;
    else if (carry < 0) ++ticker->ticks_to_expire_minus;
\t/* Update ticks_to_expire from drift input */''')
            start=fixed.index('\n\tstruct ticker_instance *instance = &_instance[instance_index];',
                              fixed.index('uint8_t ticker_update('))
            end=fixed.index('\n}\n#endif /* CONFIG_BT_TICKER_UPDATE */',start)
            queued=fixed[start:end]
            queued=queued.replace('user_op->params.update.r2_phase_us = 0;',
                                   'user_op->params.update.r2_phase_us = us;')
            api='''
uint8_t r2_ticker_phase_step(uint8_t instance_index, uint8_t user_id,
                           uint8_t ticker_id, int8_t us,
                           ticker_op_func fp_op_func, void *op_context)
{
    if (us != 8 && us != -8) return TICKER_STATUS_FAILURE;
    const uint32_t ticks_drift_plus = 0, ticks_drift_minus = 0;
    const uint32_t ticks_slot_plus = 0, ticks_slot_minus = 0;
    const uint16_t lazy = 0;
    const uint8_t force = 0, must_expire = 0, expire_info_id = ticker_id;
'''+queued+'\n}\n'
            fixed+=api
        dest=out/Path(name).relative_to('subsys/bluetooth/controller');dest.parent.mkdir(parents=True,exist_ok=True)
        if not dest.is_file() or dest.read_text()!=fixed:dest.write_text(fixed)
if __name__=='__main__':generate(Path(sys.argv[1]),Path(sys.argv[2]))
