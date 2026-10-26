// Nearest-rank quantiles, per-frame quality, and serial encoder + leaky-bucket link.
import fs from 'node:fs';
import {root,out,parseCSV} from './experiment.mjs';
export function pct(a,p){if(!a.length)return null;const s=a.slice().sort((x,y)=>x-y);return s[Math.max(0,Math.ceil(s.length*p)-1)];}
const mean=a=>a.length?a.reduce((x,y)=>x+y,0)/a.length:null;
function nums(line){return Object.fromEntries([...line.matchAll(/([A-Za-z_]+):([\d.e+\-]+|inf)/g)].map(m=>[m[1],m[2]==='inf'?Infinity:+m[2]]));}
export function analyze(c){
  const base=`${out}/${c.name}`;
  if(c.status!==0 || !fs.existsSync(base+'-frames.csv'))return null;
  const frames=parseCSV(fs.readFileSync(base+'-frames.csv','utf8')).map(f=>Object.fromEntries(Object.entries(f).map(([k,v])=>[k,+v])));
  const ps=fs.existsSync(base+'-psnr.log')?fs.readFileSync(base+'-psnr.log','utf8').trim().split(/\r?\n/).filter(l=>l.startsWith('n:')).map(nums):[];
  const ss=fs.existsSync(base+'-ssim.log')?fs.readFileSync(base+'-ssim.log','utf8').trim().split(/\r?\n/).filter(l=>l.startsWith('n:')).map(nums):[];
  const qlog=fs.existsSync(base+'-quality.log')?fs.readFileSync(base+'-quality.log','utf8'):'';
  const py=qlog.match(/PSNR y:([\d.]+|inf)/),sy=qlog.match(/SSIM Y:([\d.]+|inf)/);
  const enc=frames.map(f=>f.latency_ms),sizes=frames.map(f=>f.bytes),period=1000/c.fps;
  let skipRun=0,maxSkipRun=0;for(const f of frames){skipRun=f.bytes===0?skipRun+1:0;maxSkipRun=Math.max(maxSkipRun,skipRun);}
  const results={name:c.name,scenario:c.scenario,backend:c.backend,fps:c.fps,kbps:c.kbps??12000,mode:c.mode??(c.backend==='vpl'?'cbr':'openh264'),frames:frames.length,
    quality_frames:ps.length,skipped:frames.filter(f=>f.bytes===0).length,max_skip_run:maxSkipRun,held_gap_ms:maxSkipRun*period,delivered_fps:c.fps*frames.filter(f=>f.bytes>0).length/frames.length,enc50:pct(enc,.5),enc80:pct(enc,.8),enc95:pct(enc,.95),enc99:pct(enc,.99),encmax:pct(enc,1),
    deadline_miss_pct:100*enc.filter(e=>e>period).length/enc.length,mbps:sizes.reduce((a,b)=>a+b,0)*8*c.fps/(frames.length*1e6),
    bytes99:pct(sizes,.99),bytesmax:pct(sizes,1),size_ratio99:pct(sizes,.99)*8*c.fps/((c.kbps??12000)*1000),
    idr_count:frames.filter(f=>c.backend==='vpl'?(f.frame_type&128):f.frame_type===1).length,
    b_count:c.backend==='vpl'?frames.filter(f=>f.frame_type&4).length:0,
    psnr_y:py?+py[1]:null,ssim_y:sy?+sy[1]:null,psnr_y01:pct(ps.map(x=>x.psnr_y),.01),ssim_y01:pct(ss.map(x=>x.Y),.01)};
  let finish=0;const prevDepart={8:0,12:0,16:0},wireDepart={8:0,12:0,16:0},wireScore={8:[],12:[],16:[]},wireCapture={8:[],12:[],16:[]},wait={8:[],12:[],16:[]},total={8:[],12:[],16:[]},score={8:[],12:[],16:[]},capture={8:[],12:[],16:[]},encwait=[];
  const metrics=[];
  for(let i=0;i<frames.length;i++){
    const f=frames[i],source=f.input*period,start=Math.max(source,finish);finish=start+f.latency_ms;
    const row={input:f.input,encode_ms:f.latency_ms,bytes:f.bytes,frame_type:f.frame_type,size_budget_ratio:f.bytes*8*c.fps/((c.kbps??12000)*1000),psnr_y:ps[i]?.psnr_y??'',ssim_y:ss[i]?.Y??'',encoder_input_wait_ms:start-source};encwait.push(start-source);
    for(const R of [8,12,16]){
      const w=Math.max(0,prevDepart[R]-finish),n=w+f.bytes*8/(R*1000);prevDepart[R]=finish+n;
      wait[R].push(w);total[R].push(n);score[R].push(f.latency_ms+n);capture[R].push(start-source+f.latency_ms+n);
      row[`wait_${R}_ms`]=w;row[`network_${R}_ms`]=n;row[`encode_network_${R}_ms`]=f.latency_ms+n;row[`source_drain_${R}_ms`]=start-source+f.latency_ms+n;
      const wn=Math.max(0,wireDepart[R]-finish)+1.05*f.bytes*8/(R*1000);wireDepart[R]=finish+wn;wireScore[R].push(f.latency_ms+wn);wireCapture[R].push(start-source+f.latency_ms+wn);row[`wire_encode_network_${R}_ms`]=f.latency_ms+wn;
    }
    metrics.push(row);
  }
  results.encoder_wait99=pct(encwait,.99);results.encoder_waitmax=pct(encwait,1);
  for(const R of [8,12,16]){results[`wait${R}_99`]=pct(wait[R],.99);results[`network${R}_99`]=pct(total[R],.99);results[`network${R}_max`]=pct(total[R],1);results[`score${R}_50`]=pct(score[R],.5);results[`score${R}_80`]=pct(score[R],.8);results[`score${R}_99`]=pct(score[R],.99);results[`score${R}_max`]=pct(score[R],1);results[`capture${R}_99`]=pct(capture[R],.99);results[`wireScore${R}_99`]=pct(wireScore[R],.99);results[`wireScore${R}_max`]=pct(wireScore[R],1);results[`wireCapture${R}_99`]=pct(wireCapture[R],.99);}
  const keys=Object.keys(metrics[0]);fs.writeFileSync(base+'-metrics.csv',keys.join(',')+'\n'+metrics.map(r=>keys.map(k=>r[k]).join(',')).join('\n')+'\n');
  fs.writeFileSync(base+'-summary.json',JSON.stringify(results,null,2));return results;
}
const all=[];
for(const file of fs.readdirSync(out).filter(x=>x.endsWith('-run.json'))){const c=JSON.parse(fs.readFileSync(out+'/'+file));const r=analyze(c);if(r)all.push(r);}
if(all.length){const keys=Object.keys(all[0]);fs.writeFileSync(out+'/summary.csv',keys.join(',')+'\n'+all.map(r=>keys.map(k=>r[k]).join(',')).join('\n')+'\n');fs.writeFileSync(out+'/summary.json',JSON.stringify(all,null,2));}
if(process.argv.includes('--show'))for(const s of ['scroll','natural','video','game']){
  console.log('\n'+s);for(const r of all.filter(r=>r.scenario===s).sort((a,b)=>a.score12_99-b.score12_99))console.log(`${r.name.padEnd(42)} ${r.mbps.toFixed(2)} Mbps  enc99 ${r.enc99.toFixed(1)}  E+N99 ${r.score12_99.toFixed(1)}  PSNR ${r.psnr_y?.toFixed(2)} SSIM ${r.ssim_y?.toFixed(5)} maxKB ${(r.bytesmax/1000).toFixed(1)} ew99 ${r.encoder_wait99.toFixed(1)}`);
}
