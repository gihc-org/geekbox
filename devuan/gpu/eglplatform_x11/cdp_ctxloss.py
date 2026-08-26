#!/usr/bin/env python3
"""Minimal CDP-klient: injicer webglcontextlost-lytter i spilsiden og dump
statusMessage til browserkonsolet (dump() -> Firefox-loggen)."""
import base64
import json
import os
import socket
import struct
import sys
import time
import urllib.request


def http_json(url):
    with urllib.request.urlopen(url, timeout=5) as r:
        return json.loads(r.read().decode())


class Ws:
    def __init__(self, url):
        u = url.replace("ws://", "http://")
        key = base64.b64encode(os.urandom(16)).decode()
        req = (
            "GET /devtools/page HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n" % key
        )
        self.s = socket.create_connection(("127.0.0.1", 9222), timeout=5)
        self.s.sendall(req.encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += self.s.recv(4096)
        if b"101" not in buf.split(b"\r\n", 1)[0]:
            raise RuntimeError("ws handshake fejlede: %r" % buf[:200])

    def send(self, obj):
        payload = json.dumps(obj).encode()
        mask = os.urandom(4)
        hdr = bytearray([0x81])
        n = len(payload)
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
            n = struct.unpack(">H", self._recv_exact(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", self._recv_exact(8))[0]
        mask = self._recv_exact(4) if hdr[1] & 0x80 else None
        data = self._recv_exact(n)
        if mask:
            data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        if opcode == 1:
            return json.loads(data.decode())
        return None

    def _recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.s.recv(n - len(buf))
            if not chunk:
                break
            buf += chunk
        return buf


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "inject"
    targets = http_json("http://127.0.0.1:9222/json/list")
    page = None
    for t in targets:
        if t.get("type") == "page" and ("poki" in t.get("url", "") or "subway" in t.get("url", "")):
            page = t
            break
    if not page:
        # falder tilbage til første page-target
        page = next((t for t in targets if t.get("type") == "page"), None)
    if not page:
        print("INGEN PAGE-TARGET")
        sys.exit(1)
    print("target:", page.get("url"))
    ws = Ws(page["webSocketDebuggerUrl"])
    mid = 1
    pending = {}

    def call(method, params=None):
        nonlocal mid
        mid += 1
        msg = {"id": mid, "method": method, "params": params or {}}
        ws.send(msg)
        return mid

    def wait_response(call_id, timeout=10):
        end = time.time() + timeout
        while time.time() < end:
            m = ws.recv(timeout=max(0.1, end - time.time()))
            if m and m.get("id") == call_id:
                return m
        return None

    call("Runtime.enable")
    time.sleep(0.5)
    if mode == "inject":
        expr = (
            "window.__ctxLoss=[];"
            "window.addEventListener('webglcontextlost',function(e){"
            "window.__ctxLoss.push({msg:e.statusMessage||'',t:Date.now()});"
            "try{dump('CTXLOST statusMessage='+(e.statusMessage||'(tom)')+'\\n');}catch(x){}"
            "});"
            "window.addEventListener('webglcontextrestored',function(e){"
            "window.__ctxLoss.push({restored:true,t:Date.now()});"
            "try{dump('CTXRESTORED\\n');}catch(x){}"
            "});"
            "'lytter-installeret'"
        )
        cid = call("Runtime.evaluate", {"expression": expr, "returnByValue": True})
        r = wait_response(cid)
        print("INJICERET:", r.get("result", {}).get("result", {}).get("value") if r else "intet-svar")
    elif mode == "read":
        cid = call("Runtime.evaluate", {
            "expression": "JSON.stringify(window.__ctxLoss||'ingen-data')",
            "returnByValue": True,
        })
        r = wait_response(cid, timeout=15)
        if r:
            print("CTXLOSS-DATA:", r.get("result", {}).get("result", {}).get("value"))
        else:
            print("INTET-SVAR")
    ws.s.close()


if __name__ == "__main__":
    main()
