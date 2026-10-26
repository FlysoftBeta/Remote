import fs from 'node:fs';
import {out,parseCSV} from './experiment.mjs';
import {pct} from './analyze.mjs';
const all=JSON.parse(fs.readFileSync(out+'/summary.json')),groups=new Map();
for(const r of all){const m=r.name.match(/^(scroll|natural|game-detail)-60-10000-final-(.+)-r([1-4])$/),v=r.name.match(/^game-60-(6500|10000)-rescue-vme(?:-r[1-3])?$/),v30=/^game-30-6500-rescue-vme-r[1-8]$/.test(r.name);if(!m&&!v&&!v30)continue;const key=m?m[1]+'-'+m[2]:v30?'game-vme30-6500':'game-vme'+v[1];if(!groups.has(key))groups.set(key,[]);groups.get(key).push(r);}
const combined=[];
for(const [name,runs] of groups){
  if(runs.length!==(name==='game-vme30-6500'?8:4))continue;
  const rows=runs.flatMap(r=>{const manifest=JSON.parse(fs.readFileSync(`${out}/${r.name}-run.json`));return parseCSV(fs.readFileSync(`${out}/${r.name}-metrics.csv`,'utf8')).map(x=>({...Object.fromEntries(Object.entries(x).map(([k,v])=>[k,+v])),run:r.name,paced:manifest.paced===1}));});
  const get=k=>rows.map(x=>x[k]);const n=runs.reduce((s,r)=>s+r.frames,0);
  const range=k=>[Math.min(...runs.map(r=>r[k])),Math.max(...runs.map(r=>r[k]))];
  const result={name,scenario:runs[0].scenario,fps:runs[0].fps,kbps:runs[0].kbps,runs:runs.map(r=>r.name),frames:n,skipped:runs.reduce((s,r)=>s+r.skipped,0),
    enc50:pct(get('encode_ms'),.5),enc80:pct(get('encode_ms'),.8),enc99:pct(get('encode_ms'),.99),encmax:pct(get('encode_ms'),1),
    score12_80:pct(get('encode_network_12_ms'),.8),score12_99:pct(get('encode_network_12_ms'),.99),score12_max:pct(get('encode_network_12_ms'),1),wire12_99:pct(get('wire_encode_network_12_ms'),.99),capture12_99:pct(get('source_drain_12_ms'),.99),
    encoderWait99:pct(get('encoder_input_wait_ms'),.99),bytes99:pct(get('bytes'),.99),bytesmax:pct(get('bytes'),1),
    mbps:runs.reduce((s,r)=>s+r.mbps*r.frames,0)/n,psnr_y:-10*Math.log10(runs.reduce((s,r)=>s+r.frames*10**(-r.psnr_y/10),0)/n),ssim_y:runs.reduce((s,r)=>s+r.frames*r.ssim_y,0)/n,
    enc99Range:range('enc99'),score99Range:range('score12_99'),wire99Range:range('wireScore12_99'),
  };
  for(const R of [8,12,16]){result[`score${R}_99`]=pct(get(`encode_network_${R}_ms`),.99);result[`wire${R}_99`]=pct(get(`wire_encode_network_${R}_ms`),.99);result[`capture${R}_99`]=pct(get(`source_drain_${R}_ms`),.99);}
  for(const paced of [false,true]){const selected=rows.filter(r=>r.paced===paced);result[paced?'pacedEnc99':'unpacedEnc99']=pct(selected.map(r=>r.encode_ms),.99);result[paced?'pacedScore99':'unpacedScore99']=pct(selected.map(r=>r.encode_network_12_ms),.99);}
  combined.push(result);
}
fs.writeFileSync(out+'/final-groups.json',JSON.stringify(combined,null,2));
const f=(v,n=1)=>typeof v==='number'?v.toFixed(n):String(v);
const table=['| Configuration (10 Mbps target / 12 Mbps link) | n | Actual Mbps | Enc P99 ms | E+N P80 / P99 ms | +5% bytes P99 ms | PSNR-Y / SSIM-Y | Max KB |', '|---|---:|---:|---:|---:|---:|---:|---:|'];
for(const r of combined.filter(r=>r.scenario!=='game').sort((a,b)=>a.scenario.localeCompare(b.scenario)||a.score12_99-b.score12_99))table.push(`| ${r.name} | ${r.frames} | ${f(r.mbps,2)} | ${f(r.enc99)} | ${f(r.score12_80)} / ${f(r.score12_99)} | ${f(r.wire12_99)} | ${f(r.psnr_y,2)} / ${f(r.ssim_y,5)} | ${f(r.bytesmax/1000)} |`);
fs.writeFileSync(out+'/final-tables.md',table.join('\n')+'\n');console.log(table.join('\n'));
