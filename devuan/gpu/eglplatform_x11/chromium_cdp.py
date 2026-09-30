#!/usr/bin/env python3
"""Lille CDP-klient til Chromium-sporet på GeekBox.

Kræver at Chromium kører med --remote-debugging-port=9223, og at porten
kan nås (fx via ssh -L 9223:127.0.0.1:9223). Bruger websocket-client, som
findes på laptop'en.

Kommandoer:
  chromium_cdp.py list
  chromium_cdp.py eval '<javascript>'
  chromium_cdp.py goto <url> [sekunder]
  chromium_cdp.py game [url] [sekunder]

`game` blokerer de kendte reklame-/trackerdomæner gennem Network.setBlockedURLs,
åbner spillet, klikker på game-iframen og rapporterer canvas- og rAF-status.
Blockeringen er kun aktiv i denne CDP-session; den skriver intet på boksen.
"""
import json
import sys
import time
import urllib.request

import websocket

PORT = 9223
AD_PATTERNS = [
    "*doubleclick.net*",
    "*googlesyndication.com*",
    "*googletagservices.com*",
    "*amazon-adsystem.com*",
    "*imasdk.googleapis.com*",
    "*inmobi.com*",
    "*yieldmo.com*",
    "*3lift.com*",
    "*pubmatic.com*",
    "*rubiconproject.com*",
    "*smartadserver.com*",
    "*a-mo.net*",
    "*media.net*",
    "*onetag-sys.com*",
    "*indexww.com*",
    "*adservice.google*",
]


def page_target():
    pages = json.load(urllib.request.urlopen(
        f"http://127.0.0.1:{PORT}/json/list", timeout=5))
    return next(p for p in pages if p.get("type") == "page")


class CDP:
    def __init__(self):
        self.ws = websocket.create_connection(
            page_target()["webSocketDebuggerUrl"], timeout=30,
            suppress_origin=True)
        self.seq = 0

    def call(self, method, params=None, timeout=30):
        self.seq += 1
        req = {"id": self.seq, "method": method, "params": params or {}}
        self.ws.send(json.dumps(req))
        end = time.time() + timeout
        while time.time() < end:
            msg = json.loads(self.ws.recv())
            if msg.get("id") == self.seq:
                return msg
        raise TimeoutError(method)

    def value(self, expression, context_id=None):
        params = {"expression": expression, "returnByValue": True}
        if context_id is not None:
            params["contextId"] = context_id
        r = self.call("Runtime.evaluate", params)
        result = r.get("result", {}).get("result", {})
        if "value" in result:
            return result["value"]
        return result

    def close(self):
        self.ws.close()


def frame_tree(cdp):
    cdp.call("Page.enable")
    return cdp.call("Page.getFrameTree")["result"]["frameTree"]


def flatten_frames(tree):
    out = []

    def walk(node):
        out.append(node["frame"])
        for child in node.get("childFrames", []):
            walk(child)

    walk(tree)
    return out


def find_game_frame(cdp):
    frames = flatten_frames(frame_tree(cdp))
    return next((f for f in frames if "gdn.poki.com" in f.get("url", "")), None)


def game_summary(cdp, frame_id):
    ctx = cdp.call("Page.createIsolatedWorld",
                   {"frameId": frame_id, "worldName": "codex"})
    context_id = ctx["result"]["executionContextId"]
    expr = r"""
    (() => {
      const cs = Array.from(document.querySelectorAll('canvas')).map((c, i) => {
        const info = {i, w: c.width, h: c.height, cw: c.clientWidth,
                      ch: c.clientHeight, style: c.getAttribute('style')};
        try {
          const gl = c.getContext('webgl2') || c.getContext('webgl');
          if (gl) {
            const dbg = gl.getExtension('WEBGL_debug_renderer_info');
            info.gl = {
              version: gl.getParameter(gl.VERSION),
              renderer: gl.getParameter(gl.RENDERER),
              unmasked: dbg ? gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL) : null
            };
          }
        } catch (e) { info.err = String(e); }
        return info;
      });
      return {title: document.title, url: location.href,
              ready: document.readyState, canvas: cs,
              text: document.body ? document.body.innerText.slice(0, 400) : ''};
    })()
    """
    return cdp.value(expr, context_id), context_id


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    cdp = CDP()
    try:
        if cmd == "list":
            for p in json.load(urllib.request.urlopen(
                    f"http://127.0.0.1:{PORT}/json/list", timeout=5)):
                print(p.get("type"), p.get("title"), p.get("url", "")[:140])
        elif cmd == "eval":
            print(json.dumps(cdp.value(sys.argv[2]), indent=2)[:8000])
        elif cmd == "goto":
            url = sys.argv[2]
            wait = float(sys.argv[3]) if len(sys.argv) > 3 else 15
            cdp.call("Page.enable")
            cdp.call("Page.navigate", {"url": url})
            time.sleep(wait)
            print(json.dumps(cdp.value(
                "({title:document.title,url:location.href})"), indent=2))
        elif cmd == "game":
            url = sys.argv[2] if len(sys.argv) > 2 else \
                "https://poki.com/en/g/subway-surfers"
            wait = float(sys.argv[3]) if len(sys.argv) > 3 else 20
            cdp.call("Page.enable")
            cdp.call("Network.enable")
            cdp.call("Network.setBlockedURLs", {"urls": AD_PATTERNS})
            # SwiftShader er en "major performance caveat". Unity/WebGL-spil
            # beder typisk om failIfMajorPerformanceCaveat:false for at kunne
            # bruge software-GL; uden dette afviser Chromium konteksten.
            cdp.call("Page.addScriptToEvaluateOnNewDocument", {
                "source": """
                (() => {
                  const orig = HTMLCanvasElement.prototype.getContext;
                  HTMLCanvasElement.prototype.getContext = function(type, attrs) {
                    if (attrs && attrs.failIfMajorPerformanceCaveat) {
                      attrs = Object.assign({}, attrs,
                                             {failIfMajorPerformanceCaveat: false});
                    }
                    return orig.call(this, type, attrs);
                  };
                })();
                """
            })
            cdp.call("Page.navigate", {"url": url})
            time.sleep(wait)
            frame = find_game_frame(cdp)
            if not frame:
                print(json.dumps({
                    "error": "game frame not found",
                    "page": cdp.value("({title:document.title,url:location.href})")
                }, indent=2))
                return 1
            summary, context_id = game_summary(cdp, frame["id"])
            cdp.value("window.__codex_raf=0;(function loop(){"
                      "window.__codex_raf++;requestAnimationFrame(loop)})()",
                      context_id)
            rect = cdp.value(
                "(() => {const f=[...document.querySelectorAll('iframe')]"
                ".find(x=>x.src.includes('gdn.poki.com'));if(!f)return null;"
                "const r=f.getBoundingClientRect();"
                "return {x:r.x+r.width/2,y:r.y+r.height/2,w:r.width,h:r.height};})()")
            if not rect:
                # Spil-iframen ligger i games.poki.com, ikke direkte i
                # topdokumentet. Midten af 1920x1080-viewporten rammer
                # alligevel canvas'en (1031x580 centreret).
                rect = {"x": 960, "y": 540, "w": 1031, "h": 580,
                        "fallback": True}
            if rect:
                for typ in ("mousePressed", "mouseReleased"):
                    cdp.call("Input.dispatchMouseEvent", {
                        "type": typ, "x": rect["x"], "y": rect["y"],
                        "button": "left", "clickCount": 1})
                time.sleep(10)
            raf = cdp.value("window.__codex_raf || 0", context_id)
            summary["raf_10s"] = raf
            summary["raf_per_s"] = raf / 10.0
            summary["clicked"] = bool(rect)
            print(json.dumps(summary, indent=2)[:8000])
        else:
            print(__doc__)
            return 2
    finally:
        cdp.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
