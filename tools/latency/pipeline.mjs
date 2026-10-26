// Pipeline configuration capture for every latency report (render path, capture, encoder, viewer, display point, network).
// collectPipeline(ctx) -> object stored as report.pipeline; pipelineLines(report) -> printable summary.
import fs from 'node:fs';
import { spawnSync } from 'node:child_process';

async function cdpCall(wsUrl, method, params = {}) {
  return await new Promise((resolve, reject) => {
    const ws = new WebSocket(wsUrl);
    const t = setTimeout(() => { try { ws.close(); } catch {} reject(new Error('cdp timeout')); }, 8000);
    ws.onopen = () => ws.send(JSON.stringify({ id: 1, method, params }));
    ws.onmessage = e => { const m = JSON.parse(e.data); if (m.id === 1) { clearTimeout(t); ws.close(); m.error ? reject(new Error(m.error.message)) : resolve(m.result); } };
    ws.onerror = () => { clearTimeout(t); reject(new Error('cdp socket error')); };
  });
}
function ps(cmd) {
  try { return spawnSync('powershell', ['-NoProfile', '-Command', cmd], { encoding: 'utf8', timeout: 20000 }).stdout.trim(); } catch { return ''; }
}

// ctx: { report, cdpPort, endpointLog, headed, viewerExe, windowSize, netemProfile }
export async function collectPipeline(ctx) {
  const { report } = ctx;
  const out = { remote: {}, capture: {}, encoder: {}, viewer: {}, network: {} };
  if (ctx.cdpPort) {
    try {
      const v = await (await fetch(`http://127.0.0.1:${ctx.cdpPort}/json/version`)).json();
      out.remote.browser = v.Browser;
      const si = await cdpCall(v.webSocketDebuggerUrl, 'SystemInfo.getInfo');
      out.remote.gpuDevices = (si.gpu.devices || []).map(d => ({ vendor: d.vendorString || ('0x' + Number(d.vendorId).toString(16)), device: d.deviceString, driver: `${d.driverVendor} ${d.driverVersion}`, active: d.active }));
      out.remote.gpuFeatureStatus = si.gpu.featureStatus;
      out.remote.glRenderer = si.gpu.auxAttributes?.glRenderer;
      out.remote.glImplementationParts = si.gpu.auxAttributes?.glImplementationParts;
    } catch (e) { out.remote.cdpError = String(e); }
  } else out.remote.note = 'no --remote-cdp: GPU adapter not queried via CDP';
  const gp = ps("Get-CimInstance Win32_Process -Filter \"Name='chrome.exe'\" | Where-Object { $_.CommandLine -like '*type=gpu-process*' -and $_.ExecutablePath -like 'C:\\src\\chromium*' } | Select-Object -First 1 -ExpandProperty CommandLine");
  out.remote.gpuProcessPresent = !!gp;
  if (gp) out.remote.gpuProcessFlags = (gp.match(/--(use-angle|use-gl|use-webgpu-adapter|enable-features|disable-features|disable-gpu[\w-]*|enable-gpu-[\w-]*|enable-unsafe-swiftshader|ozone-platform)(=\S*)?/g) || []).map(x => x.slice(0, 200));
  out.remote.softwareRenderer = !gp || JSON.stringify(out.remote).toLowerCase().includes('swiftshader');
  let lines = [];
  try { lines = fs.readFileSync(ctx.endpointLog, 'utf8').split(/\r?\n/); } catch { out.encoder.note = `endpoint log ${ctx.endpointLog} unreadable`; }
  const last = re => [...lines].reverse().find(l => re.test(l));
  const strip = l => l.replace(/^\[[^\]]*\]/, '').trim();
  const fields = line => Object.fromEntries([...(line || '').matchAll(/(\w+)=([^ ]+)/g)].map(m => [m[1], m[2]]));
  // Requested CLI values and negotiated peer state are retained even if VPL
  // never starts or software fallback becomes active later in the run.
  out.encoder.experiments = fields(last(/PRTS experiment options/));
  out.encoder.negotiated = fields(last(/PRTS peer negotiated/));
  const ie = last(/PRTS encoder InitEncode/);
  out.encoder.initLine = ie ? strip(ie) : null;
  if (ie) {
    const g = k => ie.match(new RegExp(k + '=([\\w.\\-]+)'))?.[1];
    Object.assign(out.encoder, { impl: ie.match(/impl=(.*?) status=/)?.[1] || g('impl'), fps: +g('max_fps'), cores: +g('cores'), startKbps: +g('start_kbps'), minKbps: +g('min_kbps'), maxKbps: +g('max_kbps'), resolution: ie.match(/InitEncode (\d+x\d+)/)?.[1] });
  }
  const ds = last(/D3D source fps=([1-9]|0\.[1-9])/);
  const activeVpl = lines.findLastIndex(l => /PRTS encoder active=vpl/.test(l));
  const fallback = lines.findLastIndex(l => /PRTS VPL .*fallback encoder=openh264/.test(l));
  out.encoder.backend = fallback > activeVpl ? 'OpenH264' : (report.sender?.backend || out.encoder.impl);
  const vpl = /^PRTS oneVPL/.test(out.encoder.backend || '');
  const sync = last(/PRTS capture sync=/)?.match(/sync=(\w+)/)?.[1];
  out.capture.sync = sync || (last(/PRTS OpenSharedFence token=/) ? 'fence' : 'mutex');
  out.encoder.fallbacks = lines.filter(l => /^\[/.test(l) && /PRTS VPL .*fallback encoder=openh264/.test(l)).map(strip);
  out.encoder.mixedBackend = activeVpl >= 0 && out.encoder.fallbacks.length > 0;
  out.encoder.requested = last(/PRTS encoder requested=/)?.match(/requested=(\w+)/)?.[1];
  out.encoder.selected = last(/PRTS encoder selected=/)?.match(/selected=(\w+)/)?.[1];
  out.encoder.vpl = (vpl || out.encoder.mixedBackend || last(/PRTS VPL Init status=/)) ? {
    implementation: strip(last(/PRTS VPL candidate=/) || ''),
    runtime: strip(last(/PRTS VPL runtime=/) || ''),
    readback: strip(last(/PRTS VPL (GetVideoParam|Reset-readback)/) || ''),
    init: strip(last(/PRTS VPL Init status=/) || ''),
    initSettings: fields(last(/PRTS VPL Init status=/)),
    settings: fields(last(/PRTS VPL (GetVideoParam|Reset-readback)/)),
    rounding: lines.filter(l => /PRTS VPL rounded /.test(l)).map(strip)
  } : null;
  out.capture.path = vpl
    ? 'Viz capture -> Mojo shared BGRA -> keyed CopyResource to leased private BGRA -> release mutex -> asynchronous D3D11 VideoProcessorBlt NV12 + Flush -> oneVPL AVC -> bitstream-only sync (startup I420 bridge possible)'
    : 'Viz capture -> Mojo -> D3D11 shared-texture copy -> D3D11 staging Map -> libyuv BT.709 I420 (CPU) -> OpenH264 (software)';
  if (out.encoder.mixedBackend) out.capture.path = 'Mixed: native D3D11 VPP -> oneVPL AVC, then runtime fallback to staging Map -> CPU I420 -> OpenH264; see encoder.fallbacks';
  else if (vpl && out.capture.sync === 'fence') out.capture.path = 'Viz shared-fence Signal -> Mojo shared BGRA -> GPU Wait -> private BGRA copy -> asynchronous source-read EVENT release -> D3D11 VPP NV12 -> oneVPL AVC -> bitstream-only sync';
  out.capture.lastD3dLine = ds ? strip(ds) : null;
  out.encoder.codec = report.stats?.codec;
  out.encoder.contentHint = report.hint;
  const vs = out.encoder.vpl?.settings || {};
  const opts = out.encoder.experiments;
  const vplMode = `oneVPL hardware AVC ${(opts.rc || 'vbr').toUpperCase()} LowPower=${vs.lowpower || opts.lowpower || 'on'} TU=${vs.tu || opts.tu || 7} AsyncDepth=1`;
  out.encoder.mode = (vpl ? vplMode : 'OpenH264 kRealtimeVideo (camera)') + ', MAINTAIN_RESOLUTION, playout hint ' + report.hint;
  if (out.encoder.mixedBackend) out.encoder.mode = `Mixed ${vplMode} -> OpenH264 kRealtimeVideo (camera), MAINTAIN_RESOLUTION, playout hint ` + report.hint;
  out.encoder.stageLine = strip(last(/PRTS latency5s p50\/p95_ms/) || '');
  out.encoder.relayOnly = lines.some(l => /forced relay-only via TURN/.test(l)) ? 'endpoint --turn' : (ctx.netemProfile ? 'control-plane iceTransportPolicy=relay' : false);
  const inb = report.pipeline?.inbound;
  Object.assign(out.viewer, { ua: report.ua, browser: (report.ua.match(/(Edg|HeadlessChrome|Chrome)\/[\d.]+/g) || []).join(' '), headless: !ctx.headed, exe: ctx.viewerExe, windowSize: ctx.windowSize,
    decoderImplementation: inb?.decoderImplementation ?? report.stats?.decoder, powerEfficientDecoder: inb?.powerEfficientDecoder });
  out.display = { measurementPoint: 'requestVideoFrameCallback expectedDisplayTime (compositor estimate)', realScanout: false,
    decodedMeasurementPoint: 'requestVideoFrameCallback presentationTime (decoded frame installed for composition)',
    note: ctx.headed ? 'headed window on the interactive desktop; expectedDisplayTime is the compositor estimate, DWM present/scanout is not observed'
      : 'headless=new: no real vsync/scanout, expectedDisplayTime is a compositor-clock estimate' };
  out.network = ctx.netemProfile
    ? { kind: 'netem relay (forced relay-only both sides)', profile: ctx.netemProfile, selectedPairs: report.pipeline?.pairs }
    : { kind: 'loopback, no relay', selectedPairs: report.pipeline?.pairs };
  return out;
}

export function pipelineLines(r) {
  const p = r.pipeline, g = p.remote.gpuDevices?.[0];
  const L = [
    `pipeline.remote: ${p.remote.browser || '?'}; GPU ${g ? g.vendor + ' / ' + g.device : 'n/a'}; gpu-process=${p.remote.gpuProcessPresent}; software=${p.remote.softwareRenderer}; ${(p.remote.gpuProcessFlags || []).filter(x => /angle|use-gl|graphite/.test(x)).join(' ')}`,
    `pipeline.capture: ${p.capture.path}`,
    `pipeline.encoder: ${p.encoder.backend || p.encoder.impl}${p.encoder.mixedBackend ? ' (runtime fallback; mixed run)' : ''} ${p.encoder.resolution}@${p.encoder.fps} kbps start/min/max ${p.encoder.startKbps}/${p.encoder.minKbps}/${p.encoder.maxKbps} cores ${p.encoder.cores}; ${p.encoder.mode}`,
    `pipeline.viewer: ${p.viewer.browser} ${p.viewer.headless ? 'headless' : 'HEADED'}; decoder ${p.viewer.decoderImplementation} powerEfficient=${p.viewer.powerEfficientDecoder}`,
    `pipeline.display: ${p.display.measurementPoint}; ${p.display.note}`,
    `pipeline.network: ${p.network.kind}${p.network.profile ? ' ' + p.network.profile : ''}; pairs ${(p.network.selectedPairs || []).map(x => x.local + '/' + x.remote).join(',')}`];
  if (p.encoder.vpl) L.push('pipeline.encoder.vpl: ' + p.encoder.vpl.readback + '; ' + p.encoder.vpl.runtime);
  if (p.encoder.experiments && Object.keys(p.encoder.experiments).length) L.push('pipeline.encoder.experiments: ' + JSON.stringify(p.encoder.experiments));
  if (p.encoder.negotiated && Object.keys(p.encoder.negotiated).length) L.push('pipeline.encoder.negotiated: ' + JSON.stringify(p.encoder.negotiated));
  if (r.netem && r.netem.links) {
    L.push('netem: ' + ['down', 'up'].map(d => { const l = r.netem.links[d]; return `${d}: ${l.pkts} pkts ${l.achievedMbps}/${l.offeredMbps} Mbps drops q${l.dropQueue}/loss${l.dropLoss} maxQ ${l.maxQueueDelayMs}ms (limit ${l.queueLimitBytes}B)`; }).join(' | '));
    L.push('netem timer error ms ' + JSON.stringify(r.netem.timerError));
  }
  return L;
}
