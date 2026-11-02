// Copyright 2026 Jonathan Yantis. MIT. No external assets or telemetry.
'use strict';
const TimingCharts = (() => {
  const NS='http://www.w3.org/2000/svg';
  const specs=[
    ['chart-offset',[['source_offset_ns','PTOB adjusted','indigo'],['tracking_offset_ns','chrony last offset','good']],'ns'],
    ['chart-rms',[['fit_rms_ns','nRF fit RMS','indigo'],['tracking_rms_ns','chrony RMS','good'],['source_stddev_ns','PTOB std dev','ochre']],'ns'],
    ['chart-pps',[['pps_phase_ns','PPS phase','indigo'],['pps_period_ns','Period error','good']],'ns'],
    ['chart-drift',[['fit_drift_ppb','Receiver drift','indigo']],'ppb']
  ];
  const labels={source_offset_ns:'PTOB adjusted sample (ns)',tracking_offset_ns:'chrony last offset (ns)',
    source_stddev_ns:'PTOB source std dev (ns)',fit_rms_ns:'nRF internal fit RMS (ns)',pps_phase_ns:'Pi PPS phase (ns)',
    pps_period_ns:'Pi PPS period error (ns)',query_ms:'Collector queries (ms)',uart_age_ms:'UART record age (ms)'};
  let data=null, busy=false, generation=0;
  const inspections=new Map();
  const format=n=>Number.isFinite(n)?n.toLocaleString(undefined,{maximumFractionDigits:0}):'—';
  function svg(tag,attrs={},value) { const e=document.createElementNS(NS,tag);for(const [k,v] of Object.entries(attrs)) e.setAttribute(k,v);if(value!==undefined)e.textContent=value;return e; }
  function draw(id,series,unit) {
    const figure=document.getElementById(id),box=figure.querySelector('.plot'),readout=figure.querySelector('.readout');
    const pinned=inspections.get(id), rows=data.points.slice();
    // Chart reduction can omit a previously inspected point on a later poll.
    // Keep that real observation while it remains in the selected time range.
    if(pinned&&rows.length&&pinned.at_ms>=rows.at(-1).at_ms-data.window_s*1000&&
       !rows.some(r=>r.at_ms===pinned.at_ms)){rows.push(pinned);rows.sort((a,b)=>a.at_ms-b.at_ms);}
    const W=Math.max(280,box.clientWidth),H=220,left=64,right=W-14,top=16,bottom=181;
    const hadFocus=box.contains(document.activeElement);
    box.replaceChildren();box.removeAttribute('tabindex');
    const values=rows.flatMap(r=>series.map(s=>r[s[0]]).filter(Number.isFinite));
    if (!values.length) { const p=document.createElement('p');p.className='plot-empty';p.textContent='No valid samples in this window. Check source validity and telemetry freshness.';box.append(p);readout.textContent='No exact value available in the selected window.';inspections.delete(id);if(hadFocus){box.tabIndex=-1;box.focus({preventScroll:true});}return; }
    let low=Math.min(...values),high=Math.max(...values);
    if (id!=='chart-drift') {low=Math.min(0,low);high=Math.max(0,high);}
    const pad=Math.max(10,(high-low)*.1);low-=pad;high+=pad;
    const end=rows.at(-1).at_ms,start=Math.max(rows[0].at_ms,end-data.window_s*1000),span=Math.max(1000,end-start);
    const x=r=>left+(r.at_ms-start)/span*(right-left),y=v=>bottom-(v-low)/(high-low)*(bottom-top);
    const el=svg('svg',{viewBox:`0 0 ${W} ${H}`,role:'img',tabindex:'0','aria-label':figure.querySelector('h3').textContent+`. Values in ${unit}. Use left and right arrows to inspect samples; End follows the newest sample.`});
    el.append(svg('title',{},`${figure.querySelector('h3').textContent}, ${unit}`));
    for(let i=0;i<4;i++) {const v=low+(high-low)*i/3,cy=y(v);el.append(svg('line',{x1:left,x2:right,y1:cy,y2:cy,class:'chart-grid'}),svg('text',{x:left-8,y:cy+4,'text-anchor':'end',class:'chart-tick'},format(v)));}
    el.append(svg('text',{x:8,y:12,class:'chart-tick'},unit));
    for(let i=0;i<3;i++){const at=start+span*i/2;el.append(svg('text',{x:left+(right-left)*i/2,y:207,'text-anchor':i===0?'start':i===2?'end':'middle',class:'chart-tick'},new Date(at).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit',second:'2-digit'})));}
    for (const [key,label,color] of series) {
      let path='',run=[];const isolated=[];
      function finish(){if(run.length===1)isolated.push(run[0]);run=[];}
      for(const r of rows) {const valid=Number.isFinite(r[key]);if(!valid){finish();continue;}
        const previous=run.at(-1);
        if(previous&&previous.segment!==r.segment)finish();
        path+=`${run.length?'L':'M'}${x(r).toFixed(2)},${y(r[key]).toFixed(2)} `;run.push(r);}
      finish();
      el.append(svg('path',{d:path,class:`chart-line series-${color}`,fill:'none','aria-label':label}));
      // An isolated valid observation is a point, not an invisible SVG move.
      // Keep real gaps open instead of joining across missing observations.
      for(const r of isolated)el.append(svg('circle',{cx:x(r),cy:y(r[key]),r:2.5,
        class:`chart-line series-${color}`,fill:'none','aria-label':`${label}: ${format(r[key])} ${unit}`}));
    }
    const cursor=svg('line',{y1:top,y2:bottom,class:'chart-cursor',visibility:'hidden'});el.append(cursor);box.append(el);
    const legend=document.createElement('ul');legend.className='chart-legend';
    for(const [,label,color] of series){const li=document.createElement('li'),mark=document.createElement('span');mark.className=`legend-mark series-${color}`;li.append(mark,document.createTextNode(label));legend.append(li);}box.append(legend);
    let selected=rows.length-1;
    if(pinned!==undefined){selected=0;for(let i=1;i<rows.length;i++)if(Math.abs(rows[i].at_ms-pinned.at_ms)<Math.abs(rows[selected].at_ms-pinned.at_ms))selected=i;}
    function inspect(index,pin=true){selected=Math.max(0,Math.min(rows.length-1,index));const r=rows[selected],cx=x(r);if(pin)inspections.set(id,r);cursor.setAttribute('x1',cx);cursor.setAttribute('x2',cx);cursor.setAttribute('visibility','visible');readout.textContent=new Date(r.at_ms).toLocaleTimeString()+' · '+series.map(([k,label])=>`${label}: ${format(r[k])} ${unit}`).join(' · ')+` · Source: ${r.selected||'none'}`+((id==='chart-offset'||id==='chart-rms')&&Number.isFinite(r.source_sample_age_s)?` · chrony sample age ${format(r.source_sample_age_s)} s; poll ${format(r.source_poll_s)} s`:'');}
    function point(e){const at=start+((e.clientX-el.getBoundingClientRect().left)-left)/(right-left)*span;let nearest=0;for(let i=1;i<rows.length;i++)if(Math.abs(rows[i].at_ms-at)<Math.abs(rows[nearest].at_ms-at))nearest=i;inspect(nearest);}
    el.addEventListener('pointermove',point);el.addEventListener('pointerdown',point);
    el.addEventListener('keydown',e=>{if(e.key==='ArrowLeft'||e.key==='ArrowRight'){e.preventDefault();inspect(selected+(e.key==='ArrowLeft'?-1:1));}else if(e.key==='End'){e.preventDefault();inspections.delete(id);inspect(rows.length-1,false);}});
    inspect(selected,false);if(hadFocus)el.focus({preventScroll:true});
  }
  function render(){
    if(!data)return;
    for(const spec of specs)draw(...spec);
    const body=document.getElementById('window-stats');body.replaceChildren();
    for(const [key,label] of Object.entries(labels)){const s=data.statistics[key]||{n:0},tr=document.createElement('tr');for(const value of [label,format(s.n),format(s.mean),format(s.rms),format(s.stddev),format(s.p95_abs),format(s.p99_abs),s.n?`${format(s.min)} / ${format(s.max)}`:'—']){const td=document.createElement('td');td.textContent=value;tr.append(td);}body.append(tr);}
    const avg=data.averaging;
    const averages=document.getElementById('averaging-stats');averages.replaceChildren();
    if(avg){
      document.getElementById('averaging-summary').textContent=`${format(avg.unique_edges)} unique eligible edges in ${format(avg.segments)} contiguous segments · lag-1 correlation: ${Number.isFinite(avg.lag1)?avg.lag1.toFixed(3):'unavailable'}. Missing or repeated snapshot edges do not increase the block population.`;
      const fractional=n=>Number.isFinite(n)?n.toLocaleString(undefined,{minimumFractionDigits:2,maximumFractionDigits:2}):'—';
      for(const b of avg.blocks){const tr=document.createElement('tr');for(const v of [`${b.seconds} s`,format(b.n),format(b.used_edges),fractional(b.mean),fractional(b.rms),fractional(b.stddev),fractional(b.p95_abs)]){const td=document.createElement('td');td.textContent=v;tr.append(td);}averages.append(tr);}
    }
    const age=data.sample_age_s;
    document.getElementById('history-status').textContent=data.population?`${format(data.population)} snapshots · ${data.gaps} context changes / gaps · newest ${Math.max(0,Math.round(age))} s ago${(age<0||age>20)?' · History is stale; check the collector.':''}`:'History is warming up. First disk batch appears within 10 seconds.';
  }
  async function refresh(){
    if(busy)return;busy=true;const request=++generation,window=document.getElementById('window').value;
    try{const r=await fetch(`/api/history?seconds=${window}`,{cache:'no-store',signal:AbortSignal.timeout(6000)}),value=await r.json();if(!r.ok)throw Error(value.error||'History unavailable');if(request===generation){data=value;render();}}
    catch(e){document.getElementById('history-status').textContent=`${e.message}. Retrying automatically; previous charts are historical.`;}
    finally{busy=false;}
  }
  function start(){document.getElementById('window').addEventListener('change',()=>{generation++;inspections.clear();document.getElementById('export-history').href='/api/history/export?seconds='+document.getElementById('window').value;busy=false;refresh();});
    let timer;window.addEventListener('resize',()=>{clearTimeout(timer);timer=setTimeout(render,150);});refresh();setInterval(refresh,10000);}
  return {start,refresh};
})();
