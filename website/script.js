(function(){
  const cv = document.getElementById('space');
  const ctx = cv.getContext('2d');
  const reduce = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  let W, H, DPR, cx, cy, F;
  function resize(){
    DPR = Math.min(window.devicePixelRatio || 1, 2);
    W = cv.width = Math.floor(innerWidth * DPR);
    H = cv.height = Math.floor(innerHeight * DPR);
    cv.style.width = innerWidth + 'px';
    cv.style.height = innerHeight + 'px';
    cx = W * 0.5; cy = H * 0.5;
    F = Math.min(W, H);
  }
  resize();
  addEventListener('resize', resize);

  // ---- perspective starfield (mirrors the in-game Starfield projection) ----
  const NSTARS = Math.round(260 * (innerWidth > 700 ? 1 : 0.55));
  const stars = [];
  function spawnStar(s){
    s.x = (Math.random()*2-1) * W;
    s.y = (Math.random()*2-1) * H;
    s.z = Math.random() * W + 1;
    s.pz = s.z;
  }
  for(let i=0;i<NSTARS;i++){ const s={}; spawnStar(s); stars.push(s); }

  // ---- icosphere mesh (same icosahedron as SystemFlight.h) ----
  const A=0.5257311, B=0.8506508;
  const V = [
    [-A,B,0],[A,B,0],[-A,-B,0],[A,-B,0],
    [0,-A,B],[0,A,B],[0,-A,-B],[0,A,-B],
    [B,0,-A],[B,0,A],[-B,0,-A],[-B,0,A]
  ];
  const FACES = [
    [0,11,5],[0,5,1],[0,1,7],[0,7,10],[0,10,11],
    [1,5,9],[5,11,4],[11,10,2],[10,7,6],[7,1,8],
    [3,9,4],[3,4,2],[3,2,6],[3,6,8],[3,8,9],
    [4,9,5],[2,4,11],[6,2,10],[8,6,7],[9,8,1]
  ];

  function rot(p, ax, ay){
    let [x,y,z]=p;
    // yaw
    let cxr=Math.cos(ay), sxr=Math.sin(ay);
    let x1 = x*cxr + z*sxr, z1 = -x*sxr + z*cxr;
    // pitch
    let cyr=Math.cos(ax), syr=Math.sin(ax);
    let y1 = y*cyr - z1*syr, z2 = y*syr + z1*cyr;
    return [x1, y1, z2];
  }

  function norm(v){const l=Math.hypot(v[0],v[1],v[2])||1; return [v[0]/l,v[1]/l,v[2]/l];}
  function cross(a,b){return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]];}

  // Solid Lambert-shaded icosphere — same look as the game's renderIcoSphere:
  // opaque per-face triangles, lit from a fixed sun, no wireframe.
  const LIGHT = norm([-0.45, 0.55, -0.70]);   // sun direction
  function drawSphere(ox, oy, R, rotX, rotY, base, alpha){
    const cam = 4.2;
    const rv = V.map(v=>rot(v, rotX, rotY));   // rotated unit verts
    const pts = rv.map(r=>{
      const s = (R*cam) / (cam + r[2]);
      return { sx: ox + r[0]*s, sy: oy - r[1]*s };
    });
    const a0 = alpha == null ? 1 : alpha;
    for(const f of FACES){
      const a=pts[f[0]], b=pts[f[1]], c=pts[f[2]];
      const area = (b.sx-a.sx)*(c.sy-a.sy) - (b.sy-a.sy)*(c.sx-a.sx);
      if(area <= 0) continue;                  // backface cull
      // outward normal = centroid of the unit-sphere verts
      const nx=rv[f[0]][0]+rv[f[1]][0]+rv[f[2]][0];
      const ny=rv[f[0]][1]+rv[f[1]][1]+rv[f[2]][1];
      const nz=rv[f[0]][2]+rv[f[1]][2]+rv[f[2]][2];
      const nl=Math.hypot(nx,ny,nz)||1;
      let ld=(nx*LIGHT[0]+ny*LIGHT[1]+nz*LIGHT[2])/nl;
      if(ld<0) ld=0;
      const k=0.16 + 0.84*ld;                  // ambient floor + lambert
      ctx.beginPath();
      ctx.moveTo(a.sx,a.sy); ctx.lineTo(b.sx,b.sy); ctx.lineTo(c.sx,c.sy); ctx.closePath();
      ctx.fillStyle='rgba('+Math.round(base[0]*k)+','+Math.round(base[1]*k)+','+Math.round(base[2]*k)+','+a0+')';
      ctx.fill();
    }
  }

  // ---- tilted jump-gate ring (magenta wireframe) ----
  function drawGate(t, ox, oy, R, spin){
    const seg=22, tilt=1.12;
    ctx.lineWidth = 1.1*DPR;
    ctx.strokeStyle = 'rgba(255,77,207,0.42)';
    ctx.beginPath();
    for(let i=0;i<=seg;i++){
      const a = (i/seg)*Math.PI*2 + spin;
      const x = Math.cos(a)*R;
      const y = Math.sin(a)*R;
      // tilt around X then small yaw
      const yt = y*Math.cos(tilt);
      const zt = y*Math.sin(tilt);
      const sx = ox + x;
      const sy = oy + yt - zt*0.18;
      if(i===0) ctx.moveTo(sx,sy); else ctx.lineTo(sx,sy);
    }
    ctx.stroke();
    // a couple of strut nodes
    ctx.fillStyle='rgba(255,77,207,0.55)';
    for(let i=0;i<4;i++){
      const a=(i/4)*Math.PI*2 + spin;
      const x=Math.cos(a)*R, y=Math.sin(a)*R;
      const sx=ox+x, sy=oy+y*Math.cos(tilt)-y*Math.sin(tilt)*0.18;
      ctx.fillRect(sx-1.5*DPR, sy-1.5*DPR, 3*DPR, 3*DPR);
    }
  }

  // ---- flying ships (meshes ported from Ship3D.h) ----
  const MESHES = {
    wedge:{
      v:[[0,0,1.10],[0.60,0,-0.20],[-0.60,0,-0.20],[0.35,0,-0.70],[-0.35,0,-0.70],
         [0.12,0,-0.78],[-0.12,0,-0.78],[0,0.18,-0.05],[0,0.10,0.45],[0,0.14,-0.55],[0,-0.08,-0.40]],
      e:[[0,1],[0,2],[1,3],[2,4],[3,5],[4,6],[5,6],[0,8],[8,7],[7,9],[9,5],[9,6],[7,1],[7,2],[0,10],[10,3],[10,4]]
    },
    interceptor:{
      v:[[0,0,1.40],[0,0.18,0.30],[0.55,-0.05,-0.40],[-0.55,-0.05,-0.40],[0.12,0,-0.85],
         [-0.12,0,-0.85],[0,0.15,-0.55],[0.20,0.06,0.10],[-0.20,0.06,0.10]],
      e:[[0,1],[1,6],[6,4],[6,5],[0,4],[0,5],[1,2],[2,4],[1,3],[3,5],[7,2],[8,3],[1,7],[1,8],[4,5]]
    },
    gunship:{
      v:[[0,0,1.00],[0,0.22,0.30],[0.45,0.20,0.10],[-0.45,0.20,0.10],[0.45,0.45,0.10],
         [-0.45,0.45,0.10],[0.80,0,-0.20],[-0.80,0,-0.20],[0,-0.20,0.30],[0,0.20,-0.70],
         [0.30,0,-0.85],[-0.30,0,-0.85]],
      e:[[0,1],[1,9],[0,8],[8,10],[8,11],[1,6],[6,10],[1,7],[7,11],[2,4],[3,5],[1,2],[1,3],[2,6],[3,7],[9,10],[9,11],[10,11]]
    },
    alien:{
      v:[[0,0,1.20],[0,0,-1.00],[0.65,0,0],[-0.65,0,0],[0,0.55,0],[0,-0.55,0],
         [0.30,0.18,0.45],[-0.30,0.18,0.45],[0.30,-0.18,0.45],[-0.30,-0.18,0.45]],
      e:[[0,1],[0,2],[0,3],[0,4],[0,5],[1,2],[1,3],[1,4],[1,5],[2,4],[4,3],[3,5],[5,2],
         [6,7],[7,9],[9,8],[8,6],[0,6],[0,7],[0,8],[0,9]]
    },
    freighter:{
      v:[[0,0.10,1.30],[0,-0.10,1.30],[0.50,0.35,0.50],[-0.50,0.35,0.50],[0.50,-0.30,0.50],
         [-0.50,-0.30,0.50],[0.50,0.35,-0.80],[-0.50,0.35,-0.80],[0.50,-0.30,-0.80],
         [-0.50,-0.30,-0.80],[0,0,-1.00]],
      e:[[0,2],[0,3],[1,4],[1,5],[0,1],[2,3],[2,4],[3,5],[4,5],[2,6],[3,7],[4,8],[5,9],
         [6,7],[6,8],[7,9],[8,9],[6,10],[7,10],[8,10],[9,10]]
    },
    barge:{
      v:[[0,0,0.95],[0.45,0.25,0.55],[-0.45,0.25,0.55],[0.45,-0.25,0.55],[-0.45,-0.25,0.55],
         [0.55,0.30,-0.85],[-0.55,0.30,-0.85],[0.55,-0.30,-0.85],[-0.55,-0.30,-0.85],[0,-0.45,0.75]],
      e:[[0,1],[0,2],[0,3],[0,4],[1,2],[1,3],[2,4],[3,4],[1,5],[2,6],[3,7],[4,8],
         [5,6],[5,7],[6,8],[7,8],[9,3],[9,4],[9,0]]
    }
  };
  const SHIP_KINDS = [
    {m:'wedge',       c:[52,227,240]},
    {m:'interceptor', c:[255,77,207]},
    {m:'gunship',     c:[124,255,89]},
    {m:'freighter',   c:[255,157,46]},
    {m:'barge',       c:[134,127,255]},
    {m:'alien',       c:[255,77,207]},
    {m:'wedge',       c:[255,157,46]}
  ];

  const SHIPN = innerWidth > 760 ? 3 : 2;
  const ships = [];
  function focal(){ return Math.min(W,H); }
  function spawnShip(s){
    const f = focal();
    const k = SHIP_KINDS[Math.floor(Math.random()*SHIP_KINDS.length)];
    s.mesh = MESHES[k.m]; s.col = k.c;
    s.z = f*1.3 + Math.random()*f*1.8;
    const yb = (H*0.5)*s.z/f;
    s.y = (Math.random()*0.9 - 0.5) * yb;
    s.dir = Math.random()<0.5 ? 1 : -1;
    const xb = (W*0.5)*s.z/f;
    s.x = -s.dir*(xb + s.z*0.18);
    const sp = f*0.10 + Math.random()*f*0.10;     // world units / sec
    s.vx = s.dir*sp;
    s.vy = (Math.random()*2-1)*sp*0.06;
    s.vz = (Math.random()*2-1)*sp*0.10;
    s.scale = f*(0.055 + Math.random()*0.04);
    // hold a stable attitude with a slight, fixed bank — no constant rolling
    s.bank = (Math.random()*2-1)*0.4;
  }
  for(let i=0;i<SHIPN;i++){ const s={}; spawnShip(s); ships.push(s); }

  function hull2d(pts){            // gift-wrap convex hull -> ordered pts
    const n=pts.length; if(n<3) return pts;
    let start=0;
    for(let i=1;i<n;i++){ if(pts[i].x<pts[start].x || (pts[i].x===pts[start].x && pts[i].y<pts[start].y)) start=i; }
    const out=[]; let cur=start, guard=0;
    do{
      out.push(pts[cur]); let nx=-1;
      for(let j=0;j<n;j++){
        if(j===cur) continue;
        if(nx===-1){ nx=j; continue; }
        const cz=(pts[nx].x-pts[cur].x)*(pts[j].y-pts[cur].y)-(pts[nx].y-pts[cur].y)*(pts[j].x-pts[cur].x);
        if(cz<0) nx=j;
      }
      cur=nx;
    } while(cur!==start && ++guard<40);
    return out;
  }

  function drawShip(s){
    const f = focal();
    let fwd = norm([s.vx,s.vy,s.vz]);
    let right = norm(cross([0,1,0], fwd));
    let up = cross(fwd, right);
    // bank: rotate right/up around fwd
    const cb=Math.cos(s.bank), sb=Math.sin(s.bank);
    const r2=[right[0]*cb+up[0]*sb, right[1]*cb+up[1]*sb, right[2]*cb+up[2]*sb];
    const u2=[up[0]*cb-right[0]*sb, up[1]*cb-right[1]*sb, up[2]*cb-right[2]*sb];
    right=r2; up=u2;

    const pv=[];
    for(const v of s.mesh.v){
      const vx=v[0]*s.scale, vy=v[1]*s.scale, vz=v[2]*s.scale;
      const x = s.x + vx*right[0] + vy*up[0] + vz*fwd[0];
      const y = s.y + vx*right[1] + vy*up[1] + vz*fwd[1];
      const z = s.z + vx*right[2] + vy*up[2] + vz*fwd[2];
      if(z<8){ pv.push(null); continue; }
      pv.push({x: cx + x*f/z, y: cy - y*f/z});
    }
    const depth = Math.max(0, Math.min(1, (f*3.1 - s.z)/(f*1.9)));
    const c = s.col;
    // filled silhouette
    const vis = pv.filter(Boolean);
    if(vis.length>=3){
      const h = hull2d(vis);
      if(h.length>=3){
        ctx.beginPath(); ctx.moveTo(h[0].x,h[0].y);
        for(let i=1;i<h.length;i++) ctx.lineTo(h[i].x,h[i].y);
        ctx.closePath();
        ctx.fillStyle='rgba('+Math.round(c[0]*0.5)+','+Math.round(c[1]*0.5)+','+Math.round(c[2]*0.5)+','+(0.55+depth*0.4)+')';
        ctx.fill();
      }
    }
    // wireframe edges
    ctx.lineWidth = (0.6 + depth*0.9)*DPR;
    ctx.strokeStyle='rgba('+c[0]+','+c[1]+','+c[2]+','+(0.3+depth*0.5)+')';
    ctx.beginPath();
    for(const e of s.mesh.e){
      const a=pv[e[0]], b=pv[e[1]]; if(!a||!b) continue;
      ctx.moveTo(a.x,a.y); ctx.lineTo(b.x,b.y);
    }
    ctx.stroke();
  }

  function updateShips(dt){
    const f=focal();
    for(const s of ships){
      s.x += s.vx*dt; s.y += s.vy*dt; s.z += s.vz*dt;
      const xb=(W*0.5)*s.z/f + s.z*0.25;
      if((s.dir>0 && s.x>xb) || (s.dir<0 && s.x<-xb) || s.z<f*0.6) spawnShip(s);
    }
  }

  let t=0, last=0;
  function frame(ts){
    const dt = last ? Math.min(0.05,(ts-last)/1000) : 0.016; last=ts;
    t += dt;
    ctx.clearRect(0,0,W,H);

    // starfield
    const speed = 5.5;
    ctx.save();
    for(const s of stars){
      s.pz = s.z;
      s.z -= speed;
      if(s.z < 1){ spawnStar(s); continue; }
      const k = F*0.9;
      const sx = cx + (s.x / s.z) * k;
      const sy = cy + (s.y / s.z) * k;
      if(sx<0||sx>W||sy<0||sy>H) continue;
      const px = cx + (s.x / s.pz) * k;
      const py = cy + (s.y / s.pz) * k;
      const depth = 1 - s.z / W;          // 0 far .. 1 near
      const bright = 0.25 + depth*0.75;
      // warm-tint the nearest stars toward amber, far ones cyan-white
      if(depth > 0.78){
        ctx.strokeStyle = 'rgba(255,180,120,'+bright+')';
      } else {
        ctx.strokeStyle = 'rgba(200,232,240,'+ (bright*0.9) +')';
      }
      ctx.lineWidth = (0.5 + depth*1.6) * DPR;
      ctx.beginPath();
      ctx.moveTo(px,py); ctx.lineTo(sx,sy);
      ctx.stroke();
    }
    ctx.restore();

    // flying ships — real Ship3D meshes drifting across the void
    updateShips(dt);
    for(const s of ships) drawShip(s);

    // primary shaded planet — upper right
    const pR = F * (innerWidth>900 ? 0.19 : 0.15);
    const pX = W * (innerWidth>900 ? 0.78 : 0.82);
    const pY = H * (innerWidth>900 ? 0.34 : 0.16);
    drawSphere(pX, pY, pR, 0.42, t*0.16, [228,142,54]);   // solid shaded amber world

    // distant jump gate — lower left
    drawGate(t, W*0.20, H*0.78, F*0.085, t*0.25);

    if(!reduce) requestAnimationFrame(frame);
  }

  if(reduce){
    // one static composed frame
    ctx.clearRect(0,0,W,H);
    const k=F*0.9;
    ctx.fillStyle='rgba(200,232,240,0.7)';
    for(const s of stars){
      const sx=cx+(s.x/s.z)*k, sy=cy+(s.y/s.z)*k;
      if(sx<0||sx>W||sy<0||sy>H) continue;
      ctx.fillRect(sx,sy,1.4*DPR,1.4*DPR);
    }
    ships.forEach((s,i)=>{ s.x=(i-(SHIPN-1)/2)*s.z*0.35; s.y=(i%2?1:-1)*H*0.12; drawShip(s); });
    const pR=F*(innerWidth>900?0.19:0.15), pX=W*(innerWidth>900?0.78:0.82), pY=H*(innerWidth>900?0.34:0.16);
    drawSphere(pX,pY,pR,0.42,0.6,[228,142,54]);
    drawGate(0,W*0.20,H*0.78,F*0.085,0.4);
  } else {
    requestAnimationFrame(frame);
  }

  // ---- screenshot fallback: show a 240x135 placeholder until real art is dropped in ----
  const PH = 'data:image/svg+xml,' + encodeURIComponent(
    "<svg xmlns='http://www.w3.org/2000/svg' width='240' height='135' viewBox='0 0 240 135'>"+
    "<rect width='240' height='135' fill='#06090f'/>"+
    "<g fill='#0d1a20'>"+
      "<rect y='14' width='240' height='1'/><rect y='28' width='240' height='1'/>"+
      "<rect y='42' width='240' height='1'/><rect y='56' width='240' height='1'/>"+
      "<rect y='70' width='240' height='1'/><rect y='84' width='240' height='1'/>"+
      "<rect y='98' width='240' height='1'/><rect y='112' width='240' height='1'/>"+
    "</g>"+
    "<rect x='6' y='6' width='228' height='123' fill='none' stroke='#34e3f0' stroke-opacity='0.45' stroke-dasharray='5 4'/>"+
    "<polygon points='120,46 134,66 120,62 106,66' fill='none' stroke='#ff9d2e' stroke-opacity='0.8'/>"+
    "<text x='120' y='86' fill='#34e3f0' font-family='monospace' font-size='11' letter-spacing='3' text-anchor='middle'>SCREENSHOT</text>"+
    "<text x='120' y='102' fill='#4c5d6b' font-family='monospace' font-size='8' letter-spacing='2' text-anchor='middle'>240 x 135</text>"+
    "</svg>"
  );
  document.querySelectorAll('img.shot-img').forEach(img=>{
    const real = img.getAttribute('src');
    img.addEventListener('error', function onErr(){ img.removeEventListener('error', onErr); img.src = PH; });
    img.src = real;   // re-trigger load so a missing file falls back cleanly
  });
})();
