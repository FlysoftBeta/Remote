// Deterministic local load fixtures. ?scenario=S|V|G; no downloads required.
// V is the 1080p30 canvas fallback when a local ffmpeg clip is unavailable.
(function () {
  'use strict';
  const name = (new URLSearchParams(location.search).get('scenario') || 'marker').toUpperCase();
  const host = document.getElementById('load');
  let seed = 731;
  function random() { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 4294967296; }
  function texture(w, h, index) {
    const c = document.createElement('canvas'); c.width = w; c.height = h;
    const g = c.getContext('2d');
    const sky = g.createLinearGradient(0, 0, 0, h);
    sky.addColorStop(0, '#4385b8'); sky.addColorStop(.5, '#d3c9a1'); sky.addColorStop(1, '#183b38');
    g.fillStyle = sky; g.fillRect(0, 0, w, h);
    for (let layer = 0; layer < 5; layer++) {
      g.fillStyle = `hsl(${130 + index * 9 + layer * 12},${25 + layer * 5}%,${48 - layer * 7}%)`;
      g.beginPath(); g.moveTo(0, h);
      for (let x = 0; x <= w + 16; x += 16) g.lineTo(x, h * (.25 + layer * .12) + Math.sin(x / 80 + layer * 3 + index) * 50 + random() * 45);
      g.lineTo(w, h); g.fill();
    }
    // Fine foliage/detail makes scrolling exercise inter-frame compression.
    for (let i = 0; i < w * h / 18; i++) {
      const x = random() * w, y = h * .4 + random() * h * .6;
      g.fillStyle = `hsla(${90 + random() * 90},40%,${15 + random() * 55}%,.6)`;
      g.fillRect(x, y, 2 + random() * 7, 2 + random() * 5);
    }
    return c;
  }
  window.rkScenario = { name: ['S', 'V', 'G'].includes(name) ? name : 'marker', updates: 0, source: null };
  if (window.rkScenario.name === 'marker') return;
  if (name === 'S') {
    const images = Array.from({ length: 6 }, (_, i) => texture(960, 540, i).toDataURL());
    const text = 'A river crosses the valley below the wooded slopes. Along the path, changing light reveals the texture of leaves, stone and water. This local article combines readable text and detailed landscape illustrations for a continuous browser scrolling workload. ';
    for (let i = 0; i < 100; i++) {
      const article = document.createElement('article');
      const heading = document.createElement('h1'); heading.textContent = `Field journal ${i + 1}: trails through the valley`;
      const image = document.createElement('img'); image.src = images[i % images.length];
      const paragraph = document.createElement('p'); paragraph.textContent = text.repeat(6);
      article.append(heading, image, paragraph); host.append(article);
    }
    let last = null, position = 0;
    function scroll(now) {
      if (last != null) position += (now - last) * .36;
      last = now;
      host.scrollTop = position % Math.max(1, host.scrollHeight - host.clientHeight);
      window.rkScenario.updates++; requestAnimationFrame(scroll);
    }
    window.rkScenario.source = 'DOM text + local generated landscape images; continuous 360 CSS px/s scroll';
    requestAnimationFrame(scroll); return;
  }
  const c = document.createElement('canvas'); c.width = 1920; c.height = 1080;
  const g = c.getContext('2d', { alpha: false }); host.append(c);
  const landscape = texture(1920, 1080, name === 'V' ? 2 : 4);
  const sprites = Array.from({ length: 160 }, () => ({ x: random() * 1920, y: random() * 1080, speed: 100 + random() * 700, hue: random() * 360 }));
  let last = -Infinity;
  function animate(now) {
    requestAnimationFrame(animate);
    if (now - last < 1000 / 30 - 1) return;
    last = now; const t = now / 1000;
    const dx = (t * (name === 'G' ? 290 : 90)) % 1920;
    g.drawImage(landscape, -dx, 0); g.drawImage(landscape, 1920 - dx, 0);
    if (name === 'V') {
      // A camera pan with moving foreground subjects, full 1920x1080 at 30 fps.
      for (let i = 0; i < 22; i++) {
        const x = (i * 137 + t * (180 + i * 8)) % 2240 - 160;
        const y = 260 + Math.sin(t * .8 + i) * 180 + i * 25;
        g.fillStyle = `hsl(${i * 37},65%,55%)`; g.beginPath(); g.ellipse(x, y, 65, 28, Math.sin(t + i), 0, Math.PI * 2); g.fill();
      }
    } else {
      g.strokeStyle = '#8bbfbd'; g.lineWidth = 2;
      for (let i = 0; i < 32; i++) { const x = (i * 80 - t * 430) % 2048; g.beginPath(); g.moveTo(x, 0); g.lineTo(x + 500, 1080); g.stroke(); }
      for (const s of sprites) {
        const x = (s.x + t * s.speed) % 2048 - 64, y = (s.y + Math.sin(t * 2 + s.x) * 180 + 1080) % 1080;
        g.fillStyle = `hsl(${s.hue},80%,60%)`; g.fillRect(x, y, 42, 28);
        g.fillStyle = '#f6f2de'; g.fillRect(x + 6, y + 6, 12, 8);
        g.fillStyle = '#222'; g.fillRect(x + 4, y + 25, 10, 8); g.fillRect(x + 27, y + 25, 10, 8);
      }
      g.fillStyle = '#152332'; g.fillRect(24, 1000, 420, 54); g.fillStyle = '#fff'; g.font = '28px monospace'; g.fillText('SECTOR 7   SCORE ' + Math.floor(t * 100), 36, 1038);
    }
    window.rkScenario.updates++;
  }
  window.rkScenario.source = name === 'V' ? '1920x1080 canvas moving-pattern fallback at 30 fps (ffmpeg unavailable)' : '1920x1080 canvas scrolling game terrain + 160 moving sprites at 30 fps';
  requestAnimationFrame(animate);
})();
