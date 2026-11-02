// Copyright 2026 Jonathan Yantis. MIT.
'use strict';
const $ = id => document.getElementById(id);
const text = (id, value) => { $(id).textContent = value; };
const number = n => Number.isFinite(n) ? n.toLocaleString(undefined, {maximumFractionDigits: 0}) : 'Unknown';
const ago = n => n == null ? 'No observation' : `${n.toFixed(1)} s ago`;
const duration = s => Math.abs(s) < 1e-6 ? `${(s * 1e9).toFixed(0)} ns` : Math.abs(s) < .001 ? `${(s * 1e6).toFixed(1)} µs` : `${(s * 1e3).toFixed(2)} ms`;
const sourceState = s => ({'*':'Selected','+':'Combined','-':'Excluded','?':'Unavailable','x':'Falseticker','~':'Variable'}[s] || s);
let busy = false, lastData = null;
const eventNames = ['','State','Heartbeat','Scope','Parent selected','Sync requested','Sync ready','Sync ended','Sync canceled','Control connect','Control connected','Control error','Control close','Control disconnected','GM admission','Owner fault','GM connected','GM disconnected','GM MTU error','Holdover expired','Holdover recovered','Hot state','Initiator target','Initiator RX','Initiator request','Initiator aborted','Control drain','Control retry','Advertising','RX reservation','Delivery','Phase','GM control','AUX'];
function facts(id, rows) {
  $(id).replaceChildren();
  for (const [label,value] of rows) { const row=document.createElement('div'),dt=document.createElement('dt'),dd=document.createElement('dd'); dt.textContent=label; dd.textContent=value; row.append(dt,dd); $(id).append(row); }
}
function parent(d, rx) {
  const a=rx.acquisition;
  if (!Array.isArray(a)||a.length!==20||(!a[1]&&!a[2])) return 'Not selected';
  const id=a[2].toString(16).padStart(8,'0')+a[1].toString(16).padStart(8,'0');
  const alias=d.aliases?.[id];
  return alias ? `${alias.alias} (${alias.target.toUpperCase()})` : id;
}
function device(d, id) {
  if (!id || id === '0000000000000000') return 'Unknown';
  const alias=d.aliases?.[id];
  return alias ? `${alias.alias} · ${id}` : id;
}
function rows(id, values, empty, columns) {
  $(id).replaceChildren();
  if (!values.length) {const tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=columns;td.textContent=empty;tr.append(td);$(id).append(tr);return;}
  for (const cells of values) {const tr=document.createElement('tr');for (const value of cells) {const td=document.createElement('td');td.textContent=value;tr.append(td);}$(id).append(tr);}
}
function sourceReport(d, stale) {
  const s=d.source_status || {}, rx=d.receiver.data || {}, a=rx.acquisition || [];
  const old=stale || s.stale, current=!old && s.current;
  text('source-state', old ? 'Stale observation' : current ? 'Timing admitted' : 'Timing unavailable');
  text('source-summary', old ? 'Source observations stopped updating. Current GM, admission and recovery progress are unknown; the details below are last observed.' : current ? `Serving GM ${device(d,s.root)} → parent ${device(d,s.parent)} → HAT.` : s.root ? `No current admitted timing. Last valid GM: ${device(d,s.root)} · ${ago(s.last_valid_age_s)}.` : 'No GM has supplied valid timing in this receiver boot. The selected parent and signed reports below are acquisition observations.');
  const control=s.last_control;
  let lastControl='No exchange recorded';
  if (control) {
    const age=Math.max(0,((rx.uptime_ms || 0)-control.ms)/1000);
    const meaning=control.kind===27 && control.detail===1 ? 'Authenticated PENDING; parent not ready' : control.kind===13 && control.result===22 ? 'Local controller closed the link (0x16)' : `${eventNames[control.kind] || 'Control'} · result ${control.result}`;
    lastControl=`${meaning} · ${device(d,control.peer)} · ${old ? `age at last observation ${age.toFixed(1)} s` : ago(age)}`;
  }
  const mesh=rx.mesh || [], discovery=rx.discovery || {};
  const latestReport=(s.reports || []).reduce((latest,r)=>!latest || r.age_ms<latest.age_ms ? r : latest,null);
  facts('source-facts', [[current ? 'Serving grandmaster' : 'Last admitted grandmaster', device(d,s.root)],
    ['GM boot / source epoch',s.root ? `${s.root_boot} / ${s.epoch}` : 'Unknown until timing admission'],
    ['Selected BLE parent',device(d,s.parent)],
    ['Last signed GM report',latestReport ? `${device(d,latestReport.root)} · ${old ? 'receipt age at last observation' : 'received'} ${(latestReport.age_ms/1000).toFixed(1)} s${old ? '' : ' ago'}; role report only` : 'None heard in this receiver boot'],
    ['Acquisition',`${stale || s.stale ? 'Last observed' : ['Idle','Discovering context','RC7 owner running','Draining previous context'][s.discovery_state] || 'Unknown'} · ${s.control_admitted ? 'control admitted' : 'awaiting control admission'} · ${s.periodic_sync ? 'periodic sync active' : 'no periodic sync'}`],
    ['Discovery context',s.context && s.context!=='0000000000000000' ? s.context : 'Not selected'],
    ['Mesh selections / rediscoveries',`${number(s.selections)} / ${number(s.restarts)}`],
    ['Outer recovery',old ? 'Current progress unknown; observation is stale' : current ? 'Clock valid; recovery timer reset' : s.discovery_state!==2 ? 'Context discovery or drain in progress' : s.recovery_remaining_s != null ? `Rediscovery in at most ${Math.ceil(s.recovery_remaining_s)} s; native recovery continues` : 'Reporting unavailable in installed image'],
    ['Last control exchange (journal)',lastControl],
    ['GM reports omitted at cache capacity',number(discovery.role_capacity_refused)]]);
  rows('gm-reports',(s.reports || []).map(r=>[device(d,r.root),`${r.root_boot} / ${r.epoch}`,r.hot==='0000000000000000' ? 'None reported' : device(d,r.hot),`${(r.age_ms/1000).toFixed(1)} s${old ? ' at last observation' : ''} · ${!old && r.fresh ? 'recent receipt' : 'last observed receipt'}`]),'No signed GM report heard in this receiver boot. The owner may be focused on a periodic train.',4);
  rows('source-changes',(s.changes || []).slice(-8).reverse().map(e=>[new Date(e.utc).toLocaleString(),e.event,`${device(d,e.root)} / ${device(d,e.parent)}`]),'No source transitions observed.',3);
}
function stage(id, state, title, note) {
  $(id + '-stage').className = state;
  text(id + '-state', title); text(id + '-note', note);
}
function services(raw) {
  return Object.fromEntries(raw.split('\n\n').filter(Boolean).map(block => {
    const props = Object.fromEntries(block.split('\n').map(line => line.split('=')));
    return [props.Id, props.ActiveState || 'unknown'];
  }));
}
function networkState(d, states, live) {
  const chrony = states['chrony.service'] === 'active';
  const wifi = states['ptob-ptp-wifi.service'] === 'active';
  const transfer = d.clock_chain?.transfer, server = d.clock_chain?.server;
  if (!live) return {state:'wait', title:'Stale', note:'Waiting for current timing observations'};
  if (!d.chain.complete) return {state:'wait', title:d.chain.chrony_holdover ? 'PToB holdover' : 'PToB unavailable',
    note:chrony ? 'NTP may continue from holdover or network UTC' : 'NTP service unavailable'};
  if (!chrony) return {state:'wait', title:'NTP unavailable', note:'Check the chrony service'};
  if (!transfer?.fresh || !server?.fresh) return {state:'wait', title:'PTP state unavailable', note:'NTP uses PToB · Ethernet guard observation unavailable or stale'};
  if (!transfer.running || !server.ready || !server.running) return {state:'wait', title:'Ethernet PTP waiting',
    note:`NTP uses PToB · ${server.reason || transfer.reason || 'Clock transfer is not ready'}`};
  if (d.ethernet_link !== true) return {state:wifi ? 'good' : 'wait', title:'NTP active · Ethernet ready',
    note:`Wi-Fi PTP ${wifi ? 'active' : 'unavailable'} · ${d.ethernet_link === false ? 'Ethernet has no link' : 'Ethernet link unknown'} · Accuracy unmeasured`};
  return {state:'good', title:'NTP / Ethernet PTP active', note:`Wi-Fi PTP ${wifi ? 'active' : 'unavailable'} · Accuracy unmeasured`};
}
function render(d) {
  lastData=d;
  const stale = d.stale, c = d.chain, r = d.receiver, rx = r.data || {}, p = d.pps;
  const live = !stale, admitted = live && c.admitted, edges = live && p.fresh;
  const states = services(d.observations.services.text);
  text('headline', c.summary);
  $('signal').className = 'signal ' + (c.complete && live ? 'good' : 'wait');
  text('detail', stale ? 'The collector stopped updating. Check its service before trusting this display.' : c.complete ? 'Receiver timing is admitted, PPS edges are current, and chrony selects PTOB. Output accuracy remains unmeasured.' : c.chrony_holdover ? `PPS edges stopped (${ago(p.age_s)}). Chrony retains PTOB in holdover; current PToB timing is unavailable.` : c.selected ? `The system clock currently selects ${c.selected.name === 'PTOB' ? 'PTOB PPS' : 'NTP ' + c.selected.name}. The complete PToB transfer is not yet established.` : 'No chrony source is selected. Clock discipline is not established.');
  text('age', `Device sample ${ago(d.sample_age_s)} · ${new Date(d.observed_utc).toLocaleString()}`);
  sourceReport(d,stale);
  stage('receiver', admitted ? 'good' : 'wait', stale ? 'Stale' : admitted ? 'Timing admitted' : r.fresh ? 'Acquiring' : 'Telemetry unavailable', r.fresh ? `${number(rx.authenticated)} authenticated · ${ago(r.age_s)}` : r.transport + ' · ' + ago(r.age_s));
  stage('pps', edges ? 'good' : 'wait', stale ? 'Stale' : edges ? 'Edges current' : 'No current edges', `${number(p.sequence)} captured · ${ago(p.age_s)}`);
  stage('clock', live && c.pps_disciplined ? 'good' : 'wait', stale ? 'Stale' : c.pps_disciplined ? 'PPS selected' : c.pps_selected ? 'PTOB holdover' : c.selected ? 'NTP selected' : 'No selected source', c.selected ? c.selected.name + ' · coarse UTC from NTP' : 'Awaiting chrony');
  const output = networkState(d, states, live);
  stage('output', output.state, output.title, output.note);
  text('selection', stale ? 'Stale observation' : c.pps_selected ? 'PTOB selected' : 'PTOB not selected');
  $('sources').replaceChildren();
  for (const s of d.sources) {
    const tr = document.createElement('tr'); if (s.state === '*') tr.className = 'selected';
    for (const v of [s.name, sourceState(s.state), s.reach, s.reach === '0' ? '—' : duration(s.offset_s), s.reach === '0' ? '—' : duration(s.error_s), (() => {const stats=(d.source_stats||[]).find(row=>row.name===s.name);return stats&&s.reach!=='0'?`${number(stats.stddev_ns)} ns`:'—';})()]) {
      const td = document.createElement('td'); td.textContent = v; tr.append(td);
    }
    $('sources').append(tr);
  }
  if (!d.sources.length) { const tr = document.createElement('tr'), td = document.createElement('td'); td.colSpan = 6; td.textContent = 'No source observations available'; tr.append(td); $('sources').append(tr); }
  text('tracking', (d.tracking ? JSON.stringify(d.tracking,null,2) : d.observations.tracking.text) || d.observations.tracking.error || 'Unavailable');
  text('transport', r.transport);
  const fit=rx.fit || {};
  facts('receiver-facts', [['Telemetry age', ago(r.age_s)], ['Firmware', rx.version || 'Unknown'], ['Selected BLE parent', parent(d,rx)], ['Received / authenticated', `${number(rx.rx)} / ${number(rx.authenticated)}`], ['PPS emitted by nRF', number(rx.pps_emitted)], ['PPS captured by Pi', number(p.sequence)], ['PPS route', 'P1.02 → GPIO16'], ['Clock offer', rx.offer_valid ? 'Valid' : 'Unavailable'], ['Receiver fit', fit.locked ? 'Tracking' : fit.samples ? 'Acquiring' : 'Unknown']]);
  const t=rx.transport || [], a=rx.acquisition || [], radio=rx.radio || [], conn=rx.connection || [];
  facts('radio-facts', [['Capabilities seen / authenticated', `${number(t[10])} / ${number(t[11])}`], ['Admitted timing context', a[8]===1 ? 'Yes' : 'No'], ['Periodic sync', a[7]===1 ? 'Established' : 'Unavailable'], ['Control bindings',number(t[2])], ['Rejected timing packets',number(rx.rejected)], ['Radio completions / bad CRC',`${number(radio[0])} / ${number(radio[9])}`], ['PPS arm attempts rejected',number(rx.pps_rejected)], ['Fit residual (internal)',fit.rms_valid ? `${number(fit.rms_ns)} ns` : 'Unknown'], ['Correction age',fit.age_valid ? `${number(fit.age_ms)} ms` : 'Unknown'], ['Last PPS arm lead', rx.pps_plan?.[0] ? `${number(rx.pps_plan[0])} ns` : 'Unknown'], ['Map age at PPS arm', rx.pps_plan?.[0] ? `${number(rx.pps_plan[1])} µs` : 'Unknown'], ['Last control error (history)', conn[8] ? `Code ${conn[8]} at ${number(conn[12]/1000)} s uptime` : 'None recorded']]);
  text('receiver-error', r.error || (r.fresh ? 'Identity-bound telemetry received from the HAT.' : 'Receiver observation is stale; inspect its power and diagnostic connection.'));
  text('diagnostics', r.data ? JSON.stringify(r.data, null, 2) : 'No bound UART record received. The installed firmware may still require its UART diagnostics update.');
  text('ntp-status', `chrony: ${states['chrony.service'] || 'unknown'} · ${stale ? 'Last observed' : c.complete ? 'PToB PPS disciplined' : c.chrony_holdover ? 'PTOB holdover' : 'Current PToB timing unavailable'}`);
  text('wifi-status', `PTPd: ${states['ptob-ptp-wifi.service'] || 'unknown'} · ${stale ? 'Last observed' : c.complete ? 'PToB timing current' : 'Current PToB timing unavailable'}`);
  const guard = d.clock_chain?.server;
  text('ethernet-status', `${d.ethernet_link ? 'Link up' : d.ethernet_link === false ? 'No cable / link down' : 'Link unknown'} · ${stale || !guard?.fresh ? 'Guard observation stale or unavailable' : guard.ready && guard.running ? 'PTP child ready' : 'PTP child withheld: ' + (guard.reason || 'Not ready')}`);
  text('ptp', d.observations.ptp.text || d.observations.ptp.error || 'Unavailable');
  const events=(r.events || []).slice(-12).reverse(); $('events').replaceChildren();
  for (const e of events) { const tr=document.createElement('tr'); for (const value of [`${(e.ms/1000).toFixed(1)} s`,device(d,e.peer), eventNames[e.kind] || `Event ${e.kind}`,e.kind===13 && e.result===22 ? '0x16 · local close' : e.result,`${e.detail} / ${e.a} / ${e.b}${e.missed ? ` · ${e.missed} missed` : ''}`]) { const td=document.createElement('td'); td.textContent=value; tr.append(td); } $('events').append(tr); }
  if (!events.length) { const tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=5;td.textContent='No event records in this boot';tr.append(td);$('events').append(tr); }
  if (d.wire_checks) { const w=d.wire_checks; text('check-time',new Date(w.utc).toLocaleString()); text('wire-evidence',`NTP through Tailscale: ${w.ntp.valid}/${w.ntp.total} replies, stratum ${w.ntp.stratum}, refid ${w.ntp.refid}. Wi-Fi PTP: ${w.ptp.pairs} Sync/FollowUp pairs and ${w.ptp.announces} Announce packets. Plain Wi-Fi UDP/123 requests did not reach the Pi; that path is not gateway proof. These are saved checks, not continuous probes.`); }
  text('updated', 'Automatic refresh every 2 seconds');
}
async function refresh() {
  if (busy) return; busy = true; $('refresh').disabled = true;
  try {
    const response = await fetch('/api/status', {cache:'no-store', signal:AbortSignal.timeout(6000)});
    const data = await response.json(); if (!response.ok) throw Error(data.error || 'Device service unavailable');
    render(data);
  } catch (e) {
    text('headline', 'Dashboard connection lost'); text('detail', e.message + '. Reconnect to Tailscale and retry.');
    $('signal').className = 'signal bad';
    text('age', 'Previous observations must be treated as stale');
    for (const id of ['receiver','pps','clock','output']) stage(id, 'wait', 'Stale', 'Waiting for a fresh device sample');
    if(lastData)sourceReport(lastData,true);
    text('source-state','Stale observation');text('source-summary','Dashboard connection lost. Source details below are last observed; current GM and admission are unknown.');
    text('updated', 'Retrying automatically');
  } finally { busy = false; $('refresh').disabled = false; }
}
$('refresh').addEventListener('click', refresh);
TimingCharts.start();
refresh(); setInterval(refresh, 2000);
