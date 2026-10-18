#!/usr/bin/env python3
"""Live heatmap / occlusion debug viewer (ADR-0022 Tier 2).

Runs a small local HTTP server that polls the payload's ``heatmap`` command (with
the world-solid enumeration) and serves a page you open in a browser. Move in-game
and the chart — hitbox silhouettes **and** the projected world solids — updates in
near real time, which is how the occlusion output gets debugged.

The agent runs in Windows session 0 (no visible desktop) and the game/browser live
in session 1; both the named pipe and ``localhost`` HTTP cross sessions for the same
user, so the server is started here and the page is opened there.

Usage:
  python scripts/heatmap-live.py                    # http://127.0.0.1:8765
  python scripts/heatmap-live.py --port 9000 --rate 15 --fov 110
  python scripts/heatmap-live.py --png artifacts/live.png   # also write each frame

Endpoints:
  /                     the viewer page (canvas + controls)
  /_data                latest heatmap payload + server status (JSON)
  /_set?k=v&…           update settings (fov, history, target, walls,
                        rate, pause, sample, clear, mode)
  /_frame.png           the last ``--png`` frame, if enabled
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from scenario.control import Control  # noqa: E402

# --------------------------------------------------------------------------- state

LOCK = threading.Lock()
STATE: dict[str, Any] = {
    "payload": None,        # the latest heatmap response
    "status": "starting",   # human-readable poller status
    "error": None,
    "poll_ms": 0.0,
    "polls": 0,
    "updated": 0.0,
    "settings": {
        "fov": 110.0,
        "history": 8,
        "target": None,
        "walls": True,
        "walls_call": True,
        "rate": 10.0,
        "pause": False,
        "all": False,
        "pen": "simple",
        "pen_mod": 1.0,
        "dmg_mod": 1.0,
    },
}
PNG_PATH: pathlib.Path | None = None
PNG_EVERY = 3  # write a frame every N polls
COMMANDS: list[tuple[str, dict[str, Any]]] = []  # queued passthrough commands


def _snapshot() -> dict[str, Any]:
    with LOCK:
        return {
            "settings": dict(STATE["settings"]),
            "status": STATE["status"],
            "error": STATE["error"],
            "poll_ms": STATE["poll_ms"],
            "polls": STATE["polls"],
            "updated": STATE["updated"],
            "payload": STATE["payload"],
        }


# ------------------------------------------------------------------------- poller

def poller(pipe: str | None, connect_timeout: float, png_every: int) -> None:
    control: Control | None = None
    frames = 0
    while True:
        with LOCK:
            s = dict(STATE["settings"])
        if s["pause"]:
            time.sleep(0.2)
            continue
        try:
            if control is None:
                with LOCK:
                    STATE["status"] = "connecting"
                control = Control(pipe).connect(timeout=connect_timeout)
                with LOCK:
                    STATE["status"] = "connected"
                    STATE["error"] = None
                # Turn sampling on once so lag records accrue for the history view.
                try:
                    control.heatmap(sample=True)
                except Exception:  # noqa: BLE001 - best effort
                    pass

            with LOCK:
                do_sample = STATE.pop("sample", False)
                do_clear = STATE.pop("clear", False)
                queued = COMMANDS[:]
                COMMANDS.clear()
            if do_sample:
                control.heatmap(sample=True)
            if do_clear:
                control.heatmap(clear=True)
            for name, cargs in queued:
                try:
                    if name == "look":
                        control.look(**cargs)
                    elif name == "exec":
                        control.exec(cargs["line"])
                    elif name == "teleport":
                        control.teleport(**cargs)
                    elif name == "press":
                        control.press(**cargs)
                except Exception as exc:  # noqa: BLE001 - report in status
                    with LOCK:
                        STATE["error"] = f"{name}: {exc}"

            started = time.perf_counter()
            result = control.heatmap(
                fov=float(s["fov"]),
                history=int(s["history"]),
                target=None if s["target"] in (None, "", "null") else int(s["target"]),
                all=bool(s["all"]),
                walls=bool(s["walls"]),
                walls_call=bool(s["walls_call"]),
                pen=s.get("pen", "simple"),
                pen_mod=float(s.get("pen_mod", 1.0)),
                dmg_mod=float(s.get("dmg_mod", 1.0)),
            )
            elapsed = (time.perf_counter() - started) * 1000.0

            with LOCK:
                STATE["payload"] = result
                STATE["poll_ms"] = elapsed
                STATE["updated"] = time.time()
                STATE["polls"] += 1
                STATE["status"] = "live"
                STATE["error"] = None

            frames += 1
            if PNG_PATH is not None and png_every > 0 and frames % png_every == 0:
                try:
                    from scenario.heatmap import render

                    mode = "penetration" if s.get("pen") == "engine" else "damage"
                    render(result, PNG_PATH, size=800, mode=mode, occluders=True)
                except Exception:  # noqa: BLE001 - a bad frame must not kill the loop
                    pass
        except Exception as exc:  # noqa: BLE001 - report and retry
            with LOCK:
                STATE["status"] = "error"
                STATE["error"] = str(exc)
            if control is not None:
                try:
                    control.close()
                except Exception:  # noqa: BLE001
                    pass
                control = None
            time.sleep(0.5)
            continue

        with LOCK:
            rate = float(STATE["settings"]["rate"]) or 10.0
        time.sleep(max(0.02, 1.0 / rate))


# --------------------------------------------------------------------------- http

PAGE = """<!doctype html>
<html><head><meta charset="utf-8"><title>cs2 heatmap live</title>
<style>
 body{background:#0a0c10;color:#c8ced8;font:13px/1.4 ui-monospace,Consolas,monospace;margin:0}
 #wrap{display:flex;gap:14px;padding:12px}
 canvas{background:#080a0e;border:1px solid #222a36;image-rendering:pixelated}
 fieldset{border:1px solid #222a36;padding:8px 10px;margin:0 0 10px 0}
 legend{color:#7f8b9e;padding:0 4px}
 label{display:flex;align-items:center;gap:6px;margin:3px 0}
 input[type=number],select{background:#12161d;color:#c8ced8;border:1px solid #2b3444;width:76px}
 input[type=range]{width:110px}
 button{background:#18202c;color:#c8ced8;border:1px solid #2b3444;padding:3px 8px;cursor:pointer}
 #stats{white-space:pre;color:#8ea0b8}
 .k{color:#7f8b9e}
</style></head><body>
<div id="wrap">
  <canvas id="c" width="760" height="760"></canvas>
  <div>
    <fieldset><legend>view</legend>
      <label>mode <select id="mode">
        <option value="cells">cells</option>
        <option value="both" selected>cells + occluders</option>
        <option value="occluders">occluders</option>
        <option value="penetration">penetration (damage)</option>
      </select></label>
      <label>pen <select id="pen">
        <option value="simple" selected>simple</option>
        <option value="engine">engine (real)</option>
      </select></label>
      <label>pen_mod <input type="number" id="pen_mod" min="0" max="3" step="0.05" value="1"></label>
      <label>dmg_mod <input type="number" id="dmg_mod" min="0" max="3" step="0.05" value="1"></label>
      <label>fov <input type="range" id="fov" min="20" max="160" step="1" value="110"><span id="fovv"></span></label>
      <label>history <input type="number" id="history" min="1" max="32" value="8"></label>
      <label>target <input type="number" id="target" placeholder="auto"></label>
      <label>rate Hz <input type="number" id="rate" min="1" max="30" value="10"></label>
    </fieldset>
    <fieldset><legend>walls</legend>
      <label><input type="checkbox" id="walls" checked> enumerate solids</label>
      <label><input type="checkbox" id="walls_call" checked> call engine query</label>
      <label><input type="checkbox" id="allplayers"> include teammates</label>
      <label><input type="checkbox" id="pause"> pause</label>
    </fieldset>
    <fieldset><legend>actions</legend>
      <button id="btn_sample">sample on</button>
      <button id="btn_clear">clear records</button>
    </fieldset>
    <div id="stats">connecting…</div>
  </div>
</div>
<script>
const C=document.getElementById('c'), X=C.getContext('2d');
const S={mode:'both'};
const STOPS=[[0,[12,18,48]],[0.25,[24,132,168]],[0.5,[52,190,96]],[0.75,[232,205,64]],[1,[222,56,40]]];
function color(x){x=Math.max(0,Math.min(1,x));
 for(let i=0;i<STOPS.length-1;i++){const a=STOPS[i],b=STOPS[i+1];
  if(x<=b[0]){const f=b[0]<=a[0]?0:(x-a[0])/(b[0]-a[0]);
   return 'rgb('+Math.round(a[1][0]+(b[1][0]-a[1][0])*f)+','+Math.round(a[1][1]+(b[1][1]-a[1][1])*f)+','+Math.round(a[1][2]+(b[1][2]-a[1][2])*f)+')';}}
 return 'rgb(222,56,40)';}
// Penetration fraction (1 = no wall, 0 = blocked): blue -> red.
function atten(x){x=Math.max(0,Math.min(1,x));const a=[70,150,255],b=[235,70,60];
 return 'rgb('+Math.round(a[0]+(b[0]-a[0])*(1-x))+','+Math.round(a[1]+(b[1]-a[1])*(1-x))+','+Math.round(a[2]+(b[2]-a[2])*(1-x))+')';}
function poly(ctx,pts){ctx.beginPath();ctx.moveTo(pts[0][0],pts[0][1]);for(let i=1;i<pts.length;i++)ctx.lineTo(pts[i][0],pts[i][1]);ctx.closePath();}

let last=null, stat={};
function draw(){
  X.fillStyle='#080a0e';X.fillRect(0,0,C.width,C.height);
  const d=last; if(!d||!d.valid){X.fillStyle='#5a6577';X.fillText('no data',10,20);return;}
  const ext=Math.max(0.0001, parseFloat(d.extent)||1.0);
  const vu=parseFloat(d.view_yaw)||0.0, vv=parseFloat(d.view_pitch)||0.0;
  const S2=C.width, k=S2/(2*ext);
  // Absolute yaw/pitch -> pixels, centred on the camera direction. Screen-right is
  // *decreasing* yaw (Source yaw increases to the left); unwrap yaw about the centre.
  const px=(u,v)=>{
    let du=u-vu;
    while(du>Math.PI)du-=2*Math.PI;
    while(du<-Math.PI)du+=2*Math.PI;
    return [S2*0.5-du*k, S2*0.5-(v-vv)*k];
  };

  const cells=d.cells||[];
  const pen=(S.mode==='penetration');
  let lo=Infinity,hi=-Infinity;
  for(const c of cells){const v=parseFloat(c.value)||0;if(v<lo)lo=v;if(v>hi)hi=v;}
  const span=(hi-lo)||1;

  if(S.mode!=='occluders'){
    X.globalAlpha=1.0;
    for(const c of cells){
      const pts=(c.poly||[]).map(p=>px(p[0],p[1])); if(pts.length<3)continue;
      X.fillStyle=color(((parseFloat(c.value)||0)-lo)/span);
      poly(X,pts);X.fill();
    }
  }
  const occ=(d.walls&&d.walls.occluder_polygons)||[];
  if(S.mode!=='cells'){
    const only=(S.mode==='occluders');
    for(const o of occ){
      const pts=o.map(p=>px(p[0],p[1])); if(pts.length<3)continue;
      X.fillStyle=only?'rgba(206,118,52,0.55)':(pen?'rgba(206,118,52,0.16)':'rgba(206,118,52,0.10)');
      poly(X,pts);X.fill();
      X.strokeStyle=only?'rgba(250,170,90,0.95)':(pen?'rgba(250,170,90,0.45)':'rgba(250,170,90,0.30)');
      X.lineWidth=1;poly(X,pts);X.stroke();
    }
  }
  // cell outlines + crosshair. In the penetration mode the outline encodes how much
  // the walls attenuated the cell (value / base_value): blue = untouched, red = gone.
  X.lineWidth=1;
  for(const c of cells){
    const pts=(c.poly||[]).map(p=>px(p[0],p[1])); if(pts.length<3)continue;
    if(pen){
      const base=parseFloat(c.base_value)||0, v=parseFloat(c.value)||0;
      X.strokeStyle=atten(base>0?v/base:1); X.globalAlpha=0.55;
    }else{
      X.strokeStyle='rgba(215,215,225,0.22)'; X.globalAlpha=1.0;
    }
    poly(X,pts);X.stroke();
  }
  X.globalAlpha=1.0;
  const cx=px(vu,vv);
  X.strokeStyle='rgba(120,220,255,0.7)';X.beginPath();
  X.moveTo(cx[0]-12,cx[1]);X.lineTo(cx[0]+12,cx[1]);X.moveTo(cx[0],cx[1]-12);X.lineTo(cx[0],cx[1]+12);X.stroke();
}

function settext(id,v){const e=document.getElementById(id);if(e)e.textContent=v;}

async function load(){
  try{
    const r=await fetch('/_data',{cache:'no-store'}); const j=await r.json();
    last=j.payload; stat=j;
    if(j.payload){draw();}
    const w=(j.payload&&j.payload.walls)||{};
    const tg=(j.payload&&j.payload.targets||[]);
    let txt='status   '+j.status+(j.error?('  '+j.error):'')+
      '\\npoll     '+(j.poll_ms||0).toFixed(0)+' ms   #'+(j.polls||0)+
      '\\ncells    '+(j.payload?(j.payload.cell_count||0):0)+
      '   occluders '+(w.occluders||0)+
      '\\nhulls    '+(w.hulls||0)+'   shapes '+(w.shapes||0)+'   records '+(w.records||0)+
      '\\nextent   '+(j.payload?j.payload.extent:'')+
      '\\npen      '+(w.penetration_model||'simple')+'   max_pen '+(w.max_penetrations||4)+
      '   pen_mod '+(w.pen_mod!=null?w.pen_mod:1)+'   dmg_mod '+(w.dmg_mod!=null?w.dmg_mod:1)+
      '\\nfov      '+(j.settings.fov)+'   history '+(j.settings.history)+
      '\\nweapon   '+JSON.stringify(j.payload?j.payload.weapon:null)+'\\n';
    for(const t of tg) txt+='  idx '+t.index+' '+(t.name||'')+'  d '+(t.distance||0).toFixed(0)+'  hp '+t.health+'\\n';
    settext('stats',txt);
    const s=j.settings;
    document.getElementById('fov').value=s.fov; settext('fovv',s.fov+'\\u00b0');
    document.getElementById('history').value=s.history;
    document.getElementById('rate').value=s.rate;
    document.getElementById('walls').checked=!!s.walls;
    document.getElementById('walls_call').checked=!!s.walls_call;
    document.getElementById('allplayers').checked=!!s.all;
    document.getElementById('pause').checked=!!s.pause;
    document.getElementById('pen').value=s.pen||'simple';
    if(document.activeElement!==document.getElementById('pen_mod')) document.getElementById('pen_mod').value=(s.pen_mod!=null?s.pen_mod:1);
    if(document.activeElement!==document.getElementById('dmg_mod')) document.getElementById('dmg_mod').value=(s.dmg_mod!=null?s.dmg_mod:1);
    if(document.activeElement!==document.getElementById('target')) document.getElementById('target').value=s.target==null?'':s.target;
  }catch(e){settext('stats','error: '+e);}
}
async function set(k,v){try{await fetch('/_set?'+k+'='+encodeURIComponent(v),{cache:'no-store'});}catch(e){}}

for(const [id,key] of [['fov','fov'],['history','history'],['rate','rate']]){
  const e=document.getElementById(id);
  e.addEventListener('input',()=>{ if(id==='fov')settext('fovv',e.value+'\\u00b0'); set(key,e.value); });
}
document.getElementById('target').addEventListener('input',e=>set('target',e.target.value));
for(const [id,key] of [['walls','walls'],['walls_call','walls_call'],['allplayers','all'],['pause','pause']]){
  document.getElementById(id).addEventListener('change',e=>set(key,e.target.checked?'1':'0'));
}
document.getElementById('mode').addEventListener('change',e=>{S.mode=e.target.value;draw();});
document.getElementById('pen').addEventListener('change',e=>set('pen',e.target.value));
document.getElementById('pen_mod').addEventListener('input',e=>set('pen_mod',e.target.value));
document.getElementById('dmg_mod').addEventListener('input',e=>set('dmg_mod',e.target.value));
document.getElementById('btn_sample').addEventListener('click',()=>set('sample','1'));
document.getElementById('btn_clear').addEventListener('click',()=>set('clear','1'));

setInterval(load, 100);
load();
</script></body></html>
"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args: Any) -> None:  # keep the console quiet
        pass

    def _send(self, code: int, body: bytes, ctype: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def _json(self, obj: Any) -> None:
        self._send(200, json.dumps(obj, default=str).encode(), "application/json")

    def do_GET(self) -> None:  # noqa: N802 - http.server API
        path, _, query = self.path.partition("?")
        if path == "/":
            return self._send(200, PAGE.encode(), "text/html; charset=utf-8")
        if path == "/_data":
            return self._json(_snapshot())
        if path == "/_set":
            return self._json(_apply(query))
        if path in ("/_look", "/_exec"):
            return self._json(_passthrough(path, query))
        if path == "/_frame.png":
            if PNG_PATH is not None and PNG_PATH.exists():
                return self._send(200, PNG_PATH.read_bytes(), "image/png")
            return self._send(404, b"no frame", "text/plain")
        return self._send(404, b"not found", "text/plain")


def _apply(query: str) -> dict[str, Any]:
    """Apply `/_set` query parameters to the live settings."""
    for pair in query.split("&"):
        if not pair:
            continue
        key, _, raw = pair.partition("=")
        from urllib.parse import unquote

        value = unquote(raw)
        with LOCK:
            s = STATE["settings"]
            if key in ("walls", "walls_call", "all", "pause"):
                s[key] = value not in ("0", "false", "False", "")
            elif key == "pen":
                s["pen"] = "engine" if value == "engine" else "simple"
            elif key in ("pen_mod", "dmg_mod"):
                try:
                    s[key] = float(value)
                except ValueError:
                    pass
            elif key in ("sample", "clear"):
                # one-shot: prompt the poller via a transient flag
                STATE[key] = value not in ("0", "false", "")
            elif key == "target":
                s["target"] = None if value == "" else value
            elif key in ("fov", "rate", "history"):
                try:
                    s[key] = float(value) if key != "history" else int(value)
                except ValueError:
                    pass
    return _snapshot()["settings"]


def _passthrough(path: str, query: str) -> dict[str, Any]:
    """Queue a game command (`/_look?yaw=&pitch=`, `/_exec?line=`) for the poller.

    The viewer holds the payload's single-client pipe, so anything else (a probe
    script, the aim tooling) must go through here while it is running.
    """
    from urllib.parse import parse_qs, unquote

    params = {k: v[0] for k, v in parse_qs(query).items()}
    with LOCK:
        if path == "/_look":
            cargs: dict[str, Any] = {}
            for key in ("yaw", "pitch", "roll"):
                if params.get(key) not in (None, ""):
                    cargs[key] = float(params[key])
            COMMANDS.append(("look", cargs))
        else:
            COMMANDS.append(("exec", {"line": unquote(params.get("line", ""))}))
        depth = len(COMMANDS)
    return {"queued": path[1:], "pending": depth}


def main(argv: list[str]) -> int:
    global PNG_PATH
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--pipe", default=None)
    parser.add_argument("--fov", type=float, default=110.0)
    parser.add_argument("--history", type=int, default=8)
    parser.add_argument("--rate", type=float, default=10.0, help="payload polls per second")
    parser.add_argument("--no-walls", action="store_true", help="start with solids disabled")
    parser.add_argument("--png", default=None,
                        help="also write the latest frame here (for headless inspection)")
    parser.add_argument("--png-every", type=int, default=PNG_EVERY)
    parser.add_argument("--pen", choices=("simple", "engine"), default="simple",
                        help="penetration model (docs/re/penetration.md)")
    parser.add_argument("--pen-mod", type=float, default=1.0,
                        help="per-surface penetration modifier (engine model)")
    parser.add_argument("--dmg-mod", type=float, default=1.0,
                        help="per-surface damage modifier (engine model)")
    parser.add_argument("--connect-timeout", type=float, default=20.0)
    args = parser.parse_args(argv)

    if args.png:
        PNG_PATH = pathlib.Path(args.png)
        PNG_PATH.parent.mkdir(parents=True, exist_ok=True)

    STATE["settings"].update({
        "fov": args.fov,
        "history": args.history,
        "rate": args.rate,
        "walls": not args.no_walls,
        "walls_call": not args.no_walls,
        "pen": args.pen,
        "pen_mod": args.pen_mod,
        "dmg_mod": args.dmg_mod,
    })

    thread = threading.Thread(target=poller, args=(args.pipe, args.connect_timeout, args.png_every),
                              daemon=True)
    thread.start()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    url = f"http://{args.host}:{args.port}/"
    print(f"[heatmap-live] serving {url}  (pipe={args.pipe or 'default'})", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
