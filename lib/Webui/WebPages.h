#pragma once
// All front-end code lives here: HTML pages, shared CSS and shared JS.
// Edit this file to change how the UI looks/behaves; the API lives in WebUI.h.
//
//   PAGE_INDEX   "/"           image gallery + collapsible Networks panel
//   PAGE_UPLOAD  "/upload"     image editor (crop, rotate, tone, dither, red channel) + upload
//   PAGE_CSS     "/style.css"  shared styles, light + dark theme
//   PAGE_JS      "/app.js"     shared helpers + theme toggle
//
// Image file format (what the upload page produces, what the ESP stores):
//   raw 1 bit per pixel, row-major, MSB first, rows padded to whole bytes,
//   no header. Black plane = ((W+7)/8)*H bytes (15000 for 400x300), optionally
//   followed by a red plane of the same size (30000 bytes total).
//   Each plane is the Adafruit GFXcanvas1 buffer layout.
//   INK_BIT (below) says which bit value means "ink" in both planes.

#include <Arduino.h>

// ============================================================ CSS
static const char PAGE_CSS[] PROGMEM = R"HTML(
:root{--bg:#ffffff;--fg:#111827;--muted:#6b7280;--card:#f3f4f6;--border:#d1d5db;--accent:#2563eb;--accent-fg:#ffffff;--danger:#dc2626;color-scheme:light}
@media (prefers-color-scheme:dark){
  :root:not([data-theme=light]){--bg:#0f1115;--fg:#e5e7eb;--muted:#9ca3af;--card:#1a1d24;--border:#2d323c;--accent:#3b82f6;--accent-fg:#ffffff;--danger:#ef4444;color-scheme:dark}
}
:root[data-theme=dark]{--bg:#0f1115;--fg:#e5e7eb;--muted:#9ca3af;--card:#1a1d24;--border:#2d323c;--accent:#3b82f6;--accent-fg:#ffffff;--danger:#ef4444;color-scheme:dark}

*{box-sizing:border-box}
[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.4 system-ui,sans-serif}
header{display:flex;align-items:center;gap:.6rem;padding:.7rem 1rem;border-bottom:1px solid var(--border);position:sticky;top:0;background:var(--bg);z-index:5}
header h1{font-size:1.15rem;margin:0;flex:1}
main{max-width:720px;margin:0 auto;padding:1rem 1rem 5rem}
h2{font-size:1rem;margin:0}
.row{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:.6rem}
.row2{display:flex;gap:.5rem}
.row2 button{flex:1}
.muted{color:var(--muted);font-size:.9rem}

.btn,button{font:inherit;display:inline-block;padding:.5rem .9rem;border-radius:8px;border:1px solid var(--border);background:var(--card);color:var(--fg);text-decoration:none;cursor:pointer}
.btn.primary,button.primary{background:var(--accent);border-color:var(--accent);color:var(--accent-fg)}
button.danger{color:var(--danger);border-color:var(--danger);background:transparent}
button:disabled{opacity:.5;cursor:default}
input[type=text],input[type=password],select{font:inherit;width:100%;padding:.55rem .7rem;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--fg)}
input[type=range]{width:100%;accent-color:var(--accent)}

.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:.6rem}
.tile{padding:0;overflow:hidden;text-align:left;display:flex;flex-direction:column}
.tile img{width:100%;aspect-ratio:4/3;object-fit:cover;background:var(--border);display:block}
.tile canvas{width:100%;height:auto;display:block;background:#fff}
.tile span{padding:.3rem .5rem;font-size:.8rem;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.tile.sel{outline:3px solid var(--accent);outline-offset:-3px}

details{margin-top:1rem;border:1px solid var(--border);border-radius:10px;background:var(--card)}
summary{padding:.8rem 1rem;cursor:pointer;font-weight:600}
.panel{padding:0 1rem 1rem}
ul.nets{list-style:none;padding:0;margin:.2rem 0 .8rem}
ul.nets li{display:flex;justify-content:space-between;align-items:center;gap:.5rem;padding:.4rem 0;border-bottom:1px solid var(--border)}
.stack{display:flex;flex-direction:column;gap:.5rem}

.bar{position:fixed;left:0;right:0;bottom:0;display:flex;align-items:center;gap:.6rem;padding:.7rem 1rem;background:var(--card);border-top:1px solid var(--border);z-index:6}
.bar span{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}

.drop{display:block;border:2px dashed var(--border);border-radius:12px;padding:2rem 1rem;text-align:center;cursor:pointer;color:var(--muted)}
canvas.pv{display:block;width:100%;margin:1rem auto .4rem;border:1px solid var(--border);background:#fff;image-rendering:pixelated}
.lbl{display:block;margin:1rem 0 .3rem;font-weight:600}
.editor-tabs{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:.35rem;margin-top:1rem}
.editor-tabs button{padding:.5rem .25rem;font-size:.85rem}
.editor-tabs button[aria-selected=true]{background:var(--accent);border-color:var(--accent);color:var(--accent-fg)}
.editor-panel{padding:.25rem .25rem .5rem}
.ctl{display:grid;grid-template-columns:6rem 1fr 3.5rem;align-items:center;gap:.5rem;margin:.5rem 0}
.ctl output{text-align:right;color:var(--muted);font-size:.85rem}
.chk{display:flex;align-items:center;gap:.5rem;margin:.5rem 0}
canvas.pv{touch-action:none;cursor:grab}
canvas.hue{display:block;width:100%;height:14px;border-radius:4px;margin:.4rem 0;image-rendering:pixelated}
h3{font-size:.9rem;margin:1rem 0 .2rem;color:var(--muted)}
.progress{height:8px;background:var(--border);border-radius:4px;overflow:hidden;margin:1rem 0 .5rem}
.progress div{height:100%;width:0;background:var(--accent)}
)HTML";

// ============================================================ shared JS
static const char PAGE_JS[] PROGMEM = R"HTML(
// Which bit value means "black pixel" in the stored files.
// 1 matches Adafruit GFXcanvas1 (draw colour 1 = ink). If images look inverted
// on the real display, change this to 0 (re-upload existing images afterwards).
const INK_BIT=1;

// apply saved theme as early as possible (no flash)
(function(){try{var t=localStorage.getItem('theme');if(t)document.documentElement.dataset.theme=t}catch(e){}})();

const $=id=>document.getElementById(id);

async function api(path,opts){
  const r=await fetch(path,opts);
  if(!r.ok)throw new Error((await r.text())||('HTTP '+r.status));
  return r.json();
}
function post(path,obj){return api(path,{method:'POST',body:new URLSearchParams(obj)});}
function fmt(b){return b>=1048576?(b/1048576).toFixed(1)+' MB':b>=1024?(b/1024).toFixed(0)+' KB':b+' B';}

document.addEventListener('DOMContentLoaded',()=>{
  const b=$('theme');
  if(!b)return;
  b.onclick=()=>{
    const d=document.documentElement;
    const dark=d.dataset.theme?d.dataset.theme==='dark':matchMedia('(prefers-color-scheme: dark)').matches;
    const n=dark?'light':'dark';
    d.dataset.theme=n;
    try{localStorage.setItem('theme',n)}catch(e){}
  };
});
)HTML";

// ============================================================ index page
static const char PAGE_INDEX[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>E-Paper</title>
<link rel="stylesheet" href="/style.css">
<script src="/app.js"></script>
</head><body>

<header>
  <h1>E-Paper</h1>
  <a class="btn primary" href="/upload">Upload</a>
  <button id="theme" title="Toggle dark mode">&#9680;</button>
</header>

<main>
  <div class="row"><h2>Images</h2><span id="storage" class="muted"></span></div>
  <div id="grid" class="grid"></div>
  <p id="empty" class="muted" hidden>No images yet. Tap Upload to add one.</p>

  <details id="nets">
    <summary>Networks <span id="netstatus" class="muted"></span></summary>
    <div class="panel">
      <ul id="netlist" class="nets"></ul>
      <form id="netform" class="stack" autocomplete="off">
        <input type="text" name="ssid" list="nearby" placeholder="Network name (SSID)" required maxlength="32">
        <datalist id="nearby"></datalist>
        <input type="password" name="pass" placeholder="Password (blank if open)" maxlength="63">
        <div class="row2">
          <button type="button" id="scan">Scan</button>
          <button type="submit" class="primary">Save network</button>
        </div>
        <p id="netmsg" class="muted"></p>
      </form>
    </div>
  </details>
</main>

<div id="selbar" class="bar" hidden>
  <span id="selname"></span>
  <button id="draw" class="primary">Draw on display</button>
  <button id="del" class="danger">Delete</button>
</div>

<script>
let sel=null,selEl=null;

function select(name,el){
  if(selEl)selEl.classList.remove('sel');
  if(selEl===el){sel=selEl=null;$('selbar').hidden=true;return;}
  sel=name;selEl=el;
  el.classList.add('sel');
  $('selname').textContent=name;
  $('selbar').hidden=false;
}

// decode a raw 1bpp .bin file onto a canvas of the same size
async function drawBin(cv,name){
  const r=await fetch('/img?name='+encodeURIComponent(name));
  const b=new Uint8Array(await r.arrayBuffer());
  const w=cv.width,h=cv.height,stride=(w+7)>>3,plane=stride*h;
  if(b.length!==plane&&b.length!==2*plane)return;
  const hasRed=b.length===2*plane;
  const ctx=cv.getContext('2d'),id=ctx.createImageData(w,h),d=id.data;
  for(let y=0;y<h;y++)for(let x=0;x<w;x++){
    const o=y*stride+(x>>3),s=7-(x&7),p=(y*w+x)*4;
    const k=((b[o]>>s)&1)===INK_BIT;
    const r=hasRed&&((b[plane+o]>>s)&1)===INK_BIT;
    d[p]=k?0:255;d[p+1]=d[p+2]=(k||r)?0:255;
    d[p+3]=255;
  }
  ctx.putImageData(id,0,0);
}

async function loadImages(){
  try{
    const d=await api('/api/images');
    const g=$('grid');
    g.replaceChildren();
    sel=selEl=null;$('selbar').hidden=true;
    $('empty').hidden=d.files.length>0;
    $('storage').textContent=fmt(d.used)+' / '+fmt(d.total)+' used';
    const jobs=[];
    for(const f of d.files){
      const b=document.createElement('button');
      b.className='tile';
      let thumb;
      if(f.name.endsWith('.bin')){
        thumb=document.createElement('canvas');
        thumb.width=d.w;thumb.height=d.h;
        jobs.push(()=>drawBin(thumb,f.name));
      }else{  // legacy PNG/JPG uploads
        thumb=new Image();
        thumb.loading='lazy';thumb.alt=f.name;
        thumb.src='/img?name='+encodeURIComponent(f.name);
      }
      const cap=document.createElement('span');
      cap.textContent=f.name.replace(/\.bin$/,'');
      b.append(thumb,cap);
      b.onclick=()=>select(f.name,b);
      g.append(b);
    }
    for(const j of jobs){try{await j();}catch(e){}}  // one at a time: the ESP serves one request at once
  }catch(e){
    $('empty').hidden=false;
    $('empty').textContent='Could not load images: '+e.message;
  }
}

$('del').onclick=async()=>{
  if(!sel||!confirm('Delete '+sel+'?'))return;
  try{await post('/api/images/delete',{name:sel});}catch(e){alert(e.message);}
  loadImages();
};

$('draw').onclick=async()=>{
  if(!sel) return;
  try{await post('/api/images/draw',{name:sel});}catch(e){alert(e.message);}
};

// ---- networks panel (loaded the first time it is opened) ----
let netsLoaded=false;
$('nets').addEventListener('toggle',()=>{
  if($('nets').open&&!netsLoaded){netsLoaded=true;loadNets();}
});

async function loadNets(){
  try{
    const d=await api('/api/networks');
    $('netstatus').textContent=d.connected?('- connected to '+d.ssid+' ('+d.ip+')'):'- not connected';
    const ul=$('netlist');
    ul.replaceChildren();
    if(!d.saved.length){
      const li=document.createElement('li');
      li.className='muted';li.textContent='No saved networks';
      ul.append(li);
    }
    d.saved.forEach((s,i)=>{
      const li=document.createElement('li');
      const n=document.createElement('span');n.textContent=s;
      const b=document.createElement('button');
      b.textContent='Remove';b.className='danger';
      b.onclick=async()=>{try{await post('/api/networks/remove',{i});}catch(e){}loadNets();};
      li.append(n,b);ul.append(li);
    });
  }catch(e){$('netmsg').textContent=e.message;}
}

$('netform').onsubmit=async e=>{
  e.preventDefault();
  const f=e.target;
  $('netmsg').textContent='';
  try{
    await post('/api/networks/add',{ssid:f.ssid.value.trim(),pass:f.pass.value});
    f.reset();
    $('netmsg').textContent='Saved. The device will try to connect shortly.';
    loadNets();
  }catch(err){$('netmsg').textContent=err.message;}
};

$('scan').onclick=async()=>{
  const b=$('scan');
  b.disabled=true;b.textContent='Scanning...';
  try{
    const list=await api('/api/scan');
    const dl=$('nearby');
    dl.replaceChildren();
    list.forEach(s=>{const o=document.createElement('option');o.value=s;dl.append(o);});
    $('netmsg').textContent=list.length+' network(s) found - start typing a name to pick one.';
  }catch(e){$('netmsg').textContent=e.message;}
  b.disabled=false;b.textContent='Scan';
};

loadImages();
</script>
</body></html>
)HTML";

// ============================================================ upload page
static const char PAGE_UPLOAD[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Upload - E-Paper</title>
<link rel="stylesheet" href="/style.css">
<script src="/app.js"></script>
</head><body>

<header>
  <a class="btn" href="/">&larr; Back</a>
  <h1>Upload image</h1>
  <button id="theme" title="Toggle dark mode">&#9680;</button>
</header>

<main>
  <label class="drop" id="drop" for="file">Tap to choose an image</label>
  <input type="file" id="file" accept="image/*" hidden>

  <div id="editor" hidden>
    <canvas id="pv" class="pv"></canvas>
    <p class="muted" id="info"></p>

    <label class="lbl" for="name">Name</label>
    <input type="text" id="name" maxlength="36" autocomplete="off">
    <p class="muted" id="saveas"></p>

    <div class="editor-tabs" role="tablist" aria-label="Image adjustments">
      <button type="button" id="tab-crop" role="tab" aria-controls="panel-crop" aria-selected="true" tabindex="0">Crop</button>
      <button type="button" id="tab-tone" role="tab" aria-controls="panel-tone" aria-selected="false" tabindex="-1">Tone</button>
      <button type="button" id="tab-dither" role="tab" aria-controls="panel-dither" aria-selected="false" tabindex="-1">Black &amp; dithering</button>
      <button type="button" id="tab-red" role="tab" aria-controls="panel-red" aria-selected="false" tabindex="-1">Red channel</button>
    </div>

    <section id="panel-crop" class="editor-panel" role="tabpanel" aria-labelledby="tab-crop">
        <p class="muted">Zoom in and move the image around; drag the preview to pan. At 100% the whole image is shown, with white bars if the shape doesn't match.</p>
        <div class="ctl"><span>Zoom</span><input type="range" id="zm" min="100" max="600" value="100" data-u="%"><output id="zm_o">100%</output></div>
        <div class="ctl"><span>X offset</span><input type="range" id="ox" min="-100" max="100" value="0"><output id="ox_o">0</output></div>
        <div class="ctl"><span>Y offset</span><input type="range" id="oy" min="-100" max="100" value="0"><output id="oy_o">0</output></div>
        <div class="ctl"><span>Rotate</span>
          <select id="image-rotate">
            <option value="0">0</option>
            <option value="90">90</option>
            <option value="180">180</option>
            <option value="270">270</option>
          </select><span></span></div>
        <button type="button" id="cropreset">Reset crop</button>
    </section>

    <section id="panel-tone" class="editor-panel" role="tabpanel" aria-labelledby="tab-tone" hidden>
        <div class="ctl"><span>Brightness</span><input type="range" id="br" min="-100" max="100" value="0"><output id="br_o">0</output></div>
        <div class="ctl"><span>Contrast</span><input type="range" id="co" min="-100" max="100" value="0"><output id="co_o">0</output></div>
        <div class="ctl"><span>Gamma</span><input type="range" id="ga" min="-100" max="100" value="0"><output id="ga_o">0</output></div>
        <div class="ctl"><span>Saturation</span><input type="range" id="sa" min="-100" max="100" value="0"><output id="sa_o">0</output></div>
        <button type="button" id="tonereset">Reset tone</button>
    </section>

    <section id="panel-dither" class="editor-panel" role="tabpanel" aria-labelledby="tab-dither" hidden>
        <div class="ctl"><span>Method</span>
          <select id="mode">
            <option value="fs">Floyd-Steinberg (photos)</option>
            <option value="atk">Atkinson (crisp, high contrast)</option>
            <option value="bayer">Ordered / Bayer (patterned)</option>
            <option value="thresh">Threshold (line art / text)</option>
          </select><span></span></div>
        <div class="ctl"><span>Threshold</span><input type="range" id="th" min="0" max="255" value="128"><output id="th_o">128</output></div>
        <div class="ctl"><span>Strength</span><input type="range" id="ds" min="0" max="100" value="100" data-u="%"><output id="ds_o">100%</output></div>
        <p class="muted">Threshold: lower = less black. Strength: how much dithering noise/error spreading is used.</p>
        <button type="button" id="dithreset">Reset</button>
    </section>

    <section id="panel-red" class="editor-panel" role="tabpanel" aria-labelledby="tab-red" hidden>
        <label class="chk"><input type="checkbox" id="rd"> Use red ink</label>
        <canvas id="hue" class="hue" width="360" height="1"></canvas>
        <div class="ctl"><span>Hue</span><input type="range" id="rh" min="0" max="359" value="0" data-u="&deg;"><output id="rh_o">0&deg;</output></div>
        <div class="ctl"><span>Band width</span><input type="range" id="rw" min="2" max="90" value="25" data-u="&deg;"><output id="rw_o">25&deg;</output></div>
        <div class="ctl"><span>Feather</span><input type="range" id="rf" min="0" max="60" value="15" data-u="&deg;"><output id="rf_o">15&deg;</output></div>
        <div class="ctl"><span>Min colour</span><input type="range" id="rs" min="0" max="100" value="35" data-u="%"><output id="rs_o">35%</output></div>
        <div class="ctl"><span>Min bright</span><input type="range" id="rv" min="0" max="100" value="25" data-u="%"><output id="rv_o">25%</output></div>
        <div class="ctl"><span>Red amount</span><input type="range" id="ra" min="0" max="200" value="100" data-u="%"><output id="ra_o">100%</output></div>
        <p class="muted">Hue/Band width choose which colours become red (the bright part of the bar). Min colour excludes greys and pastels, Min bright keeps dark shades black. Dark reds still turn black.</p>
        <button type="button" id="redreset">Reset red</button>
    </section>

    <div id="prog" class="progress" hidden><div id="bar"></div></div>
    <p id="msg" class="muted"></p>
    <button id="send" class="primary">Upload</button>
    <button id="preview" class="primary">Preview</button>
  </div>
</main>

<script>
const off=document.createElement('canvas');   // offscreen W x H working canvas
let octx=null,W=400,H=300,names=new Set(),img=null,imgUrl=null,bits=null,raf=0;

async function init(){
  try{
    const d=await api('/api/images');   // gives us the display size + existing names
    W=d.w;H=d.h;
    names=new Set(d.files.map(f=>f.name));
  }catch(e){$('msg').textContent='Could not reach the device: '+e.message;}
  off.width=W;off.height=H;
  octx=off.getContext('2d',{willReadFrequently:true});
  const pv=$('pv');
  pv.width=W;pv.height=H;pv.style.maxWidth=W+'px';
}
const ready=init();

// ---------- naming ----------
function cleanBase(s){
  s=s.replace(/\.bin$/i,'').replace(/[^A-Za-z0-9._-]+/g,'_').replace(/^\.+/,'').slice(0,36);
  return s||'image';
}
function updName(){
  const n=cleanBase($('name').value)+'.bin';
  $('saveas').textContent='Saved as '+n+(names.has(n)?' (replaces an existing image)':'');
  return n;
}
$('name').oninput=updName;

// ---------- picking a file ----------
$('file').onchange=async e=>{
  const f=e.target.files[0];
  if(!f)return;
  await ready;
  try{
    if(imgUrl)URL.revokeObjectURL(imgUrl);
    imgUrl=URL.createObjectURL(f);
    const im=new Image();
    im.src=imgUrl;
    await im.decode();
    img=im;
  }catch(err){$('msg').textContent='Could not read that image.';return;}
  $('name').value=cleanBase(f.name);
  updName();
  $('editor').hidden=false;
  $('drop').textContent='Choose a different image';
  $('msg').textContent='';
  $('send').disabled=false;
  render();
};

// ---------- the conversion pipeline ----------
// 1. rotate, then zoom/offset onto a W x H white frame
// 2. tone (brightness, contrast, gamma, saturation)
// 3. per pixel: how much red ink (hue band) and black ink (the rest) it needs
// 4. dither both inks onto a black / white / red palette
const BAYER=[0,32,8,40,2,34,10,42,48,16,56,24,50,18,58,26,12,44,4,36,14,46,6,38,60,28,52,20,62,30,54,22,
 3,35,11,43,1,33,9,41,51,19,59,27,49,17,57,25,15,47,7,39,13,45,5,37,63,31,55,23,61,29,53,21];
const ATK=[[1,0],[2,0],[-1,1],[0,1],[1,1],[0,2]];
const sstep=t=>t<=0?0:t>=1?1:t*t*(3-2*t);

function render(){
  if(!img)return;
  const iw=img.naturalWidth,ih=img.naturalHeight;
  const angle=+$('image-rotate').value;
  const sideways=angle===90||angle===270;
  const rw=sideways?ih:iw,rh=sideways?iw:ih;
  const zoom=+$('zm').value/100,ox=+$('ox').value/100,oy=+$('oy').value/100;
  const sc=Math.min(W/rw,H/rh)*zoom;                 // 100% = whole image visible
  const dw=rw*sc,dh=rh*sc;
  const dx=(W-dw)/2*(1+ox),dy=(H-dh)/2*(1+oy);       // offset -1..1 slides between the two edges

  octx.fillStyle='#fff';
  octx.fillRect(0,0,W,H);
  octx.imageSmoothingEnabled=true;
  octx.imageSmoothingQuality='high';
  octx.save();
  octx.translate(dx+dw/2,dy+dh/2);
  octx.rotate(angle*Math.PI/180);
  octx.drawImage(img,-iw*sc/2,-ih*sc/2,iw*sc,ih*sc);
  octx.restore();
  const px=octx.getImageData(0,0,W,H).data;

  const br=+$('br').value*1.28,cv=+$('co').value;
  const k=cv>=0?1+cv/50:1+cv/100;
  const gam=Math.pow(2,+$('ga').value/50);            // >1 = lighter midtones
  const sat=1+ +$('sa').value/100;
  const useRed=$('rd').checked;
  const hc=+$('rh').value,hw=+$('rw').value,hf=Math.max(.01,+$('rf').value);
  const smin=+$('rs').value/100,vmin=+$('rv').value/100,ramt=+$('ra').value/100;
  const mode=$('mode').value,thr=1-(+$('th').value)/255,str=+$('ds').value/100;

  const N=W*H,K=new Float32Array(N),R=new Float32Array(N);
  const lut=new Float32Array(256);
  for(let i=0;i<256;i++){let v=Math.pow(i/255,1/gam)*255;v=(v-128)*k+128+br;lut[i]=v<0?0:v>255?255:v;}
  for(let i=0,p=0;i<N;i++,p+=4){
    let r=lut[px[p]],g=lut[px[p+1]],b=lut[px[p+2]];
    const l0=0.2126*r+0.7152*g+0.0722*b;
    if(sat!==1){r=l0+(r-l0)*sat;g=l0+(g-l0)*sat;b=l0+(b-l0)*sat;
      r=r<0?0:r>255?255:r;g=g<0?0:g>255?255:g;b=b<0?0:b>255?255:b;}
    const mx=Math.max(r,g,b),mn=Math.min(r,g,b),V=mx/255,d=mx-mn,S=mx?d/mx:0;
    let m=0;
    if(useRed&&d>0){
      let h=mx===r?((g-b)/d)%6:mx===g?(b-r)/d+2:(r-g)/d+4;
      h*=60;if(h<0)h+=360;
      let dh=Math.abs(h-hc);if(dh>180)dh=360-dh;
      m=Math.min(1,sstep(1-(dh-hw)/hf)*sstep((S-smin)/.15)*sstep((V-vmin)/.1)*ramt);
    }
    const dark=1-(0.2126*r+0.7152*g+0.0722*b)/255;
    K[i]=(1-m)*dark+m*(1-V);                          // black coverage 0..1
    R[i]=m*V;                                         // red coverage 0..1
  }

  const stride=(W+7)>>3,plane=stride*H,out=new Uint8Array(useRed?2*plane:plane);
  if(!INK_BIT)out.fill(255);
  const pvc=$('pv').getContext('2d'),pid=pvc.createImageData(W,H),pd=pid.data;
  pd.fill(255);
  const diffuse=mode==='fs'||mode==='atk';

  for(let y=0;y<H;y++){
    for(let x=0;x<W;x++){
      const i=y*W+x;
      const kv=K[i],rv=R[i];
      let t=thr;
      if(mode==='bayer')t+=((BAYER[(y&7)*8+(x&7)]+.5)/64-.5)*str;
      let ok=0,or=0;
      if(kv+rv>=t){if(kv>=rv)ok=1;else or=1;}
      const p=i*4,bi=y*stride+(x>>3),m=0x80>>(x&7);
      if(ok){
        if(INK_BIT)out[bi]|=m;else out[bi]&=~m;
        pd[p]=pd[p+1]=pd[p+2]=0;
      }else if(or){
        if(INK_BIT)out[plane+bi]|=m;else out[plane+bi]&=~m;
        pd[p+1]=pd[p+2]=0;
      }
      if(diffuse){
        const ek=(kv-ok)*str,er=(rv-or)*str;
        if(mode==='fs'){                               // Floyd-Steinberg
          if(x+1<W){K[i+1]+=ek*0.4375;R[i+1]+=er*0.4375;}
          if(y+1<H){
            if(x>0){K[i+W-1]+=ek*0.1875;R[i+W-1]+=er*0.1875;}
            K[i+W]+=ek*0.3125;R[i+W]+=er*0.3125;
            if(x+1<W){K[i+W+1]+=ek*0.0625;R[i+W+1]+=er*0.0625;}
          }
        }else{                                         // Atkinson: spreads 6/8 of the error
          for(const a of ATK){
            const nx=x+a[0],ny=y+a[1];
            if(nx<0||nx>=W||ny>=H)continue;
            K[ny*W+nx]+=ek/8;R[ny*W+nx]+=er/8;
          }
        }
      }
    }
  }
  pvc.putImageData(pid,0,0);
  bits=out;
  $('info').textContent=iw+'x'+ih+' px on a '+W+'x'+H+' display ('+out.length+' bytes'+(useRed?', black + red':'')+')';
  drawHue();
}

function drawHue(){
  const g=$('hue').getContext('2d'),hc=+$('rh').value,hw=+$('rw').value,hf=+$('rf').value,on=$('rd').checked;
  for(let h=0;h<360;h++){
    let dh=Math.abs(h-hc);if(dh>180)dh=360-dh;
    const w=on?(dh<=hw?1:dh>=hw+hf?0:1-(dh-hw)/hf):0;
    g.globalAlpha=1;g.clearRect(h,0,1,1);
    g.globalAlpha=.25+.75*w;
    g.fillStyle='hsl('+h+',90%,50%)';
    g.fillRect(h,0,1,1);
  }
  g.globalAlpha=1;
}

function schedule(){
  if(!raf)raf=requestAnimationFrame(()=>{raf=0;render();});
}

// ---------- controls ----------
function showVal(id){
  const el=$(id);
  $(id+'_o').textContent=el.value+(el.dataset.u||'');
}
const SLIDERS=['zm','ox','oy','br','co','ga','sa','th','ds','rh','rw','rf','rs','rv','ra'];
SLIDERS.forEach(id=>$(id).addEventListener('input',()=>{showVal(id);schedule();}));
['mode','rd'].forEach(id=>$(id).addEventListener('change',schedule));
$('image-rotate').addEventListener('change',schedule);

const editorTabs=Array.from(document.querySelectorAll('[role="tab"]'));
function activateTab(tab,focus){
  editorTabs.forEach(t=>{
    const active=t===tab;
    t.setAttribute('aria-selected',active);
    t.tabIndex=active?0:-1;
    $(t.getAttribute('aria-controls')).hidden=!active;
  });
  if(focus)tab.focus();
}
editorTabs.forEach((tab,index)=>{
  tab.onclick=()=>activateTab(tab,false);
  tab.onkeydown=e=>{
    let next=index;
    if(e.key==='ArrowRight')next=(index+1)%editorTabs.length;
    else if(e.key==='ArrowLeft')next=(index+editorTabs.length-1)%editorTabs.length;
    else if(e.key==='Home')next=0;
    else if(e.key==='End')next=editorTabs.length-1;
    else return;
    e.preventDefault();
    activateTab(editorTabs[next],true);
  };
});

function resetGroup(ids){
  ids.forEach(id=>{
    const el=$(id);
    if(el.type==='checkbox')el.checked=el.defaultChecked;
    else if(el.tagName==='SELECT')el.selectedIndex=0;
    else{el.value=el.defaultValue;showVal(id);}
  });
  schedule();
}
$('cropreset').onclick=()=>resetGroup(['zm','ox','oy','image-rotate']);
$('tonereset').onclick=()=>resetGroup(['br','co','ga','sa']);
$('dithreset').onclick=()=>resetGroup(['mode','th','ds']);
$('redreset').onclick=()=>resetGroup(['rd','rh','rw','rf','rs','rv','ra']);

// drag the preview to pan the image
(function(){
  const pv=$('pv');
  let st=null;
  pv.addEventListener('pointerdown',e=>{
    if(!img)return;
    pv.setPointerCapture(e.pointerId);
    st={x:e.clientX,y:e.clientY,ox:+$('ox').value,oy:+$('oy').value};
    pv.style.cursor='grabbing';
  });
  pv.addEventListener('pointermove',e=>{
    if(!st)return;
    const angle=+$('image-rotate').value;
    const sideways=angle===90||angle===270;
    const rw=sideways?img.naturalHeight:img.naturalWidth;
    const rh=sideways?img.naturalWidth:img.naturalHeight;
    const sc=Math.min(W/rw,H/rh)*(+$('zm').value/100);
    const f=W/pv.clientWidth;                          // CSS px -> canvas px
    const sx=(W-rw*sc)/2,sy=(H-rh*sc)/2;               // canvas px per 100% offset
    const set=(id,v)=>{$(id).value=Math.max(-100,Math.min(100,Math.round(v)));showVal(id);};
    if(Math.abs(sx)>.5)set('ox',st.ox+(e.clientX-st.x)*f/sx*100);
    if(Math.abs(sy)>.5)set('oy',st.oy+(e.clientY-st.y)*f/sy*100);
    schedule();
  });
  const end=()=>{st=null;};
  pv.addEventListener('pointerup',end);
  pv.addEventListener('pointercancel',end);
})();

// ---------- upload ----------
function fail(m){
  $('msg').textContent='Upload failed: '+m;
  $('send').disabled=false;
}

$('send').onclick=()=>{
  if(!bits)return;
  const fd=new FormData();
  fd.append('file',new Blob([bits]),cleanBase($('name').value)+'.bin');
  const x=new XMLHttpRequest();
  x.open('POST','/api/upload');
  x.upload.onprogress=e=>{if(e.lengthComputable)$('bar').style.width=(100*e.loaded/e.total)+'%';};
  x.onload=()=>{if(x.status===200)location.href='/';else fail(x.responseText||x.status);};
  x.onerror=()=>fail('connection lost');
  $('send').disabled=true;
  $('prog').hidden=false;
  $('msg').textContent='';
  x.send(fd);
};

$('preview').onclick=()=>{
  if(!bits)return;
  const b=$('preview');
  b.disabled=true;
  $('msg').textContent='Sending preview...';
  const fd=new FormData();
  fd.append('file',new Blob([bits]),'preview.bin');
  const x=new XMLHttpRequest();
  x.open('POST','/api/preview');
  x.onload=()=>{
    b.disabled=false;
    if(x.status===202)$('msg').textContent='Preview queued for display (not saved).';
    else $('msg').textContent='Preview failed: '+(x.responseText||x.status);
  };
  x.onerror=()=>{
    b.disabled=false;
    $('msg').textContent='Preview failed: connection lost';
  };
  x.send(fd);
};
</script>
</body></html>
)HTML";