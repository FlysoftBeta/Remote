// Sequential encoder + decoded-reference quality experiments. No Chromium builds.
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
export const root='C:/src/tmp/t1-encoder-tuning',out=root+'/results';
process.env.TEMP=root;process.env.TMP=root;
export const ff=root+'/bin/ffmpeg/'+fs.readdirSync(root+'/bin/ffmpeg').find(x=>x.startsWith('ffmpeg-'))+'/bin/ffmpeg.exe';
const exe=process.env.TUNING_EXE??root+'/build/tune.exe';
export function parseCSV(text){const lines=text.trim().split(/\r?\n/),keys=lines.shift().split(',');return lines.filter(Boolean).map(l=>Object.fromEntries(l.split(',').map((v,i)=>[keys[i],v])));}
export function run(c){
  const fps=c.fps??60,frames=c.frames??fps*10,backend=c.backend??'vpl';
  const input=c.input??`${root}/media/${c.scenario}-${fps}.nv12`;
  const args=Object.entries({...c,input,frames,fps,backend}).filter(([k])=>!['scenario','repeat','qualityCheck'].includes(k)).flatMap(([k,v])=>['--'+k,String(v)]);
  const name=c.name;
  let status=0;
  if(!fs.existsSync(`${out}/${name}-run.json`)){
    const log=fs.openSync(`${out}/${name}-encode.log`,'w');
    const t=Date.now(),p=spawnSync(exe,args,{stdio:['ignore',log,log],timeout:180000});fs.closeSync(log);status=p.status;
    fs.writeFileSync(`${out}/${name}-run.json`,JSON.stringify({...c,input,frames,fps,backend,status,executable:exe,elapsed_ms:Date.now()-t,started:new Date(t).toISOString(),args},null,2));
  }else status=JSON.parse(fs.readFileSync(`${out}/${name}-run.json`)).status;
  if(status!==0){console.log(`${name}: encoder exit ${status}`);return;}
  if(c.qualityCheck===false){console.log(`${name}: encoded`);return;}
  if(!fs.existsSync(`${out}/${name}-quality.json`)){
    const rows=parseCSV(fs.readFileSync(`${out}/${name}-frames.csv`,'utf8'));
    const skips=rows.filter(r=>+r.bytes===0).length;
    let decodedInput=['-threads','4','-r',String(fps),'-i',`${name}.h264`];
    if(skips){
      // Compare every source instant: a skipped picture holds the previous decoded picture.
      const decoded=`${out}/${name}-decoded.nv12`,expanded=`${out}/${name}-held.nv12`;
      const fd=fs.openSync(`${out}/${name}-expand.log`,'w');
      const p=spawnSync(ff,['-hide_banner','-y','-threads','4','-r',String(fps),'-i',`${out}/${name}.h264`,'-fps_mode','passthrough','-pix_fmt','nv12','-f','rawvideo',decoded],{stdio:['ignore',fd,fd],timeout:180000});fs.closeSync(fd);if(p.status)throw Error('Skip-run decode failed');
      const bytes=1920*1080*3/2;if(fs.statSync(decoded).size!==(rows.length-skips)*bytes)throw Error('Skip-run decoded count mismatch');
      const src=fs.openSync(decoded,'r'),dst=fs.openSync(expanded,'w'),buffer=Buffer.alloc(bytes);let have=false;
      for(const row of rows){if(+row.bytes){if(fs.readSync(src,buffer,0,bytes,null)!==bytes)throw Error('Short decoded frame');have=true;}if(!have)throw Error('First frame skipped');fs.writeSync(dst,buffer);}fs.closeSync(src);fs.closeSync(dst);
      decodedInput=['-threads','1','-f','rawvideo','-pixel_format','nv12','-video_size','1920x1080','-framerate',String(fps),'-i',expanded];
    }
    const filter=`[0:v]setpts=N/(${fps}*TB),format=yuv420p,split=2[d1][d2];[1:v]setpts=N/(${fps}*TB),format=yuv420p,split=2[s1][s2];[d1][s1]psnr=stats_file=${name}-psnr.log:stats_version=2[p];[d2][s2]ssim=stats_file=${name}-ssim.log[s]`;
    const log=fs.openSync(`${out}/${name}-quality.log`,'w'),t=Date.now();
    const p=spawnSync(ff,['-hide_banner','-y',...decodedInput,'-threads','1','-f','rawvideo','-pixel_format','nv12','-video_size','1920x1080','-framerate',String(fps),'-i',input,'-filter_complex_threads','4','-filter_complex',filter,'-map','[p]','-map','[s]','-frames:v',String(frames),'-f','null','-'],{cwd:out,stdio:['ignore',log,log],timeout:180000});fs.closeSync(log);
    fs.writeFileSync(`${out}/${name}-quality.json`,JSON.stringify({status:p.status,elapsed_ms:Date.now()-t,filter,skipped_source_frames:skips,skip_quality:skips?'hold previous decoded frame':'none'},null,2));
    if(p.status!==0)throw Error(`${name} quality failed ${p.status}`);
  }
  const f=parseCSV(fs.readFileSync(`${out}/${name}-frames.csv`,'utf8'));
  const sizes=f.map(x=>+x.bytes).sort((a,b)=>a-b),times=f.map(x=>+x.latency_ms).sort((a,b)=>a-b);
  console.log(`${name}: ${f.length} frames, enc99 ${times[Math.ceil(times.length*.99)-1].toFixed(2)} ms, max ${(sizes.at(-1)/1000).toFixed(1)} KB`);
}
const stage=process.argv[2];
if(stage==='initial'){
  for(const scenario of ['scroll','natural','video','game']){
    for(const fps of [60,30])for(const kbps of (fps===60?[8000,12000,16000]:[12000]))for(const mode of ['cbr','vbr']){
      run({name:`${scenario}-${fps}-${kbps}-${mode}`,scenario,fps,kbps,mode,...(mode==='vbr'?{ldb:1,cap:1.5}:{})});
    }
  }
}
