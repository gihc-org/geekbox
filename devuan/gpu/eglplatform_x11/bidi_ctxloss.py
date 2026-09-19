#!/usr/bin/env python3
"""Minimal WebDriver BiDi-klient: injicer webglcontextlost-lytter i spilsiden og
læs statusMessage (Firefox 140 har ikke længere CDP — kun BiDi)."""
import base64
import json
import os
import signal
import socket
import struct
import sys
import threading
import time
import urllib.request


def http_json(url, data=None, method="GET"):
    req = urllib.request.Request(url, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read().decode())


class Ws:
    def __init__(self, url):
        key = base64.b64encode(os.urandom(16)).decode()
        u = url.replace("ws://", "").replace("http://", "")
        if "/" in u:
            hostport, path = u.split("/", 1)
            path = "/" + path
        else:
            hostport, path = u, "/"
        if ":" in hostport:
            host, port = hostport.rsplit(":", 1)
            port = int(port)
        else:
            host, port = hostport, 80
        req = (
            "GET %s HTTP/1.1\r\n" % path +
            "Host: localhost:9222\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n" % key
        )
        self.s = socket.create_connection((host, port), timeout=5)
        self.s.sendall(req.encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += self.s.recv(4096)
        if b"101" not in buf.split(b"\r\n", 1)[0]:
            raise RuntimeError("ws handshake fejlede: %r" % buf[:200])

    def send(self, obj):
        payload = json.dumps(obj).encode()
        mask = os.urandom(4)
        n = len(payload)
        hdr = bytearray([0x81])
        if n < 126:
            hdr.append(0x80 | n)
        elif n < 65536:
            hdr.append(0x80 | 126)
            hdr += struct.pack(">H", n)
        else:
            hdr.append(0x80 | 127)
            hdr += struct.pack(">Q", n)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.s.sendall(bytes(hdr) + mask + masked)

    def recv(self, timeout=10):
        self.s.settimeout(timeout)
        hdr = self.s.recv(2)
        if len(hdr) < 2:
            return None
        opcode = hdr[0] & 0x0F
        n = hdr[1] & 0x7F
        if n == 126:
            n = struct.unpack(">H", self._exact(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", self._exact(8))[0]
        mask = self._exact(4) if hdr[1] & 0x80 else None
        data = self._exact(n)
        if mask:
            data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        if opcode == 1:
            return json.loads(data.decode())
        return None

    def _exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.s.recv(n - len(buf))
            if not chunk:
                break
            buf += chunk
        return buf


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "inject"
    # vent på at porten åbner (Firefox/remote-agent starter langsomt)
    ready = False
    for attempt in range(40):
        try:
            s = socket.create_connection(("127.0.0.1", 9222), timeout=3)
            s.close()
            ready = True
            break
        except Exception as e:
            time.sleep(5)
    if not ready:
        print("PORT 9222 ÅBNEDE ALDRIG")
        sys.exit(1)
    ws = None
    for attempt in range(6):
        try:
            ws = Ws("ws://127.0.0.1:9222/session")
            break
        except Exception as e:
            print("Ws-forbindelse forsøg %d: %s" % (attempt, e))
            time.sleep(6)
    if ws is None:
        print("KUNNE IKKE FORBINDE TIL /session")
        sys.exit(1)
    stop = [False]
    call_lock = threading.Lock()

    def _sig(signum, frame):
        stop[0] = True

    signal.signal(signal.SIGTERM, _sig)
    signal.signal(signal.SIGINT, _sig)
    mid = 0

    def call(method, params=None, timeout=20):
        nonlocal mid
        with call_lock:
            mid += 1
            myid = mid
            ws.send({"id": myid, "method": method, "params": params or {}})
            while True:
                m = ws.recv(timeout=timeout)
                if m and m.get("id") == myid:
                    return m
                if m and m.get("type") == "event":
                    continue
                return m

    r = call("session.new", {"capabilities": {"alwaysMatch": {}}}, timeout=50)
    if r.get("type") != "success":
        print("session.new FEJLEDE:", json.dumps(r)[:250])
        sys.exit(1)

    tree = call("browsingContext.getTree", {})
    contexts = []
    def walk(items):
        for it in items or []:
            contexts.append(it)
            walk(it.get("children"))
    walk(tree.get("result", {}).get("contexts", []))
    ctx = None
    for c in contexts:
        url = c.get("url", "")
        if "poki" in url or "subway" in url:
            ctx = c.get("context")
            print("kontekst:", ctx, url)
            break
    game_ctx = None
    for c in contexts:
        url = c.get("url", "")
        if "gdn.poki.com" in url or "index.html" in url:
            game_ctx = c.get("context")
            print("spil-kontekst:", game_ctx, url[:80])
            break
    if not ctx:
        if mode in ("preload", "preloadmin") and contexts:
            ctx = contexts[0].get("context")
            print("bruger top-kontekst:", ctx,
                  [(c.get("context"), c.get("url")) for c in contexts])
        else:
            print("INGEN SIDE-KONTEKST; har:",
                  [(c.get("context"), c.get("url")) for c in contexts])
            sys.exit(1)

    if mode == "inject":
        expr = (
            "window.__ctxLoss=[];"
            "function __cap(g,cv,t){try{"
            "if(!g||g.__capdone)return;"
            "try{dump('CAPATTRS '+t+' canvas='+cv.width+'x'+cv.height+' attrs='+"
            "JSON.stringify(g.getContextAttributes?g.getContextAttributes():{})+'\\n');}catch(x){}"
            "g.__capdone=1;g.__capLog={};"
            "try{var se=g.getSupportedExtensions?g.getSupportedExtensions():[];"
            "dump('CAPEXT '+t+' n='+se.length+' '+se.join(',')+'\\n');}catch(x){}"
            "try{var ge=g.getExtension.bind(g);"
            "g.getExtension=function(n){var res;"
            "try{res=ge(n);}catch(e){throw e;}"
            "try{dump('CAPGETEXT '+n+' -> '+(res?'OK':'NULL')+'\\n');}catch(x){}"
            "return res;};}catch(x){}"
            "try{var gp=g.getParameter.bind(g);"
            "g.getParameter=function(k){"
            "var v=gp(k);"
            "try{var keys=[g.MAX_TEXTURE_SIZE,g.MAX_CUBE_MAP_TEXTURE_SIZE,"
            "g.MAX_RENDERBUFFER_SIZE,g.MAX_VIEWPORT_DIMS,g.ALIASED_LINE_WIDTH_RANGE,"
            "g.MAX_VERTEX_ATTRIBS,g.MAX_VERTEX_UNIFORM_VECTORS,g.MAX_VARYING_VECTORS,"
            "g.MAX_COMBINED_TEXTURE_IMAGE_UNITS,g.MAX_VERTEX_TEXTURE_IMAGE_UNITS,"
            "g.MAX_FRAGMENT_UNIFORM_VECTORS,g.MAX_TEXTURE_IMAGE_UNITS,"
            "g.MAX_DRAW_BUFFERS,g.MAX_COLOR_ATTACHMENTS,g.MAX_SAMPLES,"
            "g.RED_BITS,g.GREEN_BITS,g.BLUE_BITS,g.ALPHA_BITS,g.DEPTH_BITS,g.STENCIL_BITS];"
            "for(var i=0;i<keys.length;i++){if(k===keys[i]&&!g.__capLog[k]){"
            "g.__capLog[k]=1;"
            "var vv=(v&&v.length&&v.length>1)?Array.prototype.slice.call(v).join(','):v;"
            "dump('CAPPARAM '+t+' '+k+' = '+vv+'\\n');break;}}}catch(x){}"
            "return v;};}catch(x){}"
            "}catch(e){}}"
            "function __hook(cv){try{"
            "cv.addEventListener('webglcontextlost',function(e){"
            "var o={msg:e.statusMessage||'',t:Date.now(),url:location.href};"
            "window.__ctxLoss.push(o);"
            "try{dump('CTXLOST ['+o.t+'] statusMessage='+(e.statusMessage||'(tom)')+'\\n');}catch(x){}"
            "});"
            "cv.addEventListener('webglcontextrestored',function(e){"
            "window.__ctxLoss.push({restored:true,t:Date.now()});"
            "try{dump('CTXRESTORED\\n');}catch(x){}"
            "});}catch(e){}}"
            "try{document.querySelectorAll('canvas').forEach(__hook);}catch(e){}"
            "var __gc=HTMLCanvasElement.prototype.getContext;"
            "HTMLCanvasElement.prototype.getContext=function(){"
            "var r=__gc.apply(this,arguments);__hook(this);return r;};"
            "if(typeof OffscreenCanvas!=='undefined'){"
            "var __ogc=OffscreenCanvas.prototype.getContext;"
            "OffscreenCanvas.prototype.getContext=function(){"
            "var r=__ogc.apply(this,arguments);__hook(this);return r;};}"
            "'lytter-klar'"
        )
        r = call("script.evaluate", {
            "expression": expr, "target": {"context": ctx},
            "awaitPromise": False, "returnByValue": True,
        })
        print("INJICERET:", json.dumps(r)[:300])
        if len(sys.argv) > 2 and sys.argv[2] == "wait":
            time.sleep(int(sys.argv[3]) if len(sys.argv) > 3 else 100)
            r = call("script.evaluate", {
                "expression": "JSON.stringify(window.__ctxLoss||'ingen-data')",
                "target": {"context": ctx},
                "awaitPromise": False, "returnByValue": True,
            })
            print("CTXLOSS-DATA:", json.dumps(r)[:800])
        elif len(sys.argv) > 2 and sys.argv[2] == "reload":
            r = call("browsingContext.reload", {"context": ctx})
            print("GENINDLASTET:", json.dumps(r)[:200])
    elif mode in ("preload", "preloadmin"):
        # Fælles måling i BEGGE varianter (så kørslerne kan sammenlignes):
        # rAF-tælling pr. 10 s = opnået frame-rate i browseren. NB: eglSwap-
        # Buffers kaldes IKKE i denne konfiguration (software-layers), så
        # målingen skal ligge i JS-laget.
        # 19. sep 2026 (fps-sporet): rAF-linjen maaler nu OGSAA hvor lang tid
        # sidens egen frame bruger, og hvor meget af den der ligger inde i
        # draw-kaldene. Sammenholdt med intervallet (raf=N/10 s) deler det
        # frame-budgettet i "spillets arbejde" mod "browser/compositor".
        # NB: __raf0 fanges foer patchningen, saa vores egen taelle-loekke
        # ikke selv bliver maalt.
        pre_measure = (
            "var __raf0=window.requestAnimationFrame;"
            "window.__rafwork=0;window.__rafn=0;"
            "if(__raf0){window.requestAnimationFrame=function(cb){"
            "return __raf0.call(window,function(ts){var __a=performance.now();"
            "try{return cb(ts);}finally{"
            "window.__rafwork+=performance.now()-__a;window.__rafn++;}});};}"
            "window.__frames=0;"
            "(function(){function t(){window.__frames++;"
            "try{(__raf0||requestAnimationFrame).call(window,t);}catch(e){}}"
            "try{(__raf0||requestAnimationFrame).call(window,t);}catch(e){}})();"
            "setInterval(function(){"
            "try{dump('FPS tr='+Math.round((window.performance&&"
            "performance.now?performance.now():0)/1000)+'s raf='+"
            "window.__frames+'/10s draws='+"
            "(window.__dc|0)+' rafwork='+(window.__rafn?"
            "Math.round(window.__rafwork/window.__rafn):0)+'ms/'+"
            "window.__rafn+'f drawms='+Math.round(window.__drawms||0)+'ms vis='+"
            "document.visibilityState+"
            "' focus='+(document.hasFocus?document.hasFocus():'?')+"
            "' href='+String(location.href).slice(0,70)+'\\n');"
            "window.__frames=0;window.__rafwork=0;window.__rafn=0;"
            "window.__drawms=0;"
            "if(window.__dc)window.__dc=0;}catch(x){}},10000);"
        )
        # preloadmin: KUN alpha-shim + loseContext-blok (ingen per-draw-JS-
        # wrappers, ingen __cap, ingen 10-s-intervaller) — bruges til fps-
        # sammenligning hvor shimmen skal være så billig som muligt.
        pre_min = (
            "function(){"
            "try{"
            "try{dump('PRELOAD-MIN aktiv i '+location.href+'\\n');}catch(x){}"
            "window.__ctxLoss=[];"
            "function __hook(cv){try{"
            "cv.addEventListener('webglcontextlost',function(e){"
            "window.__ctxLoss.push({t:Date.now(),url:location.href});"
            "try{dump('CTXLOST '+location.href+'\\n');}catch(x){}"
            "});}catch(e){}}"
            "function __alpha(args){"
            "if(args[1]&&typeof args[1]==='object'){"
            "var a={};for(var k in args[1])a[k]=args[1][k];"
            "if(a.alpha===false){a.alpha=true;try{dump('ALPHA-SHIM: alpha tvunget true\\n');}catch(x){}}"
            "if(a.premultipliedAlpha===false){a.premultipliedAlpha=true;try{dump('ALPHA-SHIM: premultipliedAlpha tvunget true\\n');}catch(x){}}"
            "args[1]=a;}return args;}"
            "var __gc=HTMLCanvasElement.prototype.getContext;"
            "HTMLCanvasElement.prototype.getContext=function(){"
            "var args=__alpha(Array.prototype.slice.call(arguments));"
            "var r=__gc.apply(this,args);__hook(this);"
            "try{dump('GETCONTEXT canvas='+this.width+'x'+this.height+' type='+arguments[0]+' result='+(r?'OK':'NULL')+' attrs='+JSON.stringify(r&&r.getContextAttributes?r.getContextAttributes():{})+'\\n');}catch(x){}"
            "try{if(r&&r.getExtension){"
            "var __ge=r.getExtension.bind(r);"
            "r.getExtension=function(n){"
            "if(String(n).toUpperCase()==='WEBGL_LOSE_CONTEXT'){"
            "return{loseContext:function(){},restoreContext:function(){},__blokeret:true};}"
            "return __ge(n);};}}catch(e){}"
            "return r;};"
            "if(typeof OffscreenCanvas!=='undefined'){"
            "var __ogc=OffscreenCanvas.prototype.getContext;"
            "OffscreenCanvas.prototype.getContext=function(){"
            "var args=__alpha(Array.prototype.slice.call(arguments));"
            "var r=__ogc.apply(this,args);__hook(this);return r;};}"
            "try{dump('CAPNAV hw='+navigator.hardwareConcurrency+'\\n');}catch(x){}"
            "window.addEventListener('error',function(e){"
            "try{dump('PAGE-ERROR: '+e.message+'\\n');}catch(x){}});"
            + pre_measure +
            "}catch(e){}}"
        )
        pre_full = (
            "function(){"
            "try{"
            "try{dump('PRELOAD-AKTIV i '+location.href+'\\n');}catch(x){}"
            "window.__ctxLoss=[];"
            "function __hook(cv){try{"
            "cv.addEventListener('webglcontextlost',function(e){"
            "var o={msg:e.statusMessage||'',t:Date.now(),url:location.href,"
            "canvas:(cv.id||cv.className||cv.width+'x'+cv.height)};"
            "window.__ctxLoss.push(o);"
            "try{dump('CTXLOST ['+o.t+'] canvas='+o.canvas+' statusMessage='+(e.statusMessage||'(tom)')+'\\n');}catch(x){}"
            "});"
            "cv.addEventListener('webglcontextrestored',function(e){"
            "window.__ctxLoss.push({restored:true,t:Date.now()});"
            "try{dump('CTXRESTORED\\n');}catch(x){}"
            "});}catch(e){}}"
            "var __gc=HTMLCanvasElement.prototype.getContext;"
            "HTMLCanvasElement.prototype.getContext=function(){"
            "var args=Array.prototype.slice.call(arguments);"
            "if(args[1]&&typeof args[1]==='object'){"
            "var a={};for(var k in args[1])a[k]=args[1][k];"
            "if(a.alpha===false){a.alpha=true;try{dump('ALPHA-SHIM: alpha tvunget true\\n');}catch(x){}}"
            "if(a.premultipliedAlpha===false){a.premultipliedAlpha=true;try{dump('ALPHA-SHIM: premultipliedAlpha tvunget true\\n');}catch(x){}}"
            "args[1]=a;}"
            "var r=__gc.apply(this,args);__hook(this);"
            "try{dump('GETCONTEXT canvas='+this.width+'x'+this.height+' type='+arguments[0]+' result='+(r?'OK':'NULL')+' attrs='+JSON.stringify(r&&r.getContextAttributes?r.getContextAttributes():{})+'\\n');}catch(x){}"
            "try{__cap(r,this,arguments[0]);}catch(e){}"
            "try{if(r&&r.getExtension){"
            "var __ge=r.getExtension.bind(r);"
            "r.getExtension=function(n){"
            "if(String(n).toUpperCase()==='WEBGL_LOSE_CONTEXT'){"
            "try{dump('LOSECONTEXT-BLOKERET: loseContext er no-op\\n');}catch(x){}"
            "return{loseContext:function(){try{dump('LOSECONTEXT-KALD BLOKERET\\n');}catch(x){}},"
            "restoreContext:function(){},__blokeret:true};}"
            "return __ge(n);};"
            "}}catch(e){}"
            # 19. sep 2026: getContext kaldes MANGE gange paa samme canvas (111
            # kald i én koersel maalt i loggen), og hver gang blev wrapperne
            # lagt oven paa det SAMME kontekst-objekt. Det gjorde JS-taellerne
            # ~8x for store (draws=50652/10 s mod proxyens 620/s). Vagten
            # nedenfor wrapper hver kontekst praecis én gang.
            "try{if(r&&r.drawArrays&&!r.__cywrapdone){r.__cywrapdone=1;"
            "var __da=r.drawArrays.bind(r),__dc=0;"
            "r.drawArrays=function(m,f,c){var __a=performance.now();"
            "__dc++;window.__dc=(window.__dc|0)+1;"
            "if(__dc<=10||__dc%300===0){try{dump('DRAWARRAYS #'+__dc+' mode='+m+' first='+f+' count='+c+' prog='+r.getParameter(r.CURRENT_PROGRAM)+'\\n');}catch(x){}}"
            "var __rv=__da(m,f,c);"
            "window.__drawms=(window.__drawms||0)+(performance.now()-__a);"
            "return __rv;};"
            "var __de=r.drawElements.bind(r);"
            "r.drawElements=function(m,c,t,i){var __a=performance.now();"
            "__dc++;window.__dc=(window.__dc|0)+1;"
            "if(__dc<=10||__dc%300===0){try{dump('DRAWELEMENTS #'+__dc+' mode='+m+' count='+c+' type='+t+' prog='+r.getParameter(r.CURRENT_PROGRAM)+'\\n');}catch(x){}}"
            "var __rv=__de(m,c,t,i);"
            "window.__drawms=(window.__drawms||0)+(performance.now()-__a);"
            "return __rv;};"
            "var __cl=r.clear.bind(r),__cc=0;"
            "r.clear=function(m){__cc++;"
            "if(__cc<=5||__cc%200===0){try{dump('CLEAR #'+__cc+' mask='+m+'\\n');}catch(x){}}"
            "return __cl(m);};"
            "if(r.clearColor){"
            "var __cc2=r.clearColor.bind(r),__cn=0;"
            "r.clearColor=function(a,b,c2,d){__cn++;"
            "if(__cn<=5||__cn%200===0){try{dump('CLEARCOLOR #'+__cn+' ='+a+','+b+','+c2+','+d+'\\n');}catch(x){}}"
            "return __cc2(a,b,c2,d);};}"
            "if(r.texImage2D){"
            "var __ti=r.texImage2D.bind(r),__tn=0;"
            "r.texImage2D=function(){"
            "try{var a=arguments;"
            "var tgt=a[0],lvl=a[1],ifmt=a[2],w=a[3],h=a[4];"
            "if(typeof w===\"number\"&&typeof h===\"number\"&&w>0&&h>0){__tn++;"
            "if(__tn<=20||__tn%50===0){dump('TEXIMAGE2D #'+__tn+' tgt='+tgt+' fmt='+ifmt+' '+w+'x'+h+'\\n');}}"
            "}catch(e){}"
            "return __ti.apply(this,arguments);};}"
            "}}catch(e){}"
            "try{var le=r&&r.getExtension&&r.getExtension('WEBGL_lose_context');"
            "if(le&&!le.__hooked){le.__hooked=1;var o=le.loseContext;"
            "le.loseContext=function(){"
            "try{dump('LOSECONTEXT-KALD canvas='+(this&&this.canvas?this.canvas.width+'x'+this.canvas.height:'?')+' stak='+new Error().stack+'\\n');}catch(x){}"
            "return o.apply(this,arguments);};}}catch(e){}"
            "return r;};"
            "if(typeof OffscreenCanvas!=='undefined'){"
            "var __ogc=OffscreenCanvas.prototype.getContext;"
            "OffscreenCanvas.prototype.getContext=function(){"
            "var r=__ogc.apply(this,arguments);__hook(this);"
            "try{dump('GETCONTEXT offscreen type='+arguments[0]+' result='+(r?'OK':'NULL')+'\\n');}catch(x){}"
            "try{__cap(r,this,arguments[0]);}catch(e){}"
            "try{var le=r&&r.getExtension&&r.getExtension('WEBGL_lose_context');"
            "if(le&&!le.__hooked){le.__hooked=1;var o=le.loseContext;"
            "le.loseContext=function(){"
            "try{dump('LOSECONTEXT-KALD offscreen stak='+new Error().stack+'\\n');}catch(x){}"
            "return o.apply(this,arguments);};}}catch(e){}"
            "return r;};}"
            "try{dump('CAPNAV hw='+(navigator.hardwareConcurrency)+' dm='+"
            "(navigator.deviceMemory||'?')+' touch='+(navigator.maxTouchPoints||0)+'\\n');}catch(x){}"
            "window.addEventListener('error',function(e){"
            "try{dump('PAGE-ERROR: '+e.message+' ved '+e.filename+':'+e.lineno+'\\n');}catch(x){}});"
            "window.addEventListener('unhandledrejection',function(e){"
            "try{dump('PAGE-REJECTION: '+e.reason+'\\n');}catch(x){}});"
            "var __lastpx='';"
            "setInterval(function(){"
            "try{"
            "var cvs=document.querySelectorAll('canvas');"
            "for(var i=0;i<cvs.length;i++){var cv=cvs[i];"
            "if(cv.width<100||cv.height<100)continue;"
            "var r=cv.getBoundingClientRect();"
            "var cs=getComputedStyle(cv);"
            "dump('CANVAS-DOM canvas='+cv.width+'x'+cv.height+' rect='+Math.round(r.width)+'x'+Math.round(r.height)+' display='+cs.display+' vis='+cs.visibility+' opac='+cs.opacity+'\\n');"
            "}"
            "}catch(e){}}"
            ",10000);"
            "setInterval(function(){"
            "try{"
            "var cvs=document.querySelectorAll('canvas');"
            "for(var i=0;i<cvs.length;i++){var cv=cvs[i];"
            "if(cv.width<100||cv.height<100)continue;"
            "var gl=cv.getContext&&(cv.getContext('webgl2')||cv.getContext('webgl'));"
            "if(gl&&gl.readPixels){"
            "var px=new Uint8Array(4);"
            "try{gl.readPixels(Math.floor(cv.width/2),Math.floor(cv.height/2),1,1,gl.RGBA,gl.UNSIGNED_BYTE,px);"
            "var key=px[0]+','+px[1]+','+px[2]+','+px[3];"
            "if(key!==__lastpx){__lastpx=key;"
            "dump('CANVAS-PIXEL canvas='+cv.width+'x'+cv.height+' midte='+key+' lost='+gl.isContextLost()+'\\n');"
            "}}catch(e){dump('CANVAS-PIXEL fejl: '+e+'\\n');}"
            "}}"
            "}catch(e){}}"
            ",10000);"
            + pre_measure +
            "}catch(e){}}"
        )
        pre = pre_min if mode == "preloadmin" else pre_full
        r = call("script.addPreloadScript", {"functionDeclaration": pre})
        print("PRELOAD:", json.dumps(r)[:300])
        if len(sys.argv) > 2 and sys.argv[2] == "goto":
            target = (sys.argv[3] if len(sys.argv) > 3
                      else "https://poki.com/en/g/subway-surfers")
            r = call("browsingContext.navigate",
                     {"context": ctx, "url": target}, timeout=30)
            print("NAVIGERET:", json.dumps(r)[:200])
            # SHOT_AT="60,180": tag Firefox' EGEN gengivelse af siden på de
            # tidspunkter (uafhængigt af hvad skærmen viser).
            shot_at = os.environ.get("SHOT_AT")
            if shot_at:
                shot_dir = os.environ.get("SHOT_DIR", "/root/shots")
                try:
                    os.makedirs(shot_dir, exist_ok=True)
                except Exception as e:
                    print("kunne ikke lave %s: %s" % (shot_dir, e))

                def _shots():
                    t_start = time.time()
                    for at in str(shot_at).split(","):
                        at = at.strip()
                        if not at:
                            continue
                        while time.time() - t_start < float(at):
                            time.sleep(0.5)
                        try:
                            rr = call("browsingContext.captureScreenshot",
                                      {"context": ctx}, timeout=120)
                            data = (rr.get("result") or {}).get("data")
                            if data:
                                path = os.path.join(shot_dir,
                                                    "shot_%s.png" % at)
                                with open(path, "wb") as fh:
                                    fh.write(base64.b64decode(data))
                                print("SHOT gemt: %s" % path)
                            else:
                                print("SHOT fejlede: %s" % json.dumps(rr)[:200])
                        except Exception as e:
                            print("SHOT-fejl: %s" % e)
                threading.Thread(target=_shots, daemon=True).start()
            # "Tag et billede NU"-knap: hvis SHOT_POLL er sat, tages et
            # screenshot hver gang filen /tmp/shot_now dukker op (og fjernes).
            if os.environ.get("SHOT_POLL"):
                shot_dir2 = os.environ.get("SHOT_DIR", "/root/shots")
                try:
                    os.makedirs(shot_dir2, exist_ok=True)
                except Exception:
                    pass

                def _shot_poll():
                    while not stop[0]:
                        time.sleep(2)
                        if not os.path.exists("/tmp/shot_now"):
                            continue
                        try:
                            os.unlink("/tmp/shot_now")
                        except Exception:
                            pass
                        try:
                            rr = call("browsingContext.captureScreenshot",
                                      {"context": ctx}, timeout=120)
                            data = (rr.get("result") or {}).get("data")
                            if data:
                                path = os.path.join(
                                    shot_dir2,
                                    "now_%d.png" % int(time.time()))
                                with open(path, "wb") as fh:
                                    fh.write(base64.b64decode(data))
                                print("SHOT gemt: %s" % path)
                            else:
                                print("SHOT fejlede: %s" % json.dumps(rr)[:200])
                        except Exception as e:
                            print("SHOT-fejl: %s" % e)
                threading.Thread(target=_shot_poll, daemon=True).start()
            # PLAY_AT=<sek>: send et rigtigt tastetryk (+klik) til spillet så
            # "Press to play"-overlejringen forsvinder uden manuel input.
            play_at = os.environ.get("PLAY_AT")
            if play_at:
                # PLAY_AT kan være kommasepareret ("110,210"): send input ved
                # hvert tidspunkt (mere robust — spillet er måske ikke loadet
                # ved første forsøg).
                prev = 0
                for at in str(play_at).split(","):
                    at = at.strip()
                    if not at:
                        continue
                    time.sleep(max(0, int(at) - prev))
                    prev = int(at)
                    tree2 = call("browsingContext.getTree", {})
                    ctxs2 = []

                    def walk3(items):
                        for it in items or []:
                            ctxs2.append(it)
                            walk3(it.get("children"))
                    walk3(tree2.get("result", {}).get("contexts", []))
                    tgt = None
                    for c in ctxs2:
                        u = c.get("url", "")
                        if "gdn.poki.com" in u or "index.html" in u:
                            tgt = c.get("context")
                            break
                    if tgt is None:
                        tgt = ctx
                    print("PLAY-INPUT t=%ss mod kontekst %s" % (at, tgt))
                    for nm, acts in (
                        ("tast-mellemrum", [
                            {"type": "key", "id": "kb", "actions": [
                                {"type": "keyDown", "value": " "},
                                {"type": "keyUp", "value": " "}]}]),
                        ("tast-arrowup", [
                            {"type": "key", "id": "kb", "actions": [
                                {"type": "keyDown", "value": "\ue013"},
                                {"type": "keyUp", "value": "\ue013"}]}]),
                        ("klik-midt", [
                            {"type": "pointer", "id": "m1",
                             "parameters": {"pointerType": "mouse"},
                             "actions": [
                                 {"type": "pointerMove", "x": 400, "y": 300},
                                 {"type": "pointerDown", "button": 0},
                                 {"type": "pointerUp", "button": 0}]}]),
                    ):
                        rr = call("input.performActions",
                                  {"context": tgt, "actions": acts}, timeout=25)
                        print("PLAY-INPUT %s -> %s" % (nm,
                                                       json.dumps(rr)[:160]))
                        time.sleep(3)
            if len(sys.argv) > 4:
                deadline = time.time() + int(sys.argv[4])
                while time.time() < deadline and not stop[0]:
                    time.sleep(2)
                print("VENT-FÆRDIG (stop=%s)" % stop[0])
        if len(sys.argv) > 2 and sys.argv[2] == "reload":
            r = call("browsingContext.reload", {"context": ctx})
            print("GENINDLASTET:", json.dumps(r)[:200])
        if len(sys.argv) > 3 and sys.argv[3] == "wait":
            time.sleep(int(sys.argv[4]) if len(sys.argv) > 4 else 150)
            r = call("script.evaluate", {
                "expression": "JSON.stringify(window.__ctxLoss||'ingen-data')",
                "target": {"context": ctx},
                "awaitPromise": False, "returnByValue": True,
            })
            print("CTXLOSS-DATA:", json.dumps(r)[:800])
            if len(sys.argv) > 5 and sys.argv[5] == "dump" and game_ctx:
                # find spil-konteksten igen (iframe'en kan være kommet efter reload)
                if game_ctx:
                    tree2 = call("browsingContext.getTree", {})
                    ctxs2 = []
                    def walk2(items):
                        for it in items or []:
                            ctxs2.append(it)
                            walk2(it.get("children"))
                    walk2(tree2.get("result", {}).get("contexts", []))
                    for c in ctxs2:
                        if "gdn.poki.com" in c.get("url", ""):
                            game_ctx = c.get("context")
                            print("spil-kontekst-genfundet:", game_ctx)
                            break
                expr = (
                    "var cvs=document.querySelectorAll('canvas');"
                    "var out=[];"
                    "for(var i=0;i<cvs.length;i++){var cv=cvs[i];"
                    "if(cv.width<100)continue;"
                    "try{out.push({w:cv.width,h:cv.height,data:cv.toDataURL('image/png')});}"
                    "catch(e){out.push({fejl:String(e)});}}"
                    "JSON.stringify(out)"
                )
                r2 = call("script.evaluate", {
                    "expression": expr, "target": {"context": game_ctx},
                    "awaitPromise": False, "returnByValue": True,
                })
                res = r2.get("result", {}).get("result", {}).get("value")
                if res:
                    for c in json.loads(res):
                        if "data" in c:
                            b64 = c["data"].split(",", 1)[1]
                            open("/tmp/game_canvas_%dx%d.png" % (c["w"], c["h"]), "wb").write(
                                base64.b64decode(b64))
                            print("GEMT canvas %dx%d" % (c["w"], c["h"]))
                else:
                    print("DUMP-FEJL:", json.dumps(r2)[:300])
    elif mode == "reload":
        r = call("browsingContext.reload", {"context": ctx})
        print("GENINDLASTET:", json.dumps(r)[:200])
    elif mode == "shot":
        # Browserens EGEN gengivelse af siden (uafhængig af skærmen): bruges til
        # at afgøre om spil-indholdet mangler i sidens rendering eller kun på
        # vej ud til skærmen.
        out = sys.argv[2] if len(sys.argv) > 2 else "/tmp/shot.png"
        r = call("browsingContext.captureScreenshot", {"context": ctx},
                 timeout=90)
        data = (r.get("result") or {}).get("data")
        if data:
            with open(out, "wb") as fh:
                fh.write(base64.b64decode(data))
            print("SCREENSHOT gemt: %s (%d base64-tegn)" % (out, len(data)))
        else:
            print("SCREENSHOT fejlede:", json.dumps(r)[:300])
    elif mode == "read":
        r = call("script.evaluate", {
            "expression": "JSON.stringify(window.__ctxLoss||'ingen-data')",
            "target": {"context": ctx},
            "awaitPromise": False, "returnByValue": True,
        })
        print("CTXLOSS-DATA:", json.dumps(r)[:800])
    elif mode == "domcheck" and game_ctx:
        expr = (
            "var out=[];"
            "var cvs=document.querySelectorAll('canvas');"
            "for(var i=0;i<cvs.length;i++){var cv=cvs[i];"
            "var r=cv.getBoundingClientRect();"
            "var cs=getComputedStyle(cv);"
            "var attrs=null;"
            "var gl=cv.getContext&&(cv.getContext('webgl2')||cv.getContext('webgl'));"
            "if(gl&&gl.getContextAttributes)attrs=gl.getContextAttributes();"
            "var glver=gl&&gl.getParameter&&gl.getParameter(gl.VERSION);"
            "var top=null;"
            "try{var ex=Math.floor(r.left+r.width/2),ey=Math.floor(r.top+r.height/2);"
            "var el=document.elementFromPoint(ex,ey);"
            "var chain=[];var p=el;var n=0;"
            "while(p&&n<6){var st=getComputedStyle(p);"
            "chain.push({tag:p.tagName,id:p.id||'',cls:(typeof p.className==='string'?p.className:'').slice(0,40),"
            "d:st.display,v:st.visibility,o:st.opacity,z:st.zIndex,pos:st.position});"
            "p=p.parentElement;n++;}"
            "top={atX:ex,atY:ey,el:el?el.tagName+'#'+(el.id||'')+'.'+(typeof el.className==='string'?el.className:'').slice(0,40):null,chain:chain};"
            "}catch(e){top={fejl:String(e)};}"
            "var vp={iw:window.innerWidth,ih:window.innerHeight,"
            "sx:(window.scrollX||0),sy:(window.scrollY||0),"
            "bw:document.body?document.body.scrollWidth:0,"
            "bh:document.body?document.body.scrollHeight:0};"
            "out.push({w:cv.width,h:cv.height,rw:Math.round(r.width),rh:Math.round(r.height),"
            "d:cs.display,v:cs.visibility,o:cs.opacity,z:cs.zIndex,attrs:attrs,glver:glver,"
            "top:top,vp:vp});}"
            "JSON.stringify({url:location.href,canvasser:out,"
            "iframeRect:(function(){var f=window.frameElement;"
            "if(!f)return null;var r=f.getBoundingClientRect();"
            "return {w:Math.round(r.width),h:Math.round(r.height),d:getComputedStyle(f).display};})()})"
        )
        r = call("script.evaluate", {
            "expression": expr, "target": {"context": game_ctx},
            "awaitPromise": False, "returnByValue": True,
        })
        print("DOMCHECK:", json.dumps(r)[:4000])
    elif mode == "canvasdump" and game_ctx:
        expr = (
            "var cvs=document.querySelectorAll('canvas');"
            "var out=[];"
            "for(var i=0;i<cvs.length;i++){var cv=cvs[i];"
            "if(cv.width<100)continue;"
            "try{var dl=cv.toDataURL('image/png');"
            "out.push({w:cv.width,h:cv.height,data:dl});"
            "}catch(e){out.push({w:cv.width,h:cv.height,fejl:String(e)});}}"
            "JSON.stringify(out)"
        )
        r = call("script.evaluate", {
            "expression": expr, "target": {"context": game_ctx},
            "awaitPromise": False, "returnByValue": True,
        })
        res = r.get("result", {}).get("result", {}).get("value")
        if res:
            arr = json.loads(res)
            for c in arr:
                if "data" in c:
                    b64 = c["data"].split(",", 1)[1]
                    open("/tmp/game_canvas_%dx%d.png" % (c["w"], c["h"]), "wb").write(
                        __import__("base64").b64decode(b64))
                    print("GEMT canvas %dx%d -> /tmp/game_canvas_%dx%d.png" % (c["w"], c["h"], c["w"], c["h"]))
                else:
                    print("CANVAS-FEJL:", c)
        else:
            print("INTET SVAR:", json.dumps(r)[:400])
    elif mode == "glstate" and game_ctx:
        expr = (
            "var cvs=document.querySelectorAll('canvas');"
            "var out=[];"
            "for(var i=0;i<cvs.length;i++){var cv=cvs[i];"
            "if(cv.width<100)continue;"
            "var gl=cv.getContext&&(cv.getContext('webgl2')||cv.getContext('webgl'));"
            "if(!gl)continue;"
            "var px=[];"
            "var pts=[];"
            "for(var gy=0;gy<15;gy++)for(var gx=0;gx<25;gx++)"
            "pts.push([Math.floor((gx+0.5)*cv.width/25),Math.floor((gy+0.5)*cv.height/15)]);"
            "for(var j=0;j<pts.length;j++){var p=new Uint8Array(4);"
            "try{gl.readPixels(pts[j][0],pts[j][1],1,1,gl.RGBA,gl.UNSIGNED_BYTE,p);"
            "if(p[0]!==0||p[1]!==255||p[2]!==255)px.push([pts[j][0],pts[j][1],p[0],p[1],p[2],p[3]]);}catch(e){}}"
            "var dl='';try{dl=cv.toDataURL('image/png').length;}catch(e){}"
            "out.push({w:cv.width,h:cv.height,"
            "fbo:gl.getParameter(gl.FRAMEBUFFER_BINDING),"
            "readFbo:gl.getParameter(gl.READ_FRAMEBUFFER_BINDING),"
            "viewport:gl.getParameter(gl.VIEWPORT),"
            "drawBuffers:gl.getParameter(gl.DRAW_BUFFER0),"
            "err:gl.getError(),"
            "afvigendePunkter:px.length,dataUrlLen:dl,afvigelser:px.slice(0,10)});}"
            "JSON.stringify(out)"
        )
        r = call("script.evaluate", {
            "expression": expr, "target": {"context": game_ctx},
            "awaitPromise": False, "returnByValue": True,
        })
        res = r.get("result", {}).get("result", {}).get("value")
        print("GLSTATE:", res if res else json.dumps(r)[:500])
    elif mode == "scene" and game_ctx:
        expr = (
            "var cvs=document.querySelectorAll('canvas');"
            "var out=[];"
            "for(var i=0;i<cvs.length;i++){var cv=cvs[i];"
            "if(cv.width<100)continue;"
            "var gl=cv.getContext&&(cv.getContext('webgl2')||cv.getContext('webgl'));"
            "if(!gl)continue;"
            "var px=[];var pts=[];"
            "for(var gy=0;gy<15;gy++)for(var gx=0;gx<25;gx++)"
            "pts.push([Math.floor((gx+0.5)*cv.width/25),Math.floor((gy+0.5)*cv.height/15)]);"
            "var uni={};var seen={};"
            "for(var j=0;j<pts.length;j++){var p=new Uint8Array(4);"
            "try{gl.readPixels(pts[j][0],pts[j][1],1,1,gl.RGBA,gl.UNSIGNED_BYTE,p);"
            "var k=p[0]+','+p[1]+','+p[2]+','+p[3];uni[k]=(uni[k]||0)+1;"
            "if(p[0]!==0||p[1]!==255||p[2]!==255)px.push([pts[j][0],pts[j][1],p[0],p[1],p[2],p[3]]);}catch(e){}}"
            "try{var ca=gl.getContextAttributes?gl.getContextAttributes():null;"
            "out.push({w:cv.width,h:cv.height,attrs:ca,"
            "fbo:gl.getParameter(gl.FRAMEBUFFER_BINDING),"
            "readFbo:gl.getParameter(gl.READ_FRAMEBUFFER_BINDING),"
            "viewport:gl.getParameter(gl.VIEWPORT),"
            "scissor:gl.getParameter(gl.SCISSOR_BOX),"
            "err:gl.getError(),"
            "farver:uni,afvigendePunkter:px.length,afvigelser:px.slice(0,8),"
            "data:cv.toDataURL('image/png')});}catch(e){out.push({fejl:String(e)});}}"
            "JSON.stringify(out)"
        )
        r = call("script.evaluate", {
            "expression": expr, "target": {"context": game_ctx},
            "awaitPromise": False, "returnByValue": True,
        })
        res = r.get("result", {}).get("result", {}).get("value")
        if res:
            arr = json.loads(res)
            for c in arr:
                if "data" in c:
                    b64 = c["data"].split(",", 1)[1]
                    open("/tmp/game_canvas_%dx%d.png" % (c["w"], c["h"]), "wb").write(
                        base64.b64decode(b64))
                    print("GEMT canvas %dx%d farver=%s afvig=%d attrs=%s vp=%s err=%s" % (
                        c["w"], c["h"], c.get("farver"), c.get("afvigendePunkter"),
                        c.get("attrs"), c.get("viewport"), c.get("err")))
                    try:
                        mid += 1
                        ws.send({"id": mid, "method": "session.end", "params": {}})
                    except Exception:
                        pass
                else:
                    print("CANVAS-FEJL:", c)
        else:
            print("INTET SVAR:", json.dumps(r)[:500])
    try:
        mid += 1
        ws.send({"id": mid, "method": "session.end", "params": {}})
    except Exception:
        pass
    ws.s.close()


if __name__ == "__main__":
    main()
