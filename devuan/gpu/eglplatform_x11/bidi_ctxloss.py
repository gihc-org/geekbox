#!/usr/bin/env python3
"""Minimal WebDriver BiDi-klient: injicer webglcontextlost-lytter i spilsiden og
læs statusMessage (Firefox 140 har ikke længere CDP — kun BiDi)."""
import base64
import json
import os
import socket
import struct
import sys
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
    mid = 0

    def call(method, params=None, timeout=20):
        nonlocal mid
        mid += 1
        ws.send({"id": mid, "method": method, "params": params or {}})
        while True:
            m = ws.recv(timeout=timeout)
            if m and m.get("id") == mid:
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
        print("INGEN SIDE-KONTEKST; har:", [(c.get("context"), c.get("url")) for c in contexts])
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
    elif mode == "preload":
        pre = (
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
            "try{dump('GETCONTEXT canvas='+this.width+'x'+this.height+' type='+arguments[0]+' result='+(r?'OK':'NULL')+'\\n');}catch(x){}"
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
            "try{if(r&&r.drawArrays){"
            "var __da=r.drawArrays.bind(r),__dc=0;"
            "r.drawArrays=function(m,f,c){__dc++;"
            "if(__dc<=10||__dc%300===0){try{dump('DRAWARRAYS #'+__dc+' mode='+m+' first='+f+' count='+c+' prog='+r.getParameter(r.CURRENT_PROGRAM)+'\\n');}catch(x){}}"
            "return __da(m,f,c);};"
            "var __de=r.drawElements.bind(r);"
            "r.drawElements=function(m,c,t,i){__dc++;"
            "if(__dc<=10||__dc%300===0){try{dump('DRAWELEMENTS #'+__dc+' mode='+m+' count='+c+' type='+t+' prog='+r.getParameter(r.CURRENT_PROGRAM)+'\\n');}catch(x){}}"
            "return __de(m,c,t,i);};"
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
            "}catch(e){}}"
        )
        r = call("script.addPreloadScript", {"functionDeclaration": pre})
        print("PRELOAD:", json.dumps(r)[:300])
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
                    import base64
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
            "out.push({w:cv.width,h:cv.height,rw:Math.round(r.width),rh:Math.round(r.height),"
            "d:cs.display,v:cs.visibility,o:cs.opacity,attrs:attrs,glver:glver});}"
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
            import base64
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
