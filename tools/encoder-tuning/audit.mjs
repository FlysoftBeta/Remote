import fs from 'node:fs';
import crypto from 'node:crypto';
import {root,out,parseCSV} from './experiment.mjs';
const runs=fs.readdirSync(out).filter(f=>f.endsWith('-run.json')).map(f=>JSON.parse(fs.readFileSync(out+'/'+f)));
const errors=[],unsupported=[],successful=[];let sourceFrames=0,encodedFrames=0;
for(const c of runs){
  if(c.status!==0){unsupported.push({name:c.name,status:c.status});continue;}
  const frames=parseCSV(fs.readFileSync(`${out}/${c.name}-frames.csv`,'utf8'));
  sourceFrames+=frames.length;encodedFrames+=frames.filter(f=>+f.bytes>0).length;
  const q=JSON.parse(fs.readFileSync(`${out}/${c.name}-quality.json`));
  const ps=fs.readFileSync(`${out}/${c.name}-psnr.log`,'utf8').split(/\r?\n/).filter(l=>l.startsWith('n:')).length;
  const ss=fs.readFileSync(`${out}/${c.name}-ssim.log`,'utf8').split(/\r?\n/).filter(l=>l.startsWith('n:')).length;
  const b=JSON.parse(fs.readFileSync(`${out}/${c.name}-bitstream.json`));
  const failed=frames.length!==c.frames||frames.some(f=>+f.encode_status<0||+f.sync_status!==0)||q.status!==0||ps!==c.frames||ss!==c.frames||b.invalid||b.noSlice||b.wrongProfile||b.bSlices||b.length!==b.sum_frame_bytes;
  if(failed)errors.push(c.name);else successful.push(c.name);
}
const media=[];
for(const file of fs.readdirSync(root+'/media')){
  const stat=fs.statSync(root+'/media/'+file);if(!stat.isFile())continue;
  const entry={file,bytes:stat.size};
  if(file.endsWith('.nv12')){entry.frames=stat.size/(1920*1080*3/2);entry.fps=file.includes('-30')?30:60;if(!Number.isInteger(entry.frames))errors.push('Short media: '+file);}
  else if(/\.(mkv|png|html)$/.test(file))entry.sha256=crypto.createHash('sha256').update(fs.readFileSync(root+'/media/'+file)).digest('hex');
  media.push(entry);
}
const result={checked:new Date().toISOString(),runs:runs.length,successful:successful.length,unsupported,sourceFrames,encodedFrames,errors,media};
fs.writeFileSync(out+'/validation.json',JSON.stringify(result,null,2));console.log(JSON.stringify({...result,media:undefined},null,2));if(errors.length)process.exitCode=1;
