import fs from 'node:fs';
import {run,root} from './experiment.mjs';
const stage=process.argv[2]??'headroom';
if(stage==='headroom'){
  for(const scenario of ['scroll','natural','game-detail','game'])for(const kbps of [6500,10000,13500])for(const tag of ['cbr','vbr1','vbr15']){
    const opts=tag==='cbr'?{mode:'cbr'}:{mode:'vbr',ldb:1,cap:tag==='vbr1'?1:1.5};
    run({name:`${scenario}-60-${kbps}-${tag}`,scenario,fps:60,kbps,...opts});
  }
}
if(stage==='scene'){
  const target=root+'/media/cut-60.nv12',chunk=1920*1080*3/2;
  if(!fs.existsSync(target)){
    const output=fs.openSync(target,'w'),buffer=Buffer.alloc(chunk);
    for(const source of ['scroll','game-detail']){const input=fs.openSync(root+`/media/${source}-60.nv12`,'r');for(let i=0;i<300;i++){fs.readSync(input,buffer,0,chunk,i*chunk);fs.writeSync(output,buffer);}fs.closeSync(input);}fs.closeSync(output);
  }
  for(const [tag,opts] of [
    ['cbr',{}],['vbr1',{mode:'vbr',ldb:1,cap:1}],['vbr15',{mode:'vbr',ldb:1,cap:1.5}],
    ['cbr-force',{ 'force-idr':300}],['vbr1-force',{mode:'vbr',ldb:1,cap:1,'force-idr':300}],['vbr15-force',{mode:'vbr',ldb:1,cap:1.5,'force-idr':300}],
    ['cbr-adaptive',{'adaptive-i':1}],['vbr-intra',{mode:'vbr',ldb:1,cap:1,intra:1}],
    ['vbr-ipcaps',{mode:'vbr',ldb:1,'cap-i':4,'cap-p':1,'force-idr':300}],
    ['vbr-ipcaps3vbv',{mode:'vbr',ldb:1,'cap-i':4,'cap-p':1,'force-idr':300,vbv:3}],
  ])run({name:`cut-60-10000-${tag}`,scenario:'cut',fps:60,kbps:10000,...opts});
  for(const scenario of ['scroll','game-detail'])for(const [tag,opts] of [
    ['cbr-idr6vbv',{gop:60,vbv:6}],['cbr-intra6vbv',{intra:1,vbv:6}],['cbr-none6vbv',{vbv:6}],
  ])run({name:`${scenario}-60-12000-${tag}`,scenario,fps:60,kbps:12000,...opts});
}
if(stage==='rescue'){
  for(const [tag,opts] of [
    ['panic',{panic:16}],['no-panic',{panic:32}],['maxqp51',{maxqp:51}],
    ['winbrc',{winbrc:60}],['mbbrc',{mbbrc:16}],['vbv6',{vbv:6}],
    ['main',{profile:'main'}],['high',{profile:'high'}],['vme',{lp:0}],
  ])run({name:`game-60-12000-rescue-${tag}`,scenario:'game',fps:60,kbps:12000,...opts});
  for(const kbps of [6500,10000])for(const mode of ['cbr','vbr'])run({name:`game-30-${kbps}-${mode}`,scenario:'game',fps:30,kbps,mode,...(mode==='vbr'?{ldb:1,cap:1}:{})});
}
if(stage==='software-skip'){
  for(const scenario of ['scroll','natural','game-detail'])for(const usage of ['screen','camera'])run({name:`${scenario}-60-10000-oh-${usage}-skip`,scenario,fps:60,kbps:10000,backend:'openh264',usage,threads:4,slices:4,skip:1});
  for(const scenario of ['natural','game-detail'])for(const fps of [60,30])run({name:`${scenario}-${fps}-8000-oh-camera-skip`,scenario,fps,kbps:8000,backend:'openh264',usage:'camera',threads:4,slices:4,skip:1});
  for(const scenario of ['natural','game-detail'])run({name:`${scenario}-60-10000-oh-camera-timestamp`,scenario,fps:60,kbps:10000,backend:'openh264',usage:'camera',threads:4,slices:4,skip:1,rc:3});
}
if(stage==='software-idr'){
  for(const scenario of ['scroll','natural','game-detail','video'])for(const ratio of [100,200,400])run({name:`${scenario}-60-10000-oh-camera-idr${ratio}`,scenario,fps:60,kbps:10000,backend:'openh264',usage:'camera',threads:4,slices:4,skip:1,'idr-ratio':ratio,'fix-overshoot':1});
  for(const ratio of [100,200])run({name:`game-detail-60-10000-oh-screen-idr${ratio}`,scenario:'game-detail',fps:60,kbps:10000,backend:'openh264',usage:'screen',threads:4,slices:4,skip:1,'idr-ratio':ratio,'fix-overshoot':1});
}
if(stage==='vme-repeat'){
  for(const kbps of [6500,10000])for(let rep=1;rep<=3;rep++)run({name:`game-60-${kbps}-rescue-vme-r${rep}`,scenario:'game',fps:60,kbps,lp:0,repeat:rep,paced:rep>=2?1:0});
}
if(stage==='vme30'){
  for(let rep=1;rep<=8;rep++)run({name:`game-30-6500-rescue-vme-r${rep}`,scenario:'game',fps:30,kbps:6500,lp:0,repeat:rep,paced:rep>=5?1:0});
}
