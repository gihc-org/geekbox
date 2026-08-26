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
    if not ctx:
        print("INGEN SIDE-KONTEKST; har:", [(c.get("context"), c.get("url")) for c in contexts])
        sys.exit(1)

    if mode == "inject":
        expr = (
            "window.__ctxLoss=[];"
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
            "var r=__gc.apply(this,arguments);__hook(this);"
            "try{dump('GETCONTEXT canvas='+this.width+'x'+this.height+' type='+arguments[0]+' result='+(r?'OK':'NULL')+'\\n');}catch(x){}"
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
            "try{var le=r&&r.getExtension&&r.getExtension('WEBGL_lose_context');"
            "if(le&&!le.__hooked){le.__hooked=1;var o=le.loseContext;"
            "le.loseContext=function(){"
            "try{dump('LOSECONTEXT-KALD offscreen stak='+new Error().stack+'\\n');}catch(x){}"
            "return o.apply(this,arguments);};}}catch(e){}"
            "return r;};}"
            "window.addEventListener('error',function(e){"
            "try{dump('PAGE-ERROR: '+e.message+' ved '+e.filename+':'+e.lineno+'\\n');}catch(x){}});"
            "window.addEventListener('unhandledrejection',function(e){"
            "try{dump('PAGE-REJECTION: '+e.reason+'\\n');}catch(x){}});"
            "setInterval(function(){"
            "try{"
            "var cvs=document.querySelectorAll('canvas');"
            "for(var i=0;i<cvs.length;i++){var cv=cvs[i];"
            "var gl=cv.getContext&&(cv.getContext('webgl2')||cv.getContext('webgl'));"
            "if(gl&&gl.readPixels){"
            "var px=new Uint8Array(4);"
            "try{gl.readPixels(Math.floor(cv.width/2),Math.floor(cv.height/2),1,1,gl.RGBA,gl.UNSIGNED_BYTE,px);"
            "dump('CANVAS-PIXEL canvas='+cv.width+'x'+cv.height+' midte='+px[0]+','+px[1]+','+px[2]+','+px[3]+' lost='+gl.isContextLost()+'\\n');"
            "}catch(e){dump('CANVAS-PIXEL fejl: '+e+'\\n');}"
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
    ws.s.close()


if __name__ == "__main__":
    main()
