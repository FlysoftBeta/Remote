import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
import {root,out,ff} from './experiment.mjs';
export function roi(name){
  const c=JSON.parse(fs.readFileSync(`${out}/${name}-run.json`));if(c.status!==0)return;
  const crop=c.scenario==='natural'?'1920:802:0:138':c.scenario==='scroll'?'700:1080:250:0':null;if(!crop)return;
  if(fs.existsSync(`${out}/${name}-roi.json`))return;
  const filter=`[0:v]setpts=N/(${c.fps}*TB),format=yuv420p,crop=${crop},split=2[d1][d2];[1:v]setpts=N/(${c.fps}*TB),format=yuv420p,crop=${crop},split=2[s1][s2];[d1][s1]psnr=stats_file=${name}-roi-psnr.log[p];[d2][s2]ssim=stats_file=${name}-roi-ssim.log[s]`;
  const fd=fs.openSync(`${out}/${name}-roi-quality.log`,'w');
  const p=spawnSync(ff,['-hide_banner','-y','-threads','4','-r',String(c.fps),'-i',`${name}.h264`,'-threads','1','-f','rawvideo','-pixel_format','nv12','-video_size','1920x1080','-framerate',String(c.fps),'-i',c.input,'-filter_complex_threads','4','-filter_complex',filter,'-map','[p]','-map','[s]','-frames:v',String(c.frames),'-f','null','-'],{cwd:out,stdio:['ignore',fd,fd],timeout:180000});fs.closeSync(fd);
  if(p.status!==0)throw Error('ROI quality failed '+name);
  const log=fs.readFileSync(`${out}/${name}-roi-quality.log`,'utf8');
  const result={name,crop,psnr_y:+log.match(/PSNR y:([\d.]+)/)[1],ssim_y:+log.match(/SSIM Y:([\d.]+)/)[1]};
  fs.writeFileSync(`${out}/${name}-roi.json`,JSON.stringify(result,null,2));console.log(result);
}
for(const name of process.argv.slice(2))roi(name);
