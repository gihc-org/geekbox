# Cyan-scene-handover — næste session (27. aug 2026, tidlig morgen)

> Agent/model: [codex:deepseek-v4-flash]. Læs dette + 
> `GRALLOC-LOCK-SPOR-SESSION-NOTAT-2026-08-26.md` (fuldt beslutnings- og
> målingslog) og fortsæt derfra.

## Status i ét blik

- **DDK 1.5@3830101 kører fuldt** (GL_VERSION "OpenGL ES 3.1 build
  1.5@3830101", draw_buffers OK, WebGL OK).
- **Subway Surfers loader STABILT og er spilbart** (lyd, HUD, point,
  pause-menu virker) med den rette konfiguration (se Bring-up).
- **Tilbageværende blokade: 3D-scenen renderer CYAN** — drengen/banen tegnes
  ikke; kun en ensartet cyan (eller sort) flade. Spillets logik kører.
- Boks 1: IP skifter pr. boot (`bash devuan/find_box.sh`); senest 192.168.0.109.

## Aftaler og beslutninger

- [udført] gralloc-lock-EINVAL: rodårsag = `/dev/sw_sync` 0600 root:root →
  kristian fik EACCES i lock'ens sync-fence-sti. `chmod 666` (myinit med
  retry-løkke, da enheden oprettes efter devtmpfs-mount).
- [udført] x11ws lock-usage 0x80→0x3 (1.5-gralloc er Android 5.1: SW-bit-maske
  0x33; 0x80 gav vaddr=NULL).
- [udført] `layers.acceleration.disabled=true` → chrome + side vises normalt.
- [udført] poki-load-crash: GLESv2-proxyen brød WebGL1/ES1 → behold ORIGINAL
  libGLESv2; EGL-proxyen har selv shader-hooks via eglGetProcAddress.
- [udført] Spillet vises med shim (alpha:true + premultipliedAlpha:true +
  loseContext-no-op) — indlæst som Firefox-udvidelse via about:debugging
  (midlertidig; forsvinder ved genstart).
- [åbent] Cyan-scene (næste spor).
- [åbent] Næste kernel-byg: CONFIG_ANDROID_PARANOID_NETWORK fra + bcmdhd.
- [åbent] NTP-sporet (chrony).

## Bring-up (boks efter strømcyklus)

```bash
bash devuan/find_box.sh
# ssh med -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null (ny host-key pr. reflash)
LAPEPOCH=$(date +%s); ssh ... root@<ip> "date -s @$LAPEPOCH"   # ingen RTC
ssh ... 'ls -la /dev/sw_sync'          # skal være 0666 (myinit-retry)
ssh ... 'insmod /root/pvrsrvkm_leddaz.ko; sh /root/gpu_up.sh' # pvrsrvctl-exit=0
# bindapi fra LAPTOPPEN (bind-mount forsvinder ved genstart):
sed "s/BOX=root@192.168.0.188/BOX=root@<ip>/" \
  devuan/gpu/eglplatform_x11/patch_android_bindapi.sh > /tmp/pb.sh && bash /tmp/pb.sh
# udvidelsen (hvis spillet skal VISE): about:debugging → Load Temporary Add-on
#   → /tmp/poki-fix-extension/manifest.json  (filen skal scp'es først)
```

## Den fulde virkende konfiguration (målt 27. aug ~00:2x)

- `/opt/hybris/libEGL.so.1.0.0` = EGL-proxyen (fix_egl_table + shader-hooks +
  makecurrent/swap-hooks). md5 `91651801…`.
- `/opt/hybris/libGLESv2.so.2.0.0` = ORIGINAL (md5 `ca71fb2c…` =
  `/root/hybris_backup/libGLESv2.so.2.orig`). **IKKE glesv2-proxyen** (den
  brød WebGL1/ES1 → poki crashede).
- `eglplatform_x11.so` = boksens version (SELVTEST + timestamps + usage 0x3 +
  retire-logik).
- prefs.js: `layers.acceleration.disabled=true`, `gfx.webrender.enabled=false`
  + `force-disabled=true` (tilbageført), `webgl.force-enabled=true`,
  `layers.gpu-process.enabled=true`, `xpinstall.signatures.required=false`.
  `gfx.offscreencanvas.enabled` skal IKKE være sat (default).
- Udvidelsen `poki-webgl-fix@geekbox` (shim) — indlæses midlertidigt.

## Cyan-scene: hvad vi ved (målt)

- Spillets canvas er 836x470 (300x150-konteksterne er SDK-prober).
- Canvas'et læser ENSARTET cyan (0,255,255,0) eller sort — aldrig variation.
- Clear-farven er PINK (0.847,0.584,0.843,0) — ikke cyan.
- Spillet tegner kun små elementer pr. frame: CLEAR + DRAWELEMENTS(mode=4,
  count=18, type=5123). Ingen store scene-draws.
- Spillet uploader RIGTIGE teksturer (billeder, format RGBA/UNSIGNED_BYTE) +
  1x1-cubemap-pladsholdere.
- 0 GL-fejl, 0 shader-compile-fejl, 0 JS-fejl (PAGE-ERROR/REJECTION tomme).
- MRT virker (FBO komplet, begge attachments renderer).
- ioctls på /dev/pvrsrvkm flyder (~54/s) — GPU'en modtager kommandoer.
- toDataURL = sort (preserveDrawingBuffer=false — bufferet ryddes).
- Simpel + nested + alpha:false-WebGL vises korrekt (brugerbekræftet) — så
  readback/display er ikke generelt ødelagt.

**Hypotese:** spillet renderer kun "himlen"/baggrunden (cyan) mens objekterne
(drengen, banen) er usynlige — deres shaders kompilerer men tegner intet på
1.5-stakken (forkerte uniformer/matricer eller alpha-0-output). Alternativt
deaktiverer spillet bevidst scenen via en kapabilitets-check.

## Næste skridt (cyan-scene)

1. Find 18-verts-draw'ets program + shaders (log CURRENT_PROGRAM + link til
   proxy-shaderloggen; sammenlign scene-shaderens uniformer/matricer).
2. Tjek om spillet bevidst deaktiverer scenen (kapabilitets-check: fx
   failIfMajorPerformanceCaveat, hardwareConcurrency, WEBGL-debug-renderer).
3. Sammenlign spillets vertex-shader-matematik (matrix-opbygning) mod en
   kendt-god gengivelse.
4. Overvej Spor B (4.4 + DDK 1.8) hvis 1.5's scene-rendering er uovervindelig.

## Fælder (målt — overtræd ikke)

- `pkill -9 -x firefox-esr` (aldrig `-f firefox`); ingen MOZ_GL_SPEW.
- glesv2-proxyen må IKKE installeres (WebGL1/ES1-crash).
- alpha:false-canvasser vises ikke med software-layers (shim nødvendig).
- --install-extension/manuel extensions.json er upålidelig → about:debugging.
- /tmp ryddes ved reboot (test-kit i /root; fbdump/bidi_ctxloss skal scp'es).
- Boksens host-key ændres ved reflash → ssh/scp med host-key-bypass.
- Uret er forkert efter strømcyklus (ingen RTC) → sæt fra laptoppen.
- Boksen er skrøbelig under WebGL-belastning (tilfældige crashes; kernel
  "Bad page state" set tidligere) — tag screenshots/målinger hurtigt.

## God start i en ny session

"Læs `devuan/gpu/CYAN-SCENE-HANDOVER-2026-08-27.md` og
`devuan/gpu/GRALLOC-LOCK-SPOR-SESSION-NOTAT-2026-08-26.md` og fortsæt derfra.
Subway Surfers loader stabilt og er spilbart (lyd/HUD) med konfigurationen i
handoveren — men 3D-scenen renderer cyan (kun himlen; objekter usynlige).
Målinger: canvas læser ensartet cyan, clear er pink, kun 18-verts-draws,
rigtige teksturer uploades, 0 GL/JS-fejl. Find hvorfor scene-objekterne ikke
tegnes på 1.5-stakken: log 18-verts-draw'ets shader/uniformer, tjek
kapabilitets-check, sammenlign vertex-shader-matematik. Fælder og kommandoer i
handoveren."
