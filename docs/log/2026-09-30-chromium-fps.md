# Chromium-sporet — session-notat 30. sep 2026

> Agent/model: `codex` / `deepseek-v4-flash` (læst fra DENNE sessions egen
> metadata: `~/.codex/sessions/2026/09/30/rollout-2026-09-30T01-18-31-…jsonl`
> → `"model":"deepseek-v4-flash"`).
> Fortsætter handover-prompten i `TODO.md` (FPS-sporet 26. sep 2026) og
> Firefox-arbejdet i `cyan-polish` (`17d0bc9`). Branch:
> `chromium-fps` fra `cyan-polish`.

## Status i ét blik (30. sep 2026 ~02:10)

- **Branch oprettet:** `chromium-fps` fra `cyan-polish` (`17d0bc9`). Alle
  Firefox-/proxy-/måleværktøjer er derfor med; `trunk` er ancestor til
  `cyan-polish`.
- **Boksen bragt op efter brugerens genstart:** ur sat, `pvrsrvkm` indlæst,
  GPU-stakken kører (`OpenGL ES 3.1 build 1.5@3830101`), proxy vnext12
  `dcc0a68f`, bindapi-lap aktiv, X på tty7.
- **Chromium 150 installeret:** `150.0.7871.181-1~deb13u1` (armhf). Med
  `--no-install-recommends` kom 7 pakker / 267 MB; uden den trak
  Debian-pakken samba/printer-recommends ind.
- **Den foreslåede `--use-gl=egl`-kommando er forkert for Chromium 150.** Log:
  `Requested GL implementation (gl=egl-gles2,angle=none) not found in allowed
  implementations: [(gl=egl-angle,angle=default)]`. Chromium 150 tillader kun
  ANGLE-vejen; den foreslåede kommando falder derfor tilbage uden GPU.
- **Hardware-ANGLE mod hybris-EGL er endnu ikke farbar:**
  - `--use-gl=angle --use-angle=gles-egl`: vores `x11ws`/`egl_proxy` loades,
    men Chromium fejler med `No suitable EGL configs found for initialization`.
    WebGL-konteksten bliver derefter `lost=true` (fejl 37442 =
    `CONTEXT_LOST_WEBGL`).
  - `--use-gl=angle --use-angle=gl-egl`: `eglInitialize OpenGLEGL failed with
    error EGL_NOT_INITIALIZED`.
  - Samme config-fejl med og uden Firefox-specifikke `LD_PRELOAD`-shims.
  - `es3_config_probe` viser at hybris-EGL udbyder både RGB565- og
    RGBA8888-configs med ES2/ES3; fejlen ligger altså i hvilken
    config/visual-kombination ANGLE/Chromium kræver, ikke i at configs mangler.
- **SwiftShader virker for simple sider, men ikke for spillet:**
  - `--use-angle=swiftshader --enable-unsafe-swiftshader` +
    `webgl_test_dump.html`: `OK WebKit WebGL WebGL 2.0 (OpenGL ES 3.0 Chromium)`.
  - Subway Surfers: canvas 1031x580, `raf` ~45/s efter klik, og spillet har
    lyd — men `gl.isContextLost() == true` (37442), GPU-processen falder tilbage
    til `--use-gl=disabled`, og `fb_fps` måler ~5,7 skærm-opdateringer/s.
    Brugeren bekræfter: spilområdet er “crashet”, mens lyden kører.
  - `chromium_cdp.py game` fjerner `failIfMajorPerformanceCaveat` før spillets
    scripts kører; uden det afviser spillet SwiftShader med “This browser does
    not support WebGL.”
- **uBlock Origin-udfordringen:** Devuan-pakken
  `webext-ublock-origin-chromium 1.67.0` er Manifest V2. Chromium 150 afviser
  den med `Cannot install extension because it uses an unsupported manifest
  version.` Enterprise-policyen `ExtensionManifestV2Availability=2` hjalp ikke.
  Debian-wrapperen forsøger at loade pakken automatisk fra
  `/etc/chromium.d/extensions`; i den kørsel faldt GPU-processen til
  `--use-gl=disabled`. Direkte start af `/usr/lib/chromium/chromium` undgår
  wrapperen og genskabte SwiftShader-testen.
- **Nye værktøjer i repoet:** `start_chromium_probe.sh` (reproducerbar
  Chromium-start med hybris-miljø, valgfri ANGLE-backend, profil og log) og
  `chromium_cdp.py` (liste/eval/goto/game via DevTools, adblock-lister og
  `failIfMajorPerformanceCaveat`-lap).
- **GENNEMBRUD (30. sep ~02:40): hardware-EGL-vejen virker nu for Chromium.**
  Efter sporing af `eglChooseConfig` blev tre fejl fundet og lappet i
  `egl_proxy.c`: (1) hybris returnerer RGB565-config først selv når Chromium
  beder om RGBA8888/depth16/stencil8, (2) hybris ignorerer `EGL_CONFIG_ID`, og
  (3) Chromium kræver X-visual `0x21`, mens hybris-configs rapporterer
  1/4/5/0x101. Med `EGL_PROXY_CONFIG_FIX=1` og `EGL_PROXY_FAKE_VISUAL=0x21`
  holder GPU-processen (`--use-gl=angle --use-angle=gles-egl`), WebGL-testen
  svarer `OK WebKit WebGL WebGL 2.0 (OpenGL ES 3.0 Chromium)`, og der er nul
  `GPU process exited` i loggen. Poki/Subway Surfers-siden loades også, men
  CDP-timeout under load og `fb_fps` ~0,9/s i den kørsel betyder at selve fps
  endnu ikke er målt færdig. Firefox-vejen er verificeret uændret med
  `gl_version_probe` (ES 3.1, 1.5@3830101), når config-lappen ikke er slået
  til.
- **Bruger-rapport (30. sep ~02:45): spilleren er synlig, men resten af
  skærmen er regnbue, og spillet er IKKE spilbart.** Hardware-EGL-lappen skal
  derfor foreløbig beskrives som "EGL-init og WebGL-kontekst virker", ikke som
  "browseren renderer korrekt".
- **Isolationsforsøg (30. sep ~02:50): hardware-EGL + software-compositing.**
  Med `--disable-gpu-compositing --disable-gpu-rasterization
  --disable-accelerated-2d-canvas` holder GPU-processen på `angle/gles-egl`,
  der er 0 `GPU process exited`, og `fb_fps` stiger til **~10,4
  skærmopdateringer/s** fra ~0,9/s. Et CDP-screenshot viser et mørkt/grønt
  spilagtigt billede uden den tidligere fuldstændige regnbue, men brugeren
  skal bekræfte om farverne nu er korrekte og spillet faktisk kan ses.

## Aftaler og beslutninger

- [aftalt] 30. sep: Chromium-vejen prøves i en **ny branch**; den skal tage
  udgangspunkt i `cyan-polish`, ikke `trunk`, fordi hele FPS-/hybris-værktøjssættet
  ligger der. (~01:40)
- [udført] Branch `chromium-fps` oprettet fra `cyan-polish` `17d0bc9`. (~01:43)
- [udført] Chromium 150 + `webext-ublock-origin-chromium` installeret på
  boksen; `bringup_after_power.sh` kørt efter genstarten. (~01:29 og ~01:44)
- [udført] Den foreslåede kommando prøvet; `--use-gl=egl` afvises af Chromium
  150. (~01:40)
- [udført] Hardware-ANGLE-varianterne `gles-egl` og `gl-egl` prøvet; begge
  fejler før et brugbart WebGL-kontekst. (~01:41-02:05)
- [udført] SwiftShader-varianten prøvet på testsiden og spillet; testsiden
  virker, spillets kontekst går tabt, mens lyden fortsætter. Brugeren har
  bekræftet “crashet spilområde” med lyd. (~02:00-02:08)
- [udført] uBlock Origin-pakken undersøgt; den er MV2 og kan ikke indlæses i
  Chromium 150. (~01:56)
- [afventer] Beslutning: forfølge hardware-EGL-config-lappen (A) eller lukke
  Chromium-sporet og vende tilbage til Firefox S3 (B). (~02:10)
- [aftalt] Brugeren valgte A: hardware-EGL-config-lappen forfølges. (~02:15)
- [udført] EGL-config-trace og tre lapper i `egl_proxy.c`: config-filter,
  config-id-alias og native-visual-override. (~02:40)
- [udført] WebGL 2.0 verificeret på hardware-EGL-vejen; Poki-spillet loades
  uden GPU-process-exits. (~02:40)
- [afventer] Fps-måling i selve spillet på hardware-vejen; CDP skal have
  længere timeout eller køre uden `--v=1`-spam. (~02:42)
- [udført] Brugeren observerede regnbue/ikke-spilbart; sporet er derfor ikke
  lukket. (~02:45)
- [udført] Software-compositing/raster-varianten prøvet: 0 GPU-exits,
  `fb_fps` ~10,4/s; afventer brugerens vurdering af billedet. (~02:50)

## Hardware-EGL-lappen (30. sep 2026)

Tre konkrete hybris-fejl blev fundet ved at trace `eglChooseConfig` og
`eglGetConfigAttrib`:

1. **Config-rækkefølgen.** Chromium bad om `EGL_SURFACE_TYPE=WINDOW|PBUFFER`,
   RGBA 8/8/8/8, depth 16, stencil 8 og ES3. Hybris returnerede 16 configs,
   men lagde RGB565-config'en `0x1e` først. Chromium bad om én config og
   forkastede den. Lappen filtrerer/sorterer de configs der faktisk opfylder
   minimumskravene, så `0x11` (RGBA8888, depth 24, stencil 8) kommer først.
2. **`EGL_CONFIG_ID` ignoreres.** Chromium spurgte senere efter
   `EGL_CONFIG_ID=0x1a`, men hybris returnerede `0x1e`. Lappen matcher
   config-id eksakt; derefter aliaseres id-forespørgslen til den foretrukne
   config (`0x11`), mens Chromium fortsat ser id `0x1a` via
   `eglGetConfigAttrib`. Det undgår kontekst/surface-mismatch.
3. **X-visual.** Chromiums X-vindue er depth 16 med visual `0x21`, men
   hybris-configs rapporterer visual 1/4/5/0x101. Med
   `EGL_PROXY_FAKE_VISUAL=0x21` rapporteres det visual Chromium faktisk har.

Derudover kaldes `fix_egl_table_once()` nu også fra `eglCreateWindowSurface`,
`eglCreatePbufferSurface`, `eglQuerySurface`, `eglDestroySurface` og
`eglSwapInterval`, så hybris’ tomme funktionstabel udfyldes før de første
surface-kald.

Byg på boksen (bemærk `--no-as-needed`, ellers falder `libEGL_r.so` ud af
`NEEDED`, og `dlsym("eglGetDisplay")` giver NULL):

```bash
gcc -shared -fPIC -O2 -Wl,-soname,libEGL.so.1 -Wl,--no-as-needed \
  -o /root/egl_proxy_trace.so /root/egl_proxy_trace.c \
  -I/usr/local/include -L/opt/hybris -l:libEGL_r.so -ldl
cp -f /root/egl_proxy_trace.so /opt/hybris/libEGL.so.1.0.0
```

Chromium-start på hardware-vejen:

```bash
CHROME=/usr/lib/chromium/chromium \
CHROMIUM_LD_PRELOAD= CHROMIUM_LD_LIBRARY_PATH=/opt/hybris \
CHROMIUM_USE_GL=angle CHROMIUM_USE_ANGLE=gles-egl \
EGL_PROXY_CONFIG_FIX=1 EGL_PROXY_FAKE_VISUAL=0x21 \
CHROMIUM_URL=file:///usr/local/lib/firefox-webgl/webgl_test_dump.html \
nohup /root/start_chromium_probe.sh > /root/chromium_launch.log 2>&1 &
```

`EGL_PROXY_TRACE=1` kan tilføjes for config-trace i
`/tmp/egl_proxy_trace.log`. Trace er kun diagnostik og skal være slået fra i
fps-målinger.

## Kommandoer der virker

Bring-up efter strøm-cyklus:

```bash
bash devuan/gpu/eglplatform_x11/bringup_after_power.sh
```

SwiftShader-test af en simpel WebGL-side (direkte binære, uden Debian-wrapperens
MV2-extension-load):

```bash
bash devuan/box.sh 'CHROME=/usr/lib/chromium/chromium \
  CHROMIUM_USE_GL=angle CHROMIUM_USE_ANGLE=swiftshader \
  CHROMIUM_UNSAFE_SWIFTSHADER=1 \
  CHROMIUM_URL=file:///usr/local/lib/firefox-webgl/webgl_test_dump.html \
  nohup /root/start_chromium_probe.sh > /root/chromium_launch.log 2>&1 &'
```

Spiltest med CDP-reklameblokering og `failIfMajorPerformanceCaveat`-lap:

```bash
# kræver en ssh-tunnel til Chromiums DevTools-port:
#   ssh -i ~/.ssh/geekbox_key -N -L 9223:127.0.0.1:9223 root@<boks-ip>
python3 devuan/gpu/eglplatform_x11/chromium_cdp.py game \
  https://poki.com/en/g/subway-surfers 20
```

## Fælder

- `--use-gl=egl` er ikke en gyldig EGL-tvang i Chromium 150; brug
  `--use-gl=angle` og vælg backend med `--use-angle=`.
- `--use-angle=swiftshader` kræver `--enable-unsafe-swiftshader` for WebGL i
  Chromium 150.
- Unity/WebGL-spil kan afvise software-GL med
  `failIfMajorPerformanceCaveat`; det skal fjernes i et preload-script for at
  komme videre til den egentlige SwiftShader-test.
- Chromium 150 kan ikke loade uBlock Origin 1.67 (MV2); en MV3-adblocker
  (fx uBlock Origin Lite) er nødvendig for fair sammenligning med Firefox.
- DevTools afviser WebSocket-forbindelser fra `http://127.0.0.1:9223`; brug
  `suppress_origin=True` (som `chromium_cdp.py`) eller start Chromium med
  `--remote-allow-origins=*`.
