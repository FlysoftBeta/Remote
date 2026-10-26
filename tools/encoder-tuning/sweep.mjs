import {run} from './experiment.mjs';
// Follow-up experiments selected after the fixed-rate first pass.
const stage=process.argv[2]??'hardware';
if(stage==='hardware'){
  for(const scenario of ['scroll','natural','game-detail']){
    const tests=[
      ['vbr-cap1',{mode:'vbr',ldb:1,cap:1}],
      ['vbr-cap2',{mode:'vbr',ldb:1,cap:2}],
      ['vbr-plain',{mode:'vbr'}],
      ['vbr-ldb',{mode:'vbr',ldb:1}],
      ['cbr-cap15',{cap:1.5}],
      ['qvbr26',{mode:'qvbr',quality:26,ldb:1,cap:1.5}],
      ['icq26',{mode:'icq',quality:26}],
      ['tu4',{tu:4}],
      ['vme',{lp:0}],
      ['vbv3',{vbv:3}],
      ['main',{profile:'main'}],
      ['high',{profile:'high'}],
      ['refs3',{refs:3}],
      ['slices4',{slices:4}],
      ['intra-v',{intra:1}],
      ['intra-h',{intra:2}],
      ['idr1s',{gop:60}],
      ['vbr-intra',{mode:'vbr',ldb:1,cap:1.5,intra:1}],
    ];
    for(const [tag,opts] of tests)run({name:`${scenario}-60-12000-${tag}`,scenario,fps:60,kbps:12000,...opts});
  }
}
if(stage==='software'){
  for(const scenario of ['scroll','natural','game-detail'])for(const fps of [60,30]){
    const tests=[
      ['screen-t4-s4',{usage:'screen',threads:4,slices:4}],
      ['camera-t4-s4',{usage:'camera',threads:4,slices:4}],
      ['screen-t1-s1',{usage:'screen',threads:1,slices:1}],
      ['screen-t8-s8',{usage:'screen',threads:8,slices:8}],
      ['screen-quality',{usage:'screen',threads:4,slices:4,rc:0}],
      ['screen-buffer',{usage:'screen',threads:4,slices:4,rc:2}],
    ];
    for(const [tag,opts] of tests)run({name:`${scenario}-${fps}-12000-oh-${tag}`,backend:'openh264',scenario,fps,kbps:12000,...opts});
  }
}
if(stage==='detail-initial'){
  const scenario='game-detail';for(const fps of [60,30])for(const kbps of (fps===60?[8000,12000,16000]:[12000]))for(const mode of ['cbr','vbr'])run({name:`${scenario}-${fps}-${kbps}-${mode}`,scenario,fps,kbps,mode,...(mode==='vbr'?{ldb:1,cap:1.5}:{})});
}
