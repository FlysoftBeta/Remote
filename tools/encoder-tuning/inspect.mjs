import fs from 'node:fs';
import {out,parseCSV} from './experiment.mjs';
const records=[];
function headerReader(b,start,end){
  const rbsp=[];let zeros=0;for(let k=start+1;k<Math.min(end,start+64);k++){if(zeros>=2&&b[k]===3){zeros=0;continue;}rbsp.push(b[k]);zeros=b[k]===0?zeros+1:0;}
  let pos=0;const bit=()=>{if(pos>=rbsp.length*8)throw Error('Short slice header');return (rbsp[pos>>3]>>(7-(pos++&7)))&1;};
  const ue=()=>{let n=0;while(!bit()){if(++n>30)throw Error('Invalid Exp-Golomb');}let v=0;for(let j=0;j<n;j++)v=v*2+bit();return 2**n-1+v;};
  return {ue,bit};
}
function sliceType(b,start,end){const r=headerReader(b,start,end);r.ue();return r.ue()%5;}
function ppsCabac(b,start,end){const r=headerReader(b,start,end);r.ue();r.ue();return r.bit();}
for(const file of fs.readdirSync(out).filter(x=>x.endsWith('-run.json'))){
  const c=JSON.parse(fs.readFileSync(out+'/'+file));if(c.status!==0)continue;
  const b=fs.readFileSync(`${out}/${c.name}.h264`),frames=parseCSV(fs.readFileSync(`${out}/${c.name}-frames.csv`,'utf8'));
  let offset=0,invalid=0,noSlice=0,wrongProfile=0,bSlices=0;const types={},sps=[],sliceCounts=[],entropyModes=new Set();let fillerBytes=0;
  for(const frame of frames){
    const length=+frame.bytes;if(!length)continue;const end=offset+length;const starts=[];
    for(let i=offset;i+3<end;){let p=0;if(b[i]===0&&b[i+1]===0&&b[i+2]===1)p=3;else if(b[i]===0&&b[i+1]===0&&b[i+2]===0&&b[i+3]===1)p=4;
      if(p){starts.push({prefix:i,start:i+p});i+=p;}else i++;
    }
    let slices=0;for(let j=0;j<starts.length;j++){
      const p=starts[j],e=starts[j+1]?.prefix??end,t=b[p.start]&31;types[t]=(types[t]??0)+1;if(b[p.start]&128)invalid++;
      if(t===1||t===5){slices++;if(sliceType(b,p.start,e)===1)bSlices++;}if(t===12)fillerBytes+=e-p.prefix;
      if(t===8)entropyModes.add(ppsCabac(b,p.start,e));
      if(t===7){const profile=b[p.start+1],constraints=b[p.start+2],level=b[p.start+3];sps.push({frame:+frame.input,profile,constraints,level,profile_level_id:b.subarray(p.start+1,p.start+4).toString('hex')});if((c.backend==='openh264'||(c.profile??'cb')==='cb')&&(profile!==66||!(constraints&64)))wrongProfile++;}
    }
    if(!slices)noSlice++;sliceCounts.push(slices);offset=end;
  }
  const params=fs.readFileSync(`${out}/${c.name}-params.csv`,'utf8').split(/\r?\n/).find(l=>l.startsWith('readback,'))??'';
  const cap=+(params.match(/MaxFrameSize=(\d+);/)?.[1]??0),idr=f=>c.backend==='vpl'?(+f.frame_type&128):+f.frame_type===1;
  const r={name:c.name,length:b.length,sum_frame_bytes:offset,invalid,noSlice,wrongProfile,bSlices,ppsEntropyCodingModes:[...entropyModes],sliceMin:Math.min(...sliceCounts),sliceMax:Math.max(...sliceCounts),fillerBytes,fillerPct:100*fillerBytes/b.length,readbackCap:cap,capViolations:cap?frames.filter(f=>+f.bytes>cap).length:0,capViolationMaxRatio:cap?Math.max(...frames.map(f=>+f.bytes))/cap:0,idrMaxBytes:Math.max(0,...frames.filter(idr).map(f=>+f.bytes)),pMaxBytes:Math.max(0,...frames.filter(f=>!idr(f)).map(f=>+f.bytes)),types,sps};
  fs.writeFileSync(`${out}/${c.name}-bitstream.json`,JSON.stringify(r,null,2));records.push(r);
}
fs.writeFileSync(out+'/bitstream-validation.json',JSON.stringify(records,null,2));
console.log(`${records.length} streams inspected; failures: ${records.filter(r=>r.invalid||r.noSlice||r.wrongProfile||r.bSlices||r.length!==r.sum_frame_bytes).length}`);
