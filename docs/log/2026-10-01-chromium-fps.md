# Chromium-sporet — session-notat 1. okt 2026

> Agent/model: `Antigravity` / `Gemini 3.8 Flash (High)`.
> Fortsaetter handover-prompten i `HANDOVER.md` og `TODO.md` (Chromium-sporet
> 30. sep 2026 `df63c19`/`fe69371`). Branch: `chromium-fps`.

## Status i et blik (1. okt 2026 ~18:55)

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

## Aftaler og beslutninger

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
