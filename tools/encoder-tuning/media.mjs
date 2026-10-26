// Deterministic, browser-rendered page and full-motion proxies, 10 seconds each.
import fs from 'node:fs';
import path from 'node:path';
import {spawn, spawnSync} from 'node:child_process';
const root='C:/src/tmp/t1-encoder-tuning',media=root+'/media';
process.env.TEMP=root;process.env.TMP=root;
const bin=fs.readdirSync(root+'/bin/ffmpeg').find(x=>x.startsWith('ffmpeg-'));
const ff=root+'/bin/ffmpeg/'+bin+'/bin/ffmpeg.exe';
export {ff};
function run(args,name){
  const log=fs.openSync(`${root}/${name}.log`,'w');
  const p=spawnSync(ff,['-hide_banner','-y','-threads','4','-filter_threads','4',...args],{stdio:['ignore',log,log]});fs.closeSync(log);
  if(p.status!==0)throw Error(`${name} failed: ${p.status}`);
}
async function page(){
  const text='A cloud browser sends sharp text, diagrams, images and interactive controls. Smooth scrolling must preserve small labels and punctuation while keeping each frame inside its network budget. Measurements need real rendered glyphs rather than abstract stripes.';
  const cards=Array.from({length:36},(_,i)=>`<section><div class="tag">RESEARCH NOTE ${String(i+1).padStart(2,'0')}</div><h2>${['Streaming architecture','Rendering and compositing','Latency measurements','Network adaptation','Release checklist','GPU surface ownership'][i%6]}</h2><p>${text} ${text}</p><div class="cols"><div><h3>Configuration ${i+1}</h3><p>${text}</p><pre>fps: 60\nbitrate: 12000 kbps\nasync_depth: 1\nframe_${i}: ready</pre></div><svg viewBox="0 0 600 250"><defs><linearGradient id="g${i}"><stop stop-color="hsl(${i*29%360} 65% 45%)"/><stop offset="1" stop-color="#67cfb8"/></linearGradient></defs><rect width="600" height="250" fill="url(#g${i})"/>${Array.from({length:20},(_,j)=>`<circle cx="${j*37%600}" cy="${(j*71+i*19)%250}" r="${10+j%30}" fill="white" opacity=".3"/>`).join('')}<path d="M0 200 L80 100 L160 170 L250 30 L330 130 L410 70 L500 150 L600 40" fill="none" stroke="white" stroke-width="5"/><text x="24" y="40" font-size="22" fill="white">Measured frame delivery · ${i+1}</text></svg></div><table><tr><th>Metric</th><th>Target</th><th>Observed</th><th>Status</th></tr><tr><td>Capture → display P80</td><td>100 ms</td><td>Pending</td><td>Measure</td></tr><tr><td>H.264 frame rate</td><td>60 fps</td><td>60 fps</td><td>Ready</td></tr></table></section>`).join('');
  const html=`<!doctype html><meta charset="utf-8"><style>*{box-sizing:border-box}body{margin:0;background:#edf1f6;color:#17253b;font:20px/1.5 'Segoe UI',sans-serif}.top{background:#18283e;color:white;padding:30px 160px}h1{font-size:44px;margin:5px}main{width:1500px;margin:auto}section{background:white;padding:28px 40px;margin:24px 0;border:1px solid #d0d9e3;border-radius:10px}h2{font-size:30px;margin:4px 0}h3{font-size:23px;margin:6px 0}.tag{color:#336bc5;font-size:14px;font-weight:700;letter-spacing:2px}p{margin:10px 0}.cols{display:grid;grid-template-columns:1fr 1fr;gap:30px}svg{width:100%;align-self:center}pre{background:#152338;color:#bcdfef;font:18px/1.6 Consolas;padding:15px}table{border-collapse:collapse;width:100%;font-size:16px;margin-top:16px}td,th{border:1px solid #d6dfe9;padding:6px 12px;text-align:left}th{background:#eef3f8}</style><div class="top"><div>PRTS / ENGINEERING FIELD NOTES</div><h1>Interactive browser streaming</h1><div>Text, charts, code and controls at 1080p</div></div><main>${cards}</main>`;
  fs.writeFileSync(media+'/page.html',html);
  const edge='C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
  const log=fs.openSync(root+'/edge-media.log','w');
  const browser=spawn(edge,['--headless=new','--no-first-run','--no-default-browser-check',`--user-data-dir=${root}/edge-profile`,'--remote-debugging-port=0','--disable-extensions','--disable-background-networking','--renderer-process-limit=1','about:blank'],{stdio:['ignore',log,log]});
  try{
    let port;for(let n=0;n<100;n++){await new Promise(r=>setTimeout(r,100));try{port=fs.readFileSync(root+'/edge-profile/DevToolsActivePort','utf8').split('\n')[0];break;}catch{}}
    const targets=await(await fetch(`http://127.0.0.1:${port}/json`)).json();
    const ws=new WebSocket(targets.find(t=>t.type==='page').webSocketDebuggerUrl);await new Promise(r=>ws.addEventListener('open',r,{once:true}));
    let seq=0;const pending=new Map();ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(Error(JSON.stringify(m.error))):p.resolve(m.result);}};
    const call=(method,params={})=>new Promise((resolve,reject)=>{const id=++seq;pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}));});
    await call('Emulation.setDeviceMetricsOverride',{width:1920,height:1080,deviceScaleFactor:1,mobile:false});
    await call('Page.navigate',{url:'file:///'+media+'/page.html'});await new Promise(r=>setTimeout(r,1500));
    const shot=await call('Page.captureScreenshot',{format:'png',captureBeyondViewport:true,clip:{x:0,y:0,width:1920,height:11000,scale:1}});
    fs.writeFileSync(media+'/page.png',Buffer.from(shot.data,'base64'));await call('Browser.close');ws.close();
  }finally{spawnSync('taskkill',['/PID',String(browser.pid),'/T','/F'],{stdio:'ignore'});fs.closeSync(log);}
}
if(!fs.existsSync(media+'/page.png'))await page();
if(!fs.existsSync(media+'/scroll-60.nv12'))run(['-loop','1','-framerate','60','-i',media+'/page.png','-vf',"crop=1920:1080:0:'min(9000,720*t+180*sin(2*PI*t/3))',scale=1920:1080:out_color_matrix=bt709:out_range=tv,format=nv12",'-frames:v','600','-f','rawvideo',media+'/scroll-60.nv12'],'make-scroll');
if(!fs.existsSync(media+'/video-60.nv12'))run(['-f','lavfi','-i','testsrc2=size=1920x1080:rate=60:duration=10','-vf','format=nv12','-frames:v','600','-f','rawvideo',media+'/video-60.nv12'],'make-video');
if(!fs.existsSync(media+'/game-60.nv12'))run(['-f','lavfi','-i','testsrc2=size=2304x1296:rate=60:duration=10','-vf',"drawgrid=w=96:h=96:t=2:c=white@0.35,crop=1920:1080:'192+180*sin(n/9)':'108+100*cos(n/13)',hue=h='20*sin(t*3)',format=nv12",'-frames:v','600','-f','rawvideo',media+'/game-60.nv12'],'make-game');
// A detailed full-screen pan complements the deliberately artificial color-bar proxy.
if(fs.existsSync(media+'/natural-source.mkv')){
  if(!fs.existsSync(media+'/game-texture.png'))run(['-i',media+'/natural-source.mkv','-vf','scale=2560:1440:flags=bicubic','-frames:v','1',media+'/game-texture.png'],'make-game-texture');
  if(!fs.existsSync(media+'/game-detail-60.nv12'))run(['-loop','1','-framerate','60','-i',media+'/game-texture.png','-vf',"crop=1920:1080:'320+300*sin(n/12)':'180+170*cos(n/17)',drawgrid=w=160:h=120:t=1:c=white@0.2,drawbox=x=20:y=20:w=260:h=80:c=black@0.6:t=fill,drawbox=x=40:y=40:w=220:h=15:c=lime:t=fill,drawbox=x=40:y=65:w=160:h=12:c=cyan:t=fill,scale=1920:1080:out_color_matrix=bt709:out_range=tv,format=nv12",'-frames:v','600','-f','rawvideo',media+'/game-detail-60.nv12'],'make-game-detail');
  for(const name of ['natural','game-detail'])if(!fs.existsSync(`${media}/${name}-30.nv12`))run(['-f','rawvideo','-pixel_format','nv12','-video_size','1920x1080','-framerate','60','-i',`${media}/${name}-60.nv12`,'-vf',"select='not(mod(n,2))',setpts=N/(30*TB)",'-r','30','-frames:v','300','-pix_fmt','nv12','-f','rawvideo',`${media}/${name}-30.nv12`],`make-${name}-30`);
}
for(const name of ['scroll','video','game'])if(!fs.existsSync(`${media}/${name}-30.nv12`))run(['-f','rawvideo','-pixel_format','nv12','-video_size','1920x1080','-framerate','60','-i',`${media}/${name}-60.nv12`,'-vf',"select='not(mod(n,2))',setpts=N/(30*TB)",'-r','30','-frames:v','300','-pix_fmt','nv12','-f','rawvideo',`${media}/${name}-30.nv12`],`make-${name}-30`);
console.log('Media ready');
