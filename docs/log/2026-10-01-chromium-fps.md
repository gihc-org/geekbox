# Chromium-sporet — session-notat 1. okt 2026

> Agent/model: `Antigravity` / `Gemini 3.8 Flash (High)`.
> Fortsaetter handover-prompten i `HANDOVER.md` og `TODO.md` (Chromium-sporet
> 30. sep 2026 `df63c19`/`fe69371`). Branch: `chromium-fps`.
>
> Fortsat af `codex` / `deepseek-v4-flash` 2. okt 2026 ~00:00-00:30 (model-id
> læst fra denne sessions egen metadata:
> `~/.codex/sessions/2026/10/01/rollout-2026-10-01T23-31-39-…jsonl`
> → `"model":"deepseek-v4-flash"`), efter at boksen var genstartet.

## Status i ét blik (Codex-session, 2. okt 2026 ~00:30)

- **Boksen var genstartet** (ny IP **192.168.0.103**, oppetid 2 t): uret stod på
  2013 (`/root/clock-sync.log`: "Temporary failure in name resolution" ved boot),
  `pvrsrvkm` var IKKE indlæst, og bindapi-lappen var væk. En Chromium-start
  kl. 11:38 (boksens ur) kørte `--use-angle=swiftshader` på
  `webgl_test_dump.html` og brændte 310 % CPU på GPU-processen.
- **EGL-lappen var intakt:** `/opt/hybris/libEGL.so.1.0.0` = md5 `2d66a43c` =
  `/root/egl_proxy_trace.so` (30. sep-bygget med config-fix + visual-override).
  `bringup_after_power.sh` er bevidst IKKE kørt (den ville lægge vnext12 tilbage).
- **Gendannet i drift:** ur sat fra laptoppen, `insmod /root/pvrsrvkm_leddaz.ko` +
  `sh /root/gpu_up.sh` (GL_VERSION = `OpenGL ES 3.1 build 1.5@3830101`),
  `/dev/sw_sync` 0666 og bindapi-lappen kørt med `BOXIP=192.168.0.103`
  (`eglBindAPI` ES2/ES3 giver 1).
- **Præsentationsvejen er målt ren:** otte kendte farvebånd (rød/grøn/blå/hvid/
  gul/magenta/cyan/50 % grå) lagt ind i Chromium og læst tilbage fra fb0: alle
  otte korrekte både inde i og uden for canvas-området. En statisk WebGL-canvas
  med `clearColor(0.2,0.4,0.6)` kom ud som `(49,101,156)` på fb0
  (RGB565-afrunding af `(51,102,153)`). **"Regnbuen" er altså ikke en
  format-/stride-fejl i `x11ws`-presenten** — tidligere fb-dumps viste blot en
  anden frame af den animerede testside.
- **Ren kørsel 23:57** med hardware-EGL (`--use-gl=angle --use-angle=gles-egl`),
  software-compositing (`--disable-gpu-rasterization --disable-gpu-compositing
  --disable-accelerated-2d-canvas`) og `--disable-gpu-watchdog`, uden `--v=1` og
  uden trace, `CHROMIUM_LD_PRELOAD=` (ingen Firefox-shims).
- **Resultat efter ~20 min:** 0 `GPU process exited`, 0 watchdog-hits, 0
  `CONTEXT_LOST`. Spil-framen ("Subway Surfers Web") er loadet (`readyState`
  complete), canvas 1031x580, **WebGL 2.0 med `ANGLE (Imagination Technologies,
  PowerVR Rogue G6110, OpenGL ES 3.1)`** — hardware, ikke SwiftShader.
- **Rater:** spillets rAF 3,0-3,6/s; skærmen (`/root/fb_fps /dev/fb0 20 100`) mest
  aktive blok **5,2 skærm-opdateringer/s** (Firefox-vejen ligger på 2,4-2,6/s).
- **Afventer brugerens øjne:** om skærmen efter klikket viser spillets
  titel-/startskærm eller stadig intro/loading. Spil-framens DOM har kun
  HTML/BODY/CANVAS (ren canvas), så tilstanden kan ikke læses ud af DOM'en.

## Aftaler og beslutninger (Codex-session, 2. okt 2026)

- [udført] FØRSTE OPGAVE fra `HANDOVER.md`: aktuel Chromium-proces, flags og
  skærm verificeret uden at køre `bringup_after_power.sh`. (~00:00)
- [udført] Ur + GPU-stak + bindapi-lap gendannet i drift; EGL-lappen i
  `/opt/hybris` urørt. (~00:05)
- [udført] Ren kørsel uden `--v=1`/trace, med `--disable-gpu-watchdog`,
  hardware-EGL og software-compositing. (~00:00)
- [udført] Præsentationsvejen verificeret mod fb0 med kendte farver (CSS-bånd og
  statisk WebGL-canvas). (~00:15)
- [udført] `chromium_cdp.py` har fået `probe` (inspicér kørende side uden
  navigering), `click` og `screenshot`. (~00:20)
- [afventer] Brugeren skal bekræfte visuelt om spillet nu er spilbart; derefter
  afgøres om fps-loftet (~3,6 rAF/s mod 5,2 skærm-opdateringer/s) skal angribes
  (720p / mindre vindue). (~00:30)
- [udført] `CYAN_DEPTHCLEAR=1` lagt ind i `start_chromium_probe.sh` (samme
  depth-clear-opskrift som Firefox-vejen); proxyen logger force-depth-clear pr.
  frame i spillets fbo. (~00:25)
- [udført] HDMI-faldet diagnosticeret og afhjulpet i drift
  (`echo 1 > /sys/class/display/HDMI/enable`); `restore_display` lagt i
  start-scriptet, så udgangene tændes igen efter nedlukning og 8 s efter start. (~00:35)
- [afventer] Brugeren bekræfter, om figuren/skinnerne er tilbage med
  depth-clear-lappen. (~00:35)

## Kommandoer der virker (Codex-session, 2. okt 2026)

### Opfølgning ~00:35: depth-clear-lappen og HDMI-faldet

- **Bruger-rapport (00:20):** spillet renderer til dels, men er ikke spilbart —
  figuren (drengen) vises ikke, vand står i forgrunden, skinner/rails mangler,
  skygger ses. Det er præcis det gamle cyan-scene-symptom fra Firefox-sporet.
- **Årsag:** proxyen på boksen ER vnext15-kilden (md5 `54133609` = repoets
  `egl_proxy.c`) og indeholder depthmask-lappen + tvungen depth-clear, men den
  sidste er gated på `CYAN_DEPTHCLEAR`, og `start_chromium_probe.sh` satte den
  ikke. Efter tilføjelsen (`CHROMIUM_DEPTHCLEAR=1` som standard) logger proxyen
  `force-depth-clear fbo=23 1031x580 count=5964 att-col=0x1702 att-depth=0x8d41
  depthfunc=0x203 (kald=1..)`, altså én rydning pr. frame i spillets fbo.
- **HDMI-fald:** mens den nye kørsel blev startet, slog kernen HDMI-udgangen fra
  (`hdmi remove from lcdc0`, `switch:en=0`, `/sys/class/display/HDMI/enable=0`)
  uden efterfølgende connect — TV'et blev sort. Det sker, når en
  Chromium-instans med hardware-EGL river sin hybris/gralloc-flade ned.
  `echo 1 > /sys/class/display/HDMI/enable` giver billedet tilbage med det
  samme, og `start_chromium_probe.sh` tænder nu udgangene igen både efter
  nedlukningen og 8 s efter starten (`restore_display`).
- Efter HDMI-genoptagelsen opdaterer fb0 igen (tre forskellige md5 over 8 s) med
  rigeligt indhold, så præsentationen kører videre uændret.

Gendan boksen efter genstart UDEN at røre EGL-lappen:

```bash
bash devuan/find_box.sh
NOW=$(date +%s); bash devuan/box.sh "date -s @$NOW"
bash devuan/box.sh 'lsmod | grep -q "^pvrsrvkm" || insmod /root/pvrsrvkm_leddaz.ko; sh /root/gpu_up.sh; chmod 666 /dev/sw_sync'
BOXIP=<ip> bash devuan/gpu/eglplatform_x11/patch_android_bindapi.sh
```

Ren Chromium-kørsel (hardware-EGL + software-compositing, ingen trace):

```bash
bash devuan/box.sh 'CHROMIUM_LD_PRELOAD= CHROMIUM_USE_GL=angle CHROMIUM_USE_ANGLE=gles-egl \
  CHROMIUM_URL=https://poki.com/en/g/subway-surfers nohup /root/start_chromium_probe.sh > /root/chromium_launch.log 2>&1 &'
ssh -f -N -i ~/.ssh/geekbox_key -o StrictHostKeyChecking=no -L 9223:127.0.0.1:9223 root@<ip>
python3 devuan/gpu/eglplatform_x11/chromium_cdp.py probe 8      # frame/WebGL/rAF uden navigering
python3 devuan/gpu/eglplatform_x11/chromium_cdp.py click 960 540
python3 devuan/gpu/eglplatform_x11/chromium_cdp.py screenshot /tmp/cdp.png
bash devuan/box.sh '/root/fb_fps /dev/fb0 20 100'
```

Fælder (nye, 2. okt):

- Efter genstart: ny IP, ur 2013 (DNS-race i myinit), `pvrsrvkm` ikke indlæst og
  bindapi-lappen væk — men EGL-lappen i `/opt/hybris` overlever.
- En efterladt Chromium kan køre videre med SwiftShader og 310 % CPU;
  `start_chromium_probe.sh` dræber den selv (`pkill -f /usr/lib/chromium/chromium`),
  så en ren kørsel rydder op.
- Et fb0-dump kan være en ANDEN frame af en animeret side end det CDP-screenshot
  man sammenligner med. Sammenlign inden for samme par sekunder, eller brug en
  statisk farvetest som her.
- `CYAN_DEPTHCLEAR=1` skal sættes i Chromium-starten. Uden den er depth-clear-
  lappen i proxyen slået fra, og spillet viser vand i forgrunden, manglende
  figur og manglende skinner (samme symptom som Firefox-sporet før vnext12).
- En hardware-EGL-Chromium kan slukke HDMI-udgangen, når den river sin
  hybris/gralloc-flade ned: kernens `hdmi remove from lcdc0` uden efterfølgende
  connect, og TV'et bliver sort selv om X kører fint. `echo 1 >
  /sys/class/display/HDMI/enable` giver billedet tilbage; start-scriptet gør det
  nu selv.

## Status i et blik (Antigravity-session, 1. okt 2026 ~18:55)

- **Boksen verificeret:** IP 192.168.0.171, oppetid 1 dag 16 timer, load ~3,08.
- **Aktuel Chromium-proces verificeret:** PID 18833 koerer med software-compositing-
  flags (`--disable-gpu-rasterization --disable-gpu-compositing
  --disable-accelerated-2d-canvas`) og hardware-EGL (`--use-gl=angle
  --use-angle=gles-egl`) samt `EGL_PROXY_CONFIG_FIX=1` og
  `EGL_PROXY_FAKE_VISUAL=0x21`.
- **Skaerm-status (fb0):** Raa 1920x1080 mmap-dump (`fb_full.raw` -> `fb_full.png`)
  afkraefter regnbue-fejlen! Billedet er 100 % skarpt og rent: Subway Surfers-
  intrografikken ("SUBWAY SURFERS by SYBO" logo med moerkeblaa baggrund) staar
  centreret i Poki-rammen med korrekte farver og skarp tekst.
- **Aarsag til manglende indlaesning/spilbarhed fundet:**
  Chromiums interne GPU watchdog draebte GPU-processen kl. 03:10
  (`gpu-watchdog-crashed-in-gpu-init`, exit_code=512) under shader-kompilering /
  tung belastning. Three.js rapporterede derefter `CONTEXT_LOST_WEBGL: loseContext`,
  og efter 3 watchdog-drab slog Chromium automatisk over paa
  `--use-gl=disabled`. Spillets renderer-proces (PID 18904) har siden spundet
  med 45 % CPU uden WebGL.
- **Vigtig regel overholdt:** `bringup_after_power.sh` er IKKE koert, saa
  hardware-EGL-lappen i `/opt/hybris/libEGL.so.1.0.0` er fuldt intakt.

## Aftaler og beslutninger (Antigravity-session, 1. okt 2026)

- [aftalt] 1. okt: FØRSTE OPGAVE fra HANDOVER.md udføres: verificér aktuel proces,
  flags og skærm uden at røre `bringup_after_power.sh`. (~17:45)
- [udført] Boksens kørsel og skærm (fb0 mmap) verificeret; regnbue afkræftet,
  subway-logo fundet skarpt, men GPU-watchdog-crash (exit 512) identificeret som
  årsag til WebGL-tab. (~18:50)
- [foreslået] Tilføj `--disable-gpu-watchdog` til Chromium-launcheren for at
  forhindre at den langsomme G6110-shader-kompilering dræbes af Chromiums
  watchdog-timer. (~18:55)
- [foreslået] Kør en ren Chromium-start uden `--v=1` og uden EGL-trace, opret
  SSH-tunnel til port 9223, og tag synkroniseret CDP-screenshot + fb0-dump. (~18:55)

## Verifikation af aktuel tilstand (1. okt 2026)

1. **Chromium-hovedproces (PID 18833):**
   ```bash
   /usr/lib/chromium/chromium --kiosk --no-sandbox --disable-gpu-sandbox \
     --ignore-gpu-blocklist --enable-zero-copy --use-gl=angle \
     --use-angle=gles-egl --disable-gpu-rasterization --disable-gpu-compositing \
     --disable-accelerated-2d-canvas \
     --user-data-dir=/home/kristian/.config/chromium-fps \
     --no-first-run --no-default-browser-check --disable-dev-shm-usage \
     --enable-logging=stderr --remote-debugging-port=9223 \
     https://poki.com/en/g/subway-surfers
   ```
2. **GPU-proces (PID 18959):**
   Faldet tilbage til `--use-gl=disabled` efter 3 watchdog-timeouts:
   - Kl. 03:10:38: `GPU process exited unexpectedly: exit_code=512`
   - `WebGL: CONTEXT_LOST_WEBGL: loseContext: context lost`
   - `THREE.WebGLRenderer: Context Lost.`
   - Minidump `204439a8-5ff5-4d04-98fa-adf1ca5d8d9c.dmp`:
     `gpu-watchdog-crashed-in-gpu-init`.
3. **Skærm (fb0):**
   - Målt via mmap på `/dev/fb0`: 1920x1080x16bpp (4.147.200 bytes).
   - `fb_full.png`: Intet spor af regnbue eller stride-fejl. Præsentationen
     gennem `x11ws` i software-compositing-tilstand virker og afleverer rene
     billeder til X11-vinduet.

## Kommandoer der virker

Fuld mmap-dump af 1080p 16-bpp framebuffer:

```bash
python3 -c 'import mmap; f=open("/dev/fb0","rb"); \
  mm=mmap.mmap(f.fileno(), 4147200, mmap.MAP_SHARED, mmap.PROT_READ); \
  open("/root/fb_full.raw","wb").write(mm.read(4147200))'
```

Konvertering af raw RGB565 til PNG:

```bash
python3 devuan/gpu/diagnostik/rgb565_to_png.py scratch/fb_full.raw scratch/fb_full.png
```
