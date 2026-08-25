# Handover — GL-layers-forsøget, nat 25.→26. aug 2026 (WebGL-frys i Firefox)

Læs først `devuan/gpu/GL-LAYERS-SESSION-NOTAT-2026-08-25.md` (fuldt målenotat,
"Aftaler og beslutninger" + "Status i ét blik" øverst) og `DOKUMENTATION.md`
§5.15d. Dette er overleveringen til næste session: hvor vi er, hvad der er
afkræftet siden sidste commit, og hvad der skal gøres nu.

## Status i ét blik

- **Mål:** finde hvorfor Subway Surfers (poki.com) fryser i firefox-esr på
  boks 1 (192.168.0.188), selvom WebGL 2.0 + UI virker. Slutmål: kunne spille.
- **Mekanisme (målt):** GPU-proces-reset `DeviceResetReason::UNKNOWN
  DeviceResetDetectPlace::WR_POST_UPDATE` → GPU-proces-genstart → WebRender
  renderer sorte frames → sort frosset vindue (skærmen viser sidste present;
  resten af skærmen er fin). Reset er flaky og kan komme BÅDE tidligt
  (present #2–#100) og sent (run G: #950 / ~11 min).
- **Vendor-GL er ren:** standalone probe gennem samme eglplatform_x11-sti =
  300 frames, 0 GL/EGL-fejl. Reset'et er Firefox/WebRender-samspil.
- **Shader-kompileringshypotesen er AFKRÆFTET:** run G (med virkende
  RUST_LOG-webrender-logning) kompilerede alle shaders med "Success" — inkl.
  de præcise shaders fra Bugzilla 1989579 (`ps_text_run_ALPHA_PASS_TEXTURE_2D`,
  `composite_FAST_PATH_TEXTURE_2D`) — og resettede alligevel, uden nogen
  shader-/GL-fejl i loggen. dmesg er tavs (ingen PVR/ION/fence-linjer).
- **Kadence ~1,4 Hz er sandsynligvis transfer-cap ~2M px/s** på
  fuld-vindue-presents (standalone: 640×360→9 fps, 1280×720→2,2 fps,
  1280×948→1,4 fps; Firefox præsenterer altid hele vinduet uanset
  canvas-scale). Separat sag fra reset'et.

## Nyt siden sidste commit (2b3954f)

1. **RUST_LOG virker på ESR — tidligere "blind"-fund var en grep-fejl.**
   Modulnavnet står efter niveauet: `[INFO  webrender::device::gl]`,
   `[WARN  webrender::device::gl]`, `[INFO  webrender::renderer::init]` osv.
   Kørsel: `RUST_LOG=webrender=debug` i start_game.sh (env understøttes nu).
2. **Run G (23:45–23:57, bevis gemt: `devuan/gpu/beviser/ff_rust_runG-2026-08-25.log`):**
   - Fuldt log fra start til reset: WR-init-info, FrameBuilderConfig
     (`default_font_render_mode: Alpha`, `dual_source_blending_is_supported:
     false`, `compositor_kind: Draw { max_partial_present_rects: 1 }`,
     `is_software: false`), alle shader-kompileringer "Success".
   - `x11ws: present #950` → straks efter
     `[GFX1-]: Detect DeviceReset DeviceResetReason::UNKNOWN
     DeviceResetDetectPlace::WR_POST_UPDATE in GPU process`.
   - Efter reset: GPU-proces-genstart (ny WR-init, "vindue pakket ind
     (1280x948)"), system-shims HDMI-dans, `Failed as lost
     WebRenderBridgeChild`, 3× `CompositorBridgeChild ... AbnormalShutdown`,
     IPDL `Msg_NotifyChildRecreated`-fejl — kendt mønster.
   - `pix0=00000000` på alle presents (bufferpixel 0 = transparent sort;
     konsistent med WR-baggrund ColorF{0,0,0,0} + chrome-hjørne — siger ikke
     at hele overfladen er sort).
   - `WaitFlushedEvent`-forsinkelse 2166 ms ÉN gang ved start (ikke
     reset-udløseren).
3. **Reset-tidspunkt revurderet:** tidlige (#2–#100) OG sene (#950 / ~11 min)
   resets — "kun ved start" er forkert; overlevelsesraten er bare højere jo
   længere sessionen varer.
4. **Bugzilla-indsigt (PowerVR + WebRender):** Mozilla blokerer WebRender på
   Android på netop PowerVR Rogue G6110 (bug 1742987 pga. 1742986 border-
   radius + 1717863 sort boks ved opacity) og på Rogue-GPU'er pga.
   glFenceSync-nedbrud i `UploadPBOPool::end_frame` (bug 1773128; Chromium
   har tilsvarende workaround). Desktop-Linux-bloklisten rammer vores boks
   ikke → vi kører WR på en GPU som Firefox selv har fravalgt til WR.
5. **Kadence-forklaring revurderet:** ~1,4 Hz matcher transfer-cap ~2M px/s
   på fuld-vindue-presents (se status). "vsync-kadence-problematik" var en
   fortolkning, ikke målt.
6. **Brugerrapport 23:40:** skærmen viste frosset sidste frame fra
   stress-siden (fire firkanter, nederste halvdel pixeleret ~1 Hz), selvom
   hverken Firefox eller probe kørte — X tegner ikke over området når et
   klientvindue forsvinder; fb-indholdet står tilbage. Skærmen var et
   øjebliksbillede, ikke en levende tilstand.

## Afkræftet / revideret (vigtigt)

- **Buffer-race-hypotesen (destroyBuffers → UAF ved 1×1→resize):** fixet
  (retired-flag, md5 409af875, installeret) eliminerede IKKE reset'et og
  udløste 0 retire-hændelser — ikke bekræftet som rodårsag.
- **Shader-kompileringshypotesen (Bugzilla 1989579):** AFKRÆFTET på vores
  boks (run G: alle shaders Success, reset alligevel, ingen shader-fejl).
- **"Reset kun ved start":** forkert (run G reset ved #950).
- **"RUST_LOG/MOZ_LOG-gfx er blind på ESR":** RUST_LOG virker (grep-fejl).

## Nuværende hypotese (arbejdshypotese)

WR_POST_UPDATE-detektionen ser noget driveren melder som context-lost/ukendt
status — uden logget GL-fejl og uden kernel-signatur. Det er en
userspace-/driver-kontekst-reset. Mulige udløsere at teste: (a)
glGetGraphicsResetStatus-værdien lige før detektion (er den ægte? hvilken
værdi?), (b) fence/PBO-stien (kendt PowerVR-problem, bug 1773128), (c)
ressource-tilstand (CMA/gralloc/teksturcache) der ophobes over ~10 min.

## Næste skridt (i rækkefølge)

1. **Fang hvad WR_POST_UPDATE ser:** GL-shim (LD_PRELOAD der wrapper
   `glGetGraphicsResetStatus`/`glGetError`/`glFenceSync`/`glCompileShader` i
   GPU-processen; symbolopslag via eglGetProcAddress-wrap i egl_platform_shim)
   ELLER gdb-breakpoint på `CheckGraphicsResetStatus`/`glGetGraphicsResetStatus`
   i GPU-processen ved næste reset. Mål værdien der udløser detektionen.
2. **Precache-workaround-test (afbrudt sidst, ikke afprøvet):**
   `user_pref("gfx.webrender.precache-shaders", true)` i
   /home/kristian/ffprof/prefs.js → kør stress-siden 10+ min × 2–3. Selvom
   kompileringsfejl er afkræftet, kan precache + shader-cache stadig ændre
   reset-adfærden (færre midt-session-kompileringer).
3. **uBlock-test:** spillet + stress-side med uBlock 1.74.0 (installeret i
   profilen) — kontrol på om ekstra indhold påvirker reset-raten.
4. **Auto-genstart-workaround (nødløsning):** overvåg `DeviceReset`/sort
   vindue i loggen → `pkill -9 -x firefox-esr` + genstart → spil i bidder.
5. **Kadence (separat):** hvis transfer-cap er skyld i ~1,4 Hz, prøv fx
   mindre vindue, 16-bit buffer direkte (spare konverteringen), eller
   hwcomposer-præsentation. Mål om FPS stiger.

## Fælder + sikkerhedsregler (målt, overtræd ikke)

- **ALDRIG** `MOZ_GL_SPEW=1` eller body-rød-testen.
- **Ingen** `XGetImage(root)`/`dd if=/dev/fb0` under load — kiler
  fb-driverens read() i D-state → sysrq-b-genstart nødvendig.
- Gentagne Firefox-opstarter forringer boksen (glxtest i D-state →
  `FAIL_NO_CONTEXT` / "Exhausted GL driver options"). Efter ~8 opstarter:
  genstart boksen.
- Stop spil/stress med `pkill -9 -x firefox-esr` ved høj load (boksen er
  gået ned to gange; ingen panic-log i 3.10-kernen).
- Tjek VT=tty7 og HDMI=1 efter hvert forsøg.
- /tmp på boksen er tmpfs — gem beviser på laptoppen (repoet:
  `devuan/gpu/beviser/`).
- Firefox omskriver prefs.js ved exit — tjek prefs efter behov.

## Boksens tilstand (26. aug, 00:1x)

- Boksen kører (uptime ~21 min), VT=tty7, HDMI=1.
- **Run G's Firefox kører STADIG** (post-reset, GPU-proces genstartet) med
  stress-siden; log `/tmp/ff_rust.log` (kopi gemt i repoet). Dræb den med
  `pkill -9 -x firefox-esr` før næste forsøg.
- prefs.js er UÆNDRET (ingen precache — den afbrudte kommando nåede ikke at
  køre). Profil: `/home/kristian/ffprof` (gfx.webrender.enabled=false,
  force-disabled=true, gpu-process=true; ingen layers.acceleration.disabled
  = acceleration default TIL).
- Shim: buffer-fixet `/usr/local/lib/libhybris/eglplatform_x11.so`
  (md5 409af875) + `/usr/local/lib/firefox-webgl/{system_shim.so,
  egl_platform_shim.so}` + glxtest-patch + bindapi/driver-patches — ALT på
  plads, intet skal geninstalleres før reboot.
- Firefox-opstarter på denne boot: 1 (run G) — der er plads til flere
  kørsler før genstart anbefales.

## Efter genstart af boksen (root, i rækkefølge)

```bash
# laptop:
bash devuan/gpu/eglplatform_x11/patch_android_bindapi.sh
bash devuan/gpu/eglplatform_x11/patch_driver_minor.sh
# boks:
sh /root/gpu_up.sh
# værktøjer (tmpfs — genopbyg fra repoet):
scp -i /home/kristian/.ssh/geekbox_key devuan/gpu/eglplatform_x11/{start_game.sh,webgl_stress.html,capture_stress.sh,stall_capture.sh,gl_reset_probe.cpp} root@192.168.0.188:/tmp/
# boks: gcc -o /tmp/gl_reset_probe /tmp/gl_reset_probe.cpp -I/usr/local/include -L/opt/hybris -lEGL -lGLESv2 -lX11 -lhybris-common -lm -ldl -lrt
```

## Nøglekommandoer (denne session)

```bash
# RUST_LOG-kørsel (start_game.sh understøtter RUST_LOG):
ssh -i /home/kristian/.ssh/geekbox_key root@192.168.0.188 \
  'pkill -9 -x firefox-esr; rm -f /tmp/ff_rust.log; chvt 7; \
   env GAME_URL="file:///tmp/webgl_stress.html?scale=1&tiles=32&tex=0" \
       GAME_LOG=/tmp/ff_rust.log RUST_LOG=webrender=debug \
       nohup bash /tmp/start_game.sh >/tmp/ff_rust_launch.log 2>&1 &'

# Aflæsning (ingen X/fb-læsninger!):
grep -an "DeviceReset\|Shader(Compilation\|Failed to compile\|Handling webrender" /tmp/ff_rust.log
grep -aoE "x11ws: present #[0-9]+" /tmp/ff_rust.log | tail -1

# Standalone GL-probe (0 afvigelser = vendor ren):
LD_PRELOAD="/usr/local/lib/firefox-webgl/system_shim.so /usr/local/lib/firefox-webgl/egl_platform_shim.so" \
LD_LIBRARY_PATH=/opt/hybris:/usr/local/lib/firefox-webgl EGL_PLATFORM=x11 DISPLAY=:0 \
/tmp/gl_reset_probe --frames 300 --size 1280x720
```

## Repo-tilstand

- Sidste commit: `2b3954f` (gl_reset_probe + Bugzilla-signatur + RUST_LOG-støtte).
- Ikke commitet endnu: session-notat-opdateringer (run G, afkræftelser),
  `devuan/gpu/beviser/ff_rust_runG-2026-08-25.log`, dette handover-dokument,
  `dmesg-mmc/` (uvist indhold — tjek før commit).
- Commit når brugeren er enig i dokumentationen.
