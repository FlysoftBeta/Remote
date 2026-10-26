import {run} from './experiment.mjs';
const stage=process.argv[2]??'hardware';
if(stage==='hardware'){
  const candidates=[
    ['vbr1',{mode:'vbr',ldb:1,cap:1}],
    ['vbr15',{mode:'vbr',ldb:1,cap:1.5}],
    ['cbr',{mode:'cbr'}],
  ];
  for(let rep=1;rep<=4;rep++)for(const scenario of ['scroll','natural','game-detail']){
    const rotated=candidates.slice(rep%3).concat(candidates.slice(0,rep%3));
    for(const [tag,opts] of rotated)run({name:`${scenario}-60-10000-final-${tag}-r${rep}`,scenario,fps:60,kbps:10000,repeat:rep,paced:rep>=3?1:0,buffer:42,initial:20,mbbrc:32,...opts});
  }
}
if(stage==='software'){
  for(let rep=1;rep<=4;rep++)for(const scenario of ['scroll','natural','game-detail']){
    const usage=scenario==='game-detail'?'camera':'screen';
    run({name:`${scenario}-60-10000-final-oh-${usage}-r${rep}`,scenario,fps:60,kbps:10000,repeat:rep,paced:rep>=3?1:0,backend:'openh264',usage,threads:4,slices:4,skip:1});
  }
}
if(stage==='extra'){
  for(const profile of ['main','high'])run({name:`game-detail-60-10000-vbr1-${profile}`,scenario:'game-detail',fps:60,kbps:10000,mode:'vbr',ldb:1,cap:1,profile});
  for(const slices of [2])run({name:`scroll-60-10000-vbr1-slices${slices}`,scenario:'scroll',fps:60,kbps:10000,mode:'vbr',ldb:1,cap:1,slices});
  for(const tag of ['cbr','vbr1','vbr15'])run({name:`video-60-10000-${tag}`,scenario:'video',fps:60,kbps:10000,...(tag==='cbr'?{mode:'cbr'}:{mode:'vbr',ldb:1,cap:tag==='vbr1'?1:1.5})});
  for(const usage of ['screen','camera'])run({name:`video-60-10000-oh-${usage}-skip`,scenario:'video',fps:60,kbps:10000,backend:'openh264',usage,threads:4,slices:4,skip:1});
  for(const profile of ['main','high'])for(const kbps of [6500,10000])run({name:`game-60-${kbps}-rescue-${profile}`,scenario:'game',fps:60,kbps,profile});
  for(const kbps of [6500,10000])run({name:`game-60-${kbps}-rescue-vme`,scenario:'game',fps:60,kbps,lp:0});
  for(const scenario of ['scroll','natural','game-detail'])run({name:`${scenario}-60-10000-vbr1-mbbrc`,scenario,fps:60,kbps:10000,mode:'vbr',ldb:1,cap:1,mbbrc:16});
}
